#include "DualityEditor/Import/ModelImportUI.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unordered_map>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "DualityEditor/AssetInspectorRegistry.h"
#include "DualityEditor/EditorAutomation.h"
#include "DualityEditor/EditorCommands.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/FieldEditorWidget.h"
#include "DualityEditor/Import/ModelImportSettings.h"
#include "DualityEditor/ModelImporter.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Asset/MaterialLoader.h"
#include "DualityEngine/Asset/MeshLoader.h"
#include "DualityEngine/Asset/ModelAsset.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Project/Project.h"
#include "DualityEngine/Renderer/OpenGL/OpenGLRenderer2D.h"
#include "DualityEngine/Renderer/OpenGL/OpenGLRenderer3D.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/PrefabSerializer.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Duality {

    namespace ModelImportUI {

        namespace {
            // Unapplied Inspector edits per model (Unity keeps them until Apply / Revert).
            std::unordered_map<std::string, ModelImportSettings>& Pending() {
                static std::unordered_map<std::string, ModelImportSettings> pending;
                return pending;
            }
            int s_RequestedTab = -1; // automation: 0 Model, 1 Rig, 2 Animation, 3 Materials
            OpenGLRenderer3D* s_Renderer3D = nullptr;
            OpenGLRenderer2D* s_Renderer2D = nullptr;
            bool s_ReimportedThisFrame = false;

            const ImVec4 Good{ 0.45f, 0.85f, 0.55f, 1.0f };
            const ImVec4 Warn{ 1.0f, 0.78f, 0.32f, 1.0f };
            const ImVec4 Bad{ 1.0f, 0.42f, 0.38f, 1.0f };

            std::string Kilobytes(uint32_t bytes) {
                char text[32];
                if (bytes >= 1024u * 1024u) std::snprintf(text, sizeof(text), "%.2f MB", bytes / (1024.0 * 1024.0));
                else std::snprintf(text, sizeof(text), "%.1f KB", bytes / 1024.0);
                return text;
            }

            template<typename Enum>
            bool EnumCombo(const char* label, Enum& value, const char* const* names, int count) {
                int index = static_cast<int>(value);
                const std::string id = BeginPropertyRow(label);
                if (ImGui::Combo(id.c_str(), &index, names, count)) {
                    value = static_cast<Enum>(index);
                    return true;
                }
                return false;
            }

            bool Check(const char* label, bool& value, const char* tooltip = nullptr) {
                const std::string id = BeginPropertyRow(label);
                const bool changed = ImGui::Checkbox(id.c_str(), &value);
                if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
                return changed;
            }

            bool Float(const char* label, float& value, float speed, float minimum, float maximum, const char* format = "%.3f", const char* tooltip = nullptr) {
                const std::string id = BeginPropertyRow(label);
                const bool changed = ImGui::DragFloat(id.c_str(), &value, speed, minimum, maximum, format);
                if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
                value = std::clamp(value, minimum, maximum);
                return changed;
            }

            void DrawModelTab(ModelImportSettings& settings, const ModelImportInfo& info) {
                ImGui::SeparatorText("Scene");
                Float("Scale Factor", settings.ScaleFactor, 0.01f, 0.0001f, 10000.0f, "%.4f");
                Check("Convert Units", settings.ConvertUnits, "Also scale by the file's unit (FBX centimetres -> metres), like Unity.");
                ImGui::TextDisabled("    File unit: 1 = %.4g m  ->  total scale %.4g", info.FileUnitScale,
                    settings.ScaleFactor * (settings.ConvertUnits ? info.FileUnitScale : 1.0f));
                ImGui::SeparatorText("Meshes");
                static const char* compression[] = { "Off", "On (16-bit positions/UVs, 8-bit normals/weights)" };
                EnumCombo("Mesh Compression", settings.MeshCompression, compression, 2);
                Check("Read/Write", settings.ReadWrite, "Stored in the .dmesh header. Both players currently keep the CPU copy\n(frustum culling and skinning read it).");
                Check("Optimize Mesh", settings.OptimizeMesh, "Reorders triangles for vertex-cache locality (Assimp ImproveCacheLocality).");
                ImGui::SeparatorText("Geometry");
                static const char* normals[] = { "Import", "Calculate", "None" };
                EnumCombo("Normals", settings.Normals, normals, 3);
                if (settings.Normals == ModelNormalsMode::Calculate)
                    Float("Smoothing Angle", settings.SmoothingAngle, 0.5f, 0.0f, 180.0f, "%.0f");
                static const char* tangents[] = { "Import", "Calculate", "None" };
                EnumCombo("Tangents", settings.Tangents, tangents, 3);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tangents are not stored: MeshLoader derives them at load time for normal maps.");
                Check("Import Vertex Colors", settings.ImportVertexColors);
                ImGui::SeparatorText("Textures (3DS)");
                static const int sizes[] = { 32, 64, 128, 256, 512, 1024 };
                static const char* sizeNames[] = { "32", "64", "128", "256", "512", "1024" };
                int sizeIndex = 4;
                for (int i = 0; i < 6; ++i) if (sizes[i] == settings.MaxTextureSize) sizeIndex = i;
                const std::string sizeId = BeginPropertyRow("Max Texture Size");
                if (ImGui::Combo(sizeId.c_str(), &sizeIndex, sizeNames, 6)) settings.MaxTextureSize = sizes[sizeIndex];
                int formatCount = 0;
                const char* const* formats = ModelImportSettings::TextureFormatNames(formatCount);
                int formatIndex = 0;
                for (int i = 0; i < formatCount; ++i) if (settings.TextureFormat == formats[i]) formatIndex = i;
                const std::string formatId = BeginPropertyRow("3DS Format");
                if (ImGui::Combo(formatId.c_str(), &formatIndex, formats, formatCount)) settings.TextureFormat = formats[formatIndex];
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("tex3ds output format for textures cooked from this model.\nAuto = RGBA8 (32 bpp); RGB565 16 bpp; ETC1 4 bpp; ETC1A4 8 bpp.");
                ImGui::TextDisabled("    Resized to the nearest power of two (PICA200 requirement).");
            }

            void DrawRigTab(ModelImportSettings& settings, const ModelImportInfo& info) {
                static const char* rigs[] = { "None", "Generic" };
                EnumCombo("Animation Type", settings.Rig, rigs, 2);
                if (settings.Rig == ModelRigType::None)
                    ImGui::TextDisabled("    No skeleton: skinned meshes import as static bind-pose meshes.");
                int bones = settings.MaxBonesPerVertex;
                const std::string id = BeginPropertyRow("Max Bones/Vertex");
                if (ImGui::SliderInt(id.c_str(), &bones, 1, 4)) settings.MaxBonesPerVertex = bones;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Strongest influences kept and renormalized (the PICA/GL skinning shaders read 4).");
                ImGui::TextDisabled("    Bones per draw: <= %u (PICA200 uniform budget: 24 x 3 vectors)", DMeshMaxPaletteBones);
                ImGui::TextDisabled("    Skeleton: %u bones", info.Budget.Bones);
            }

            void DrawAnimationTab(ModelImportSettings& settings, const ModelImportInfo& info) {
                Check("Import Animation", settings.ImportAnimation);
                if (!settings.ImportAnimation)
                    return;
                Float("Sample Rate", settings.SampleRate, 1.0f, 1.0f, 120.0f, "%.0f fps");
                ImGui::SeparatorText("Anim. Compression");
                Check("Keyframe Reduction", settings.KeyframeReduction);
                if (settings.KeyframeReduction) {
                    Float("Rotation Error", settings.RotationError, 0.05f, 0.0f, 10.0f, "%.2f deg");
                    Float("Position Error", settings.PositionError, 0.05f, 0.0f, 10.0f, "%.2f %%");
                    Float("Scale Error", settings.ScaleError, 0.05f, 0.0f, 10.0f, "%.2f %%");
                }
                ImGui::SeparatorText("Root Motion");
                Check("Remove Root Motion", settings.RemoveRootMotion, "In-place clips: the root bone's XZ motion is locked to the first frame.");
                ImGui::TextDisabled("    Root node: %s", info.RootMotionNode.empty() ? "(none animated)" : info.RootMotionNode.c_str());

                ImGui::SeparatorText("Clips");
                if (info.SourceTakes.empty()) {
                    ImGui::TextDisabled("The file contains no animation.");
                    return;
                }
                if (settings.Clips.empty()) {
                    ImGui::TextDisabled("Default: one looping clip per take (%d).", static_cast<int>(info.SourceTakes.size()));
                    if (ImGui::Button("Edit Clip List"))
                        for (const auto& take : info.SourceTakes)
                            settings.Clips.push_back({ take.Name, take.Name, 0.0f, take.Duration, true });
                    return;
                }
                if (ImGui::BeginTable("##clips", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("Take");
                    ImGui::TableSetupColumn("Start (s)");
                    ImGui::TableSetupColumn("End (s)");
                    ImGui::TableSetupColumn("Loop", ImGuiTableColumnFlags_WidthFixed, 34.0f);
                    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 22.0f);
                    ImGui::TableHeadersRow();
                    int remove = -1;
                    for (size_t i = 0; i < settings.Clips.size(); ++i) {
                        ModelClipSettings& clip = settings.Clips[i];
                        ImGui::PushID(static_cast<int>(i));
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        char name[128];
                        std::snprintf(name, sizeof(name), "%s", clip.Name.c_str());
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::InputText("##name", name, sizeof(name))) clip.Name = name;
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::BeginCombo("##take", clip.Take.c_str())) {
                            for (const auto& take : info.SourceTakes)
                                if (ImGui::Selectable(take.Name.c_str(), take.Name == clip.Take)) clip.Take = take.Name;
                            ImGui::EndCombo();
                        }
                        float takeLength = 0.0f;
                        for (const auto& take : info.SourceTakes) if (take.Name == clip.Take) takeLength = take.Duration;
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::DragFloat("##start", &clip.Start, 0.01f, 0.0f, std::max(0.0f, takeLength), "%.2f");
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-1.0f);
                        if (clip.End < 0.0f) clip.End = takeLength;
                        ImGui::DragFloat("##end", &clip.End, 0.01f, 0.0f, std::max(0.0f, takeLength), "%.2f");
                        ImGui::TableNextColumn();
                        ImGui::Checkbox("##loop", &clip.Loop);
                        ImGui::TableNextColumn();
                        if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                    if (remove >= 0) settings.Clips.erase(settings.Clips.begin() + remove);
                }
                if (ImGui::Button("+ Clip")) {
                    const auto& take = info.SourceTakes.front();
                    settings.Clips.push_back({ take.Name + " " + std::to_string(settings.Clips.size() + 1), take.Name, 0.0f, take.Duration, true });
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset to Takes")) settings.Clips.clear();
            }

            void DrawMaterialsTab(const std::string& path, ModelImportSettings& settings, const ModelImportInfo& info) {
                static const char* creation[] = { "Import (generated, read-only)", "None (default material)" };
                EnumCombo("Material Creation", settings.MaterialCreation, creation, 2);
                if (settings.MaterialCreation == ModelMaterialCreation::None)
                    return;
                ImGui::SeparatorText("Remapped Materials");
                ImGui::TextDisabled("Assign a .mat to use it instead of the generated material.");
                for (const std::string& name : info.SourceMaterials) {
                    FieldValue value = AssetRef{ settings.MaterialRemaps.count(name) ? settings.MaterialRemaps[name] : std::string() };
                    ImGui::PushID(name.c_str());
                    if (DrawFieldValueWidget(name, value)) {
                        const std::string guid = std::get<AssetRef>(value).Guid;
                        if (guid.empty()) settings.MaterialRemaps.erase(name);
                        else settings.MaterialRemaps[name] = guid;
                    }
                    ImGui::PopID();
                }
                ImGui::Spacing();
                const fs::path folder = fs::path(path).parent_path();
                if (ImGui::Button("Extract Materials...")) {
                    const int count = ModelImporter::ExtractMaterials(path, folder / "Materials");
                    Log::Info("Extracted " + std::to_string(count) + " material(s) to '" + (folder / "Materials").string() + "'");
                    Pending().erase(path);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Moves the generated materials to %s as editable .mat files\n(same GUIDs: every reference follows) and remaps them.", (folder / "Materials").string().c_str());
                ImGui::SameLine();
                if (ImGui::Button("Extract Textures...")) {
                    const int count = ModelImporter::ExtractTextures(path, folder / "Textures");
                    Log::Info("Extracted " + std::to_string(count) + " texture(s) to '" + (folder / "Textures").string() + "'");
                    Pending().erase(path);
                }
            }

            void DrawBudget(const ModelImportInfo& info) {
                const ModelBudget& budget = info.Budget;
                if (!ImGui::CollapsingHeader("3DS Budget", ImGuiTreeNodeFlags_DefaultOpen))
                    return;
                ImGui::Text("Triangles: %u    Draw calls: %u    Bones: %u", budget.Triangles, budget.DrawCalls, budget.Bones);
                ImGui::Text("Vertex memory (linear heap): %s    Texture VRAM: %s", Kilobytes(budget.RuntimeVertexBytes).c_str(), Kilobytes(budget.TextureBytes).c_str());
                if (budget.Clips) ImGui::Text("Clips: %u (%u keys after reduction)", budget.Clips, budget.ClipKeys);
                if (!budget.Meshes.empty() && ImGui::BeginTable("##meshBudget", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                    for (const char* column : { "Mesh", "Verts", "Tris", "Draws", "Bones/draw", "Runtime", "File" })
                        ImGui::TableSetupColumn(column);
                    ImGui::TableHeadersRow();
                    for (const ModelBudgetMesh& mesh : budget.Meshes) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(mesh.Name.c_str());
                        ImGui::TableNextColumn(); ImGui::Text("%u", mesh.Vertices);
                        ImGui::TableNextColumn(); ImGui::Text("%u", mesh.Triangles);
                        ImGui::TableNextColumn(); ImGui::Text("%u", mesh.SubMeshes);
                        ImGui::TableNextColumn();
                        if (mesh.Bones) ImGui::TextColored(mesh.MaxPaletteBones <= DMeshMaxPaletteBones ? Good : Bad, "%u / %u", mesh.MaxPaletteBones, DMeshMaxPaletteBones);
                        else ImGui::TextDisabled("-");
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(Kilobytes(mesh.RuntimeBytes).c_str());
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(Kilobytes(mesh.FileBytes).c_str());
                    }
                    ImGui::EndTable();
                }
                if (!budget.Textures.empty() && ImGui::BeginTable("##textureBudget", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                    for (const char* column : { "Texture", "Source -> 3DS", "Format", "VRAM" })
                        ImGui::TableSetupColumn(column);
                    ImGui::TableHeadersRow();
                    for (const ModelBudgetTexture& texture : budget.Textures) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(texture.Name.c_str());
                        ImGui::TableNextColumn(); ImGui::Text("%dx%d -> %dx%d", texture.SourceWidth, texture.SourceHeight, texture.Width, texture.Height);
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(texture.Format.c_str());
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(Kilobytes(texture.Bytes).c_str());
                    }
                    ImGui::EndTable();
                }
                if (budget.Warnings.empty())
                    ImGui::TextColored(Good, "Within the 3DS budget.");
                for (const std::string& warning : budget.Warnings)
                    ImGui::TextColored(Warn, "! %s", warning.c_str());
            }

            void DrawModelInspector(const std::string& path, EditorContext&) {
                const ModelImportInfo& info = ModelImporter::Inspect(path);
                float progress = 0.0f;
                const ModelImportState state = ModelImporter::GetState(path, &progress);
                if (state == ModelImportState::Importing || state == ModelImportState::Queued) {
                    ImGui::ProgressBar(state == ModelImportState::Queued ? 0.0f : progress, ImVec2(-1.0f, 0.0f),
                        state == ModelImportState::Queued ? "Queued for import..." : "Importing...");
                } else if (state == ModelImportState::Failed || !info.Error.empty()) {
                    ImGui::TextColored(Bad, "Import failed: %s", info.Error.c_str());
                } else if (info.Valid) {
                    int meshes = 0, materials = 0, textures = 0, clips = 0;
                    for (const ModelSubAssetInfo& child : info.Children) {
                        meshes += child.Type == "Mesh"; materials += child.Type == "Material";
                        textures += child.Type == "Texture"; clips += child.Type == "Animation";
                    }
                    ImGui::TextColored(Good, "Imported");
                    ImGui::SameLine();
                    ImGui::TextDisabled("%d mesh(es), %d material(s), %d texture(s), %d clip(s) -- drag into the Scene to instantiate", meshes, materials, textures, clips);
                }
                if (ImGui::SmallButton("Reimport"))
                    ModelImporter::RequestImport(path, true);

                const ModelImportSettings saved = ModelImportSettings::Load(path);
                auto& pending = Pending();
                auto found = pending.find(path);
                if (found == pending.end())
                    found = pending.emplace(path, saved).first;
                ModelImportSettings& settings = found->second;

                if (ImGui::BeginTabBar("##ModelImporterTabs")) {
                    const char* tabs[] = { "Model", "Rig", "Animation", "Materials" };
                    for (int tab = 0; tab < 4; ++tab) {
                        const ImGuiTabItemFlags flags = s_RequestedTab == tab ? ImGuiTabItemFlags_SetSelected : 0;
                        if (ImGui::BeginTabItem(tabs[tab], nullptr, flags)) {
                            if (tab == 0) DrawModelTab(settings, info);
                            else if (tab == 1) DrawRigTab(settings, info);
                            else if (tab == 2) DrawAnimationTab(settings, info);
                            else DrawMaterialsTab(path, settings, info);
                            ImGui::EndTabItem();
                        }
                    }
                    s_RequestedTab = -1;
                    ImGui::EndTabBar();
                }

                const bool dirty = settings != saved;
                ImGui::Separator();
                ImGui::BeginDisabled(!dirty);
                if (ImGui::Button("Revert"))
                    pending.erase(path);
                ImGui::SameLine();
                if (ImGui::Button("Apply")) {
                    ModelImportSettings::Save(path, settings);
                    ModelImporter::RequestImport(path, true);
                    pending.erase(path);
                }
                ImGui::EndDisabled();
                if (dirty) {
                    ImGui::SameLine();
                    ImGui::TextColored(Warn, "Unapplied import settings");
                }
                ImGui::Spacing();
                DrawBudget(info);
                if (!info.Warnings.empty() && ImGui::CollapsingHeader("Import Messages")) {
                    for (const std::string& warning : info.Warnings)
                        ImGui::TextWrapped("%s", warning.c_str());
                }
            }

            void DrawMeshAsset(const std::string& path, EditorContext&) {
                const MeshData& mesh = MeshLoader::Load(path);
                if (mesh.Vertices.empty()) {
                    ImGui::TextDisabled("<empty or invalid mesh>");
                    return;
                }
                ImGui::Text("Format: .dmesh v%u%s", mesh.SourceVersion, mesh.SourceVersion < DMeshCurrentVersion ? " (legacy)" : "");
                ImGui::Text("Runtime vertices: %u (%u triangles)", static_cast<unsigned>(mesh.Vertices.size()), static_cast<unsigned>(mesh.Vertices.size() / 3));
                ImGui::Text("Bounds: (%.3f, %.3f, %.3f) - (%.3f, %.3f, %.3f)", mesh.BoundsMin.x, mesh.BoundsMin.y, mesh.BoundsMin.z,
                    mesh.BoundsMax.x, mesh.BoundsMax.y, mesh.BoundsMax.z);
                ImGui::Text("Bones: %u", static_cast<unsigned>(mesh.Bones.size()));
                if (ImGui::BeginTable("##submeshes", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Draw");
                    ImGui::TableSetupColumn("Vertices");
                    ImGui::TableSetupColumn("Palette");
                    ImGui::TableHeadersRow();
                    for (size_t i = 0; i < mesh.SubMeshes.size(); ++i) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::Text("%d", static_cast<int>(i));
                        ImGui::TableNextColumn(); ImGui::Text("%u", mesh.SubMeshes[i].VertexCount);
                        ImGui::TableNextColumn(); ImGui::Text("%u bones", static_cast<unsigned>(mesh.SubMeshes[i].BonePalette.size()));
                    }
                    ImGui::EndTable();
                }
                if (path.find(".duality-import") != std::string::npos)
                    ImGui::TextDisabled("Generated by the model importer (change the model's import settings).");
            }

            fs::path ResolveScriptPath(const std::string& value) {
                fs::path path(value);
                std::error_code error;
                if (fs::exists(path, error))
                    return path;
                if (auto project = Project::GetActive()) {
                    const fs::path inAssets = fs::path(project->GetAssetsDirectory()) / value;
                    if (fs::exists(inAssets, error))
                        return inAssets;
                }
                return path;
            }

            // Automation commands (Docs/pages/editor.html "Model import").
            std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> s_RememberedGuids;

            bool ModelAutomation(EditorContext& ctx, const std::vector<std::string>& args, AutomationApi& api) {
                const std::string& command = args[0];
                auto arg = [&](size_t index) { return index < args.size() ? args[index] : std::string(); };
                if (command == "model-wait-import") {
                    // model-wait-import [frames]: waits until every queued import finished.
                    const int remaining = args.size() > 1 ? std::atoi(arg(1).c_str()) : 3000;
                    if (!ModelImporter::IsIdle()) {
                        if (remaining <= 0) { api.Expect(false, "model import finished in time"); return true; }
                        api.RunNext("model-wait-import " + std::to_string(remaining - 5));
                        api.Wait(5);
                    } else {
                        api.Expect(true, "model imports idle");
                        api.Wait(2);
                    }
                    return true;
                }
                if (command == "select-asset") {
                    const fs::path path = ResolveScriptPath(arg(1));
                    std::error_code error;
                    api.Expect(fs::exists(path, error), "select-asset finds '" + arg(1) + "'");
                    ctx.SelectedAssetPath = path.string();
                    ctx.SelectedAssetPaths = { path.string() };
                    ctx.Session.FocusedPanel = EditorPanel::Project;
                    ctx.Session.PingAssetPath = path.string();
                    ctx.Session.PingStartTime = ImGui::GetTime();
                    return true;
                }
                if (command == "model-tab") {
                    const std::string tab = arg(1);
                    s_RequestedTab = tab == "Rig" ? 1 : tab == "Animation" ? 2 : tab == "Materials" ? 3 : 0;
                    api.Wait(2);
                    return true;
                }
                if (command == "model-expect-child") {
                    // model-expect-child <model> <Type> <minCount>
                    const ModelImportInfo& info = ModelImporter::Inspect(ResolveScriptPath(arg(1)));
                    int count = 0;
                    for (const ModelSubAssetInfo& child : info.Children)
                        if (child.Type == arg(2) && (child.Type == "Avatar" || !child.AssetGuid.empty())) ++count;
                    const int minimum = std::atoi(arg(3).c_str());
                    api.Expect(count >= minimum, arg(1) + " has >= " + arg(3) + " " + arg(2) + " sub-asset(s) (actual " + std::to_string(count) + ")");
                    return true;
                }
                if (command == "model-remember-guids" || command == "model-expect-same-guids") {
                    const fs::path path = ResolveScriptPath(arg(1));
                    const ModelImportInfo& info = ModelImporter::Inspect(path);
                    std::vector<std::pair<std::string, std::string>> guids;
                    for (const ModelSubAssetInfo& child : info.Children)
                        if (!child.AssetGuid.empty()) guids.push_back({ child.Key, child.AssetGuid });
                    if (command == "model-remember-guids") {
                        s_RememberedGuids[path.string()] = guids;
                        api.Expect(!guids.empty(), "remembered " + std::to_string(guids.size()) + " sub-asset GUIDs");
                    } else {
                        const auto& before = s_RememberedGuids[path.string()];
                        api.Expect(!before.empty() && before == guids, "sub-asset GUIDs identical after re-import (" + std::to_string(guids.size()) + ")");
                    }
                    return true;
                }
                if (command == "model-set") {
                    // model-set <model> <SettingKey> <json value>  (edits the Inspector's pending settings)
                    const fs::path path = ResolveScriptPath(arg(1));
                    auto& pending = Pending();
                    auto found = pending.find(path.string());
                    if (found == pending.end()) found = pending.emplace(path.string(), ModelImportSettings::Load(path.string())).first;
                    json value = found->second.ToJson();
                    std::string text;
                    for (size_t i = 3; i < args.size(); ++i) text += (i > 3 ? " " : "") + args[i];
                    const json parsed = json::parse(text, nullptr, false);
                    if (parsed.is_discarded()) {
                        value[arg(2)] = text; // bare string value
                    } else {
                        value[arg(2)] = parsed;
                    }
                    found->second = ModelImportSettings::FromJson(value);
                    return true;
                }
                if (command == "model-apply" || command == "model-revert") {
                    const fs::path path = ResolveScriptPath(arg(1));
                    auto& pending = Pending();
                    auto found = pending.find(path.string());
                    if (command == "model-apply" && found != pending.end()) {
                        ModelImportSettings::Save(path.string(), found->second);
                        ModelImporter::RequestImport(path, true);
                    }
                    pending.erase(path.string());
                    api.Wait(2);
                    return true;
                }
                if (command == "model-reimport") {
                    ModelImporter::RequestImport(ResolveScriptPath(arg(1)), true);
                    api.Wait(2);
                    return true;
                }
                if (command == "model-extract-materials") {
                    const fs::path path = ResolveScriptPath(arg(1));
                    const int count = ModelImporter::ExtractMaterials(path, path.parent_path() / "Materials");
                    api.Expect(count > 0, "extracted " + std::to_string(count) + " material(s)");
                    return true;
                }
                if (command == "model-instantiate") {
                    // model-instantiate <model> [x y z]
                    const fs::path path = ResolveScriptPath(arg(1));
                    const std::string guid = AssetMeta::EnsureMetaFile(path);
                    AssetDatabase::Register(guid, path.string());
                    Entity instance = InstantiateModel(ctx, guid, Entity{});
                    api.Expect(static_cast<bool>(instance), "instantiated model '" + arg(1) + "'");
                    if (instance && args.size() >= 5)
                        instance.GetComponent<TransformComponent>().Translation = { static_cast<float>(std::atof(arg(2).c_str())),
                            static_cast<float>(std::atof(arg(3).c_str())), static_cast<float>(std::atof(arg(4).c_str())) };
                    return true;
                }
                if (command == "model-expect-budget") {
                    // model-expect-budget <model> <maxTriangles>: also checks every draw fits the palette.
                    const ModelImportInfo& info = ModelImporter::Inspect(ResolveScriptPath(arg(1)));
                    bool palettesOk = true;
                    for (const ModelBudgetMesh& mesh : info.Budget.Meshes) palettesOk &= mesh.MaxPaletteBones <= DMeshMaxPaletteBones;
                    const uint32_t maximum = static_cast<uint32_t>(std::atoi(arg(2).c_str()));
                    api.Expect(palettesOk && info.Budget.Triangles > 0 && info.Budget.Triangles <= maximum,
                        arg(1) + " budget: " + std::to_string(info.Budget.Triangles) + " triangles, palettes <= 24");
                    return true;
                }
                return false;
            }

            const bool s_AutomationRegistered = (EditorAutomation::RegisterExtension(&ModelAutomation), true);
        }

        void RegisterInspectors() {
            for (const char* extension : { ".fbx", ".gltf", ".glb", ".obj" })
                AssetInspectorRegistry::Register({ extension, "Model Import Settings", &DrawModelInspector });
            AssetInspectorRegistry::Register({ ".dmesh", "Mesh", &DrawMeshAsset });
        }

        void Update(EditorContext& ctx) {
            s_Renderer3D = &ctx.Renderer3D;
            s_Renderer2D = &ctx.Renderer;
            static bool listenerInstalled = false;
            if (!listenerInstalled) {
                listenerInstalled = true;
                ModelImporter::SetReimportListener([](const std::string&) { s_ReimportedThisFrame = true; });
            }
            fs::path assetsRoot;
            if (auto project = Project::GetActive())
                assetsRoot = project->GetAssetsDirectory();
            ModelImporter::Tick(assetsRoot);
            if (s_ReimportedThisFrame) {
                // GPU copies are cached by path: drop them so the new .dmesh / .png data is used.
                s_ReimportedThisFrame = false;
                if (s_Renderer3D) {
                    s_Renderer3D->UnloadAllMeshes();
                    s_Renderer3D->UnloadAllTextures();
                }
                MaterialLoader::ClearCache();
            }

            const int jobs = ModelImporter::PendingJobCount();
            if (jobs <= 0)
                return;
            float progress = 0.0f;
            const std::string label = ModelImporter::ActiveJobLabel(&progress);
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 12.0f, viewport->WorkPos.y + viewport->WorkSize.y - 12.0f), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
            ImGui::SetNextWindowBgAlpha(0.92f);
            ImGui::SetNextWindowViewport(viewport->ID);
            if (ImGui::Begin("##ModelImportProgress", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking)) {
                ImGui::Text("Importing %s%s", label.empty() ? "models" : label.c_str(), jobs > 1 ? (" (+" + std::to_string(jobs - 1) + " queued)").c_str() : "");
                ImGui::ProgressBar(progress, ImVec2(260.0f, 0.0f));
            }
            ImGui::End();
        }

        void DrawTileBadge(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const fs::path& path) {
            if (!ModelImporter::IsSupported(path))
                return;
            float progress = 0.0f;
            const ModelImportState state = ModelImporter::GetState(path, &progress);
            if (state == ModelImportState::Queued || state == ModelImportState::Importing) {
                drawList->AddRectFilled(min, max, IM_COL32(10, 14, 22, 170), 4.0f);
                const char* text = state == ModelImportState::Queued ? "Queued" : "Importing";
                const ImVec2 size = ImGui::CalcTextSize(text);
                drawList->AddText(ImVec2((min.x + max.x - size.x) * 0.5f, (min.y + max.y) * 0.5f - size.y), IM_COL32(220, 230, 255, 255), text);
                const float barY = max.y - 8.0f;
                drawList->AddRectFilled(ImVec2(min.x + 6.0f, barY), ImVec2(max.x - 6.0f, barY + 4.0f), IM_COL32(50, 60, 80, 255), 2.0f);
                drawList->AddRectFilled(ImVec2(min.x + 6.0f, barY), ImVec2(min.x + 6.0f + (max.x - min.x - 12.0f) * progress, barY + 4.0f), IM_COL32(85, 160, 255, 255), 2.0f);
            } else if (state == ModelImportState::Failed) {
                drawList->AddRectFilled(ImVec2(max.x - 18.0f, min.y + 2.0f), ImVec2(max.x - 2.0f, min.y + 18.0f), IM_COL32(200, 60, 50, 255), 3.0f);
                drawList->AddText(ImVec2(max.x - 13.0f, min.y + 3.0f), IM_COL32(255, 255, 255, 255), "!");
            }
        }

        Entity InstantiateModel(EditorContext& ctx, const std::string& modelGuid, Entity parent) {
            const std::string prefab = ModelAsset::ResolveInstantiablePrefab(modelGuid);
            if (prefab.empty())
                return {};
            Entity instance = PrefabSerializer::Instantiate(ctx.SceneRef, prefab, {}, AssetRef{ modelGuid });
            if (!instance)
                return {};
            ctx.SceneRef.SetParent(instance, parent ? parent : ctx.SceneRef.EnsureScreenRoot(Screen::Top), {}, true);
            EditorCommands::SelectOnly(ctx, instance);
            ctx.Session.RequestRevealSelection = true;
            MarkSceneDirty(ctx, "Instantiate Model");
            return instance;
        }
    }

}
