#include "DualityEditor/Panels/ContentBrowserPanel.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <imgui.h>
#include <imgui_internal.h>

#include "DualityEditor/EditorIcons.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/EditorTheme.h"
#include "DualityEditor/ModelImporter.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Asset/Material.h"
#include "DualityEngine/Asset/MaterialLoader.h"
#include "DualityEngine/Asset/ScriptableObjectLoader.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Project/Project.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/PrefabSerializer.h"
#include "DualityEngine/Scene/SceneSerializer.h"
#include "DualityEngine/Scripting/ScriptableObjectRegistry.h"

namespace Duality {

    // Case-insensitive substring match, same small local-helper shape used by
    // ConsolePanel.cpp/HierarchyPanel.cpp's own search boxes.
    static bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle) {
        if (needle.empty())
            return true;
        auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
            [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
        return it != haystack.end();
    }

    static bool IsPathInside(const std::filesystem::path& path, const std::filesystem::path& root) {
        if (root.empty())
            return false;
        std::error_code error;
        const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path, error);
        if (error)
            return false;
        const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(root, error);
        if (error)
            return false;
        const std::filesystem::path relative = canonicalPath.lexically_relative(canonicalRoot);
        return relative.empty() || (*relative.begin() != "..");
    }

    // Writes a brand-new default `className` instance to "<directory>/<className>[ (N)].asset"
    // (numbered suffix added only if the plain name is already taken, so repeat clicks don't
    // silently clobber an existing asset) and registers it the same 3-step
    // Create -> EnsureMetaFile -> Register sequence used everywhere else a custom asset type is
    // created (Prefab, and Material's own setup) -- the Content Browser's "Create ScriptableObject" flow.
    static void CreateScriptableObjectAsset(const std::filesystem::path& directory, const std::string& className) {
        std::filesystem::path path = directory / (className + ".asset");
        for (int suffix = 1; std::filesystem::exists(path); suffix++)
            path = directory / (className + " (" + std::to_string(suffix) + ").asset");

        ScriptableObjectLoader::Loaded loaded = ScriptableObjectLoader::Create(path.string(), className);
        if (!loaded.Instance) {
            Log::Error("ContentBrowserPanel: failed to create ScriptableObject '" + className + "'");
            return;
        }
        std::string guid = AssetMeta::EnsureMetaFile(path);
        AssetDatabase::Register(guid, path.string());
    }

    // Writes a brand-new default Material (Lit/Diffuse, white, back-face culled -- Unity's
    // Default-Material equivalent) to "<directory>/NewMaterial[
    // (N)].mat" -- same numbered-suffix/3-step-registration shape as CreateScriptableObjectAsset
    // above, the Content Browser's "Create Material" flow.
    static void CreateMaterialAsset(const std::filesystem::path& directory) {
        std::filesystem::path path = directory / "NewMaterial.mat";
        for (int suffix = 1; std::filesystem::exists(path); suffix++)
            path = directory / ("NewMaterial (" + std::to_string(suffix) + ").mat");

        if (!MaterialLoader::Save(path.string(), Material::CreateDefaultLit())) {
            Log::Error("ContentBrowserPanel: failed to create Material at '" + path.string() + "'");
            return;
        }
        std::string guid = AssetMeta::EnsureMetaFile(path);
        AssetDatabase::Register(guid, path.string());
    }

    // Writes a brand-new empty Scene (zero entities) to "<directory>/NewScene[ (N)].scene" --
    // same numbered-suffix/3-step-registration shape as CreateMaterialAsset above, the
    // Content Browser's "Create Scene" flow. Reuses SceneSerializer directly (a throwaway
    // Scene has nothing to differ from what Serialize already writes for zero entities) rather
    // than hand-writing the empty {"Entities":[]} JSON shape a second time. Does NOT open the
    // new scene -- matches Unity's own Project-window "Create > Scene" (creates the asset,
    // doesn't switch to it); double-click it afterward like any other .scene file.
    static void CreateSceneAsset(const std::filesystem::path& directory) {
        std::filesystem::path path = directory / "NewScene.scene";
        for (int suffix = 1; std::filesystem::exists(path); suffix++)
            path = directory / ("NewScene (" + std::to_string(suffix) + ").scene");

        Scene emptyScene;
        if (!SceneSerializer(emptyScene).Serialize(path.string())) {
            Log::Error("ContentBrowserPanel: failed to create Scene at '" + path.string() + "'");
            return;
        }
        std::string guid = AssetMeta::EnsureMetaFile(path);
        AssetDatabase::Register(guid, path.string());
    }

    bool IsCppIdentifier(const std::string& value) {
        if (value.empty())
            return false;
        const auto isFirst = [](unsigned char c) { return std::isalpha(c) || c == '_'; };
        const auto isRest = [](unsigned char c) { return std::isalnum(c) || c == '_'; };
        if (!isFirst(static_cast<unsigned char>(value.front())))
            return false;
        return std::all_of(value.begin() + 1, value.end(), [isRest](unsigned char c) { return isRest(c); });
    }

    std::string DefaultScriptClassName(const std::filesystem::path& directory) {
        std::string className = "NewBehaviour";
        for (int suffix = 1; std::filesystem::exists(directory / (className + ".h")) ||
             std::filesystem::exists(directory / (className + ".cpp")) ||
             std::filesystem::exists(directory / (className + ".ts")); ++suffix)
            className = "NewBehaviour" + std::to_string(suffix);
        return className;
    }

    // Writes a TypeScript MonoBehaviour ("<directory>/<Name>.ts"). The Editor's
    // TypeScriptCompiler notices the new file within a second, bundles the project and the class
    // appears under Add Component -- no Reload Scripts click needed (unlike C++).
    bool CreateTypeScriptAsset(const std::filesystem::path& directory, const std::string& className) {
        if (!IsCppIdentifier(className))
            return false;
        std::filesystem::path path = directory / (className + ".ts");
        if (std::filesystem::exists(path))
            return false;
        std::ofstream file(path);
        if (!file.is_open()) {
            Log::Error("ContentBrowserPanel: failed to create '" + path.string() + "'");
            return false;
        }
        file <<
            "import { MonoBehaviour, Time, Vector3, field } from 'duality';\n\n"
            "export class " << className << " extends MonoBehaviour {\n"
            "  // Public fields with a plain default show up in the Inspector; @field adds options.\n"
            "  @field({ range: [0, 10] }) speed = 1;\n\n"
            "  start() {\n"
            "  }\n\n"
            "  update() {\n"
            "    // this.transform.translate(this.speed * Time.deltaTime, 0, 0);\n"
            "  }\n"
            "}\n";
        return true;
    }

    // Writes a brand-new Behaviour script (`.h` + `.cpp` pair) to "<directory>/<Name>.h"/".cpp"
    // -- lets a project own its own gameplay scripts (compiled into GameScripts alongside the
    // engine's shared/demo scripts, see GameScripts/CMakeLists.txt's
    // DUALITY_PROJECT_SCRIPTS_DIR) with no GameScripts/CMakeLists.txt editing needed. Deliberately
    // NOT the same numbered-suffix shape as CreateSceneAsset/CreateMaterialAsset above
    // ("NewScene (1).scene" is fine since a scene's filename is purely cosmetic) -- a script's
    // filename and its C++ class name (REGISTER_BEHAVIOUR's argument) need to match at creation
    // time, so the suffix has to stay a valid C++ identifier ("NewBehaviour1", not
    // "NewBehaviour (1)"). No AssetMeta/AssetDatabase registration -- scripts are looked up by
    // class name (a plain string), never referenced by GUID like a real asset. Does NOT trigger
    // a rebuild -- matches every other "add a script" flow already documented (README.md/
    // GETTING_STARTED.md): click Reload Scripts afterward, same as always.
    bool CreateScriptAsset(const std::filesystem::path& directory, const std::string& className) {
        if (!IsCppIdentifier(className))
            return false;
        std::filesystem::path headerPath = directory / (className + ".h");
        std::filesystem::path sourcePath = directory / (className + ".cpp");
        if (std::filesystem::exists(headerPath) || std::filesystem::exists(sourcePath))
            return false;

        std::ofstream header(headerPath);
        if (!header.is_open()) {
            Log::Error("ContentBrowserPanel: failed to create '" + headerPath.string() + "'");
            return false;
        }
        header <<
            "#pragma once\n\n"
            "#include \"DualityEngine/Scene/Behaviour.h\"\n\n"
            "class " << className << " : public Duality::Behaviour {\n"
            "public:\n"
            "    void OnCreate() override;\n"
            "    void OnUpdate(float deltaTime) override;\n"
            "};\n";
        header.close();

        std::ofstream source(sourcePath);
        if (!source.is_open()) {
            Log::Error("ContentBrowserPanel: failed to create '" + sourcePath.string() + "'");
            std::error_code error;
            std::filesystem::remove(headerPath, error); // avoid leaving a half-created class pair
            return false;
        }
        source <<
            "#include \"" << className << ".h\"\n\n"
            "#include \"ScriptRegistration.h\"\n\n"
            "void " << className << "::OnCreate() {\n"
            "}\n\n"
            "void " << className << "::OnUpdate(float deltaTime) {\n"
            "}\n\n"
            "REGISTER_BEHAVIOUR(" << className << ")\n";
        source.close();
        return true;
    }

    // Turns an existing scene subtree into a reusable asset at the exact Content Browser
    // directory the user dropped it onto. This mirrors Hierarchy's Create Prefab command but
    // avoids forcing a detour through Assets/Prefabs when an author has selected a different
    // project folder deliberately.
    static void CreatePrefabFromDroppedEntity(EditorContext& ctx, Entity entity,
        const std::filesystem::path& directory) {
        if (!entity || entity.GetScene() != &ctx.SceneRef)
            return;
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            Log::Error("ContentBrowserPanel: could not create prefab directory: " + error.message());
            return;
        }

        std::string name = entity.GetComponent<NameComponent>().Name;
        // Entity names may contain characters a file name cannot ("Cube (Lit/Specular)").
        for (char& c : name)
            if (std::string("\\/:*?\"<>|").find(c) != std::string::npos)
                c = '_';
        if (name.empty())
            name = "NewPrefab";
        std::filesystem::path path = directory / (name + ".prefab");
        for (int suffix = 1; std::filesystem::exists(path); ++suffix)
            path = directory / (name + " (" + std::to_string(suffix) + ").prefab");

        if (!PrefabSerializer::Save(entity, path.string()))
            return;
        const std::string guid = AssetMeta::EnsureMetaFile(path);
        AssetDatabase::Register(guid, path.string());
        if (entity.HasComponent<PrefabInstanceComponent>())
            entity.GetComponent<PrefabInstanceComponent>().Prefab = AssetRef{ guid };
        else
            entity.AddComponent<PrefabInstanceComponent>(PrefabInstanceComponent{ AssetRef{ guid } });
        ctx.Selected = entity;
        ctx.SelectedEntities = { entity };
        MarkSceneDirty(ctx);
        Log::Info("Created prefab from Hierarchy drop: '" + path.string() + "'");
    }

    static void DrawFolderIcon(ImDrawList* drawList, ImVec2 min, ImVec2 max) {
        const ImU32 color = IM_COL32(230, 190, 80, 255);
        float w = max.x - min.x, h = max.y - min.y;
        ImVec2 tabMin(min.x + w * 0.08f, min.y + h * 0.14f);
        ImVec2 tabMax(min.x + w * 0.45f, min.y + h * 0.26f);
        ImVec2 bodyMin(min.x + w * 0.08f, min.y + h * 0.26f);
        ImVec2 bodyMax(min.x + w * 0.92f, min.y + h * 0.84f);
        drawList->AddRectFilled(tabMin, tabMax, color, 2.0f);
        drawList->AddRectFilled(bodyMin, bodyMax, color, 3.0f);
    }

    struct FileIconInfo {
        const char* Glyph;
        const char* Kind;
        ImU32 Accent;
    };

    static FileIconInfo GetFileIconInfo(const std::filesystem::path& path) {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (extension == ".scene")
            return { EditorIcons::Map, "SCENE", IM_COL32(80, 185, 245, 255) };
        if (extension == ".cpp" || extension == ".c" || extension == ".h" || extension == ".hpp" || extension == ".cs")
            return { EditorIcons::Code, "SCRIPT", IM_COL32(95, 205, 150, 255) };
        if (extension == ".mat")
            return { EditorIcons::Material, "MAT", IM_COL32(220, 130, 220, 255) };
        if (extension == ".prefab")
            return { EditorIcons::Cube, "PREFAB", IM_COL32(95, 180, 245, 255) };
        if (extension == ".asset")
            return { EditorIcons::Settings, "ASSET", IM_COL32(235, 175, 80, 255) };
        if (extension == ".obj" || extension == ".fbx" || extension == ".gltf" || extension == ".glb" || extension == ".mesh")
            return { EditorIcons::Cube, "MESH", IM_COL32(90, 190, 210, 255) };
        if (extension == ".wav" || extension == ".ogg" || extension == ".mp3")
            return { EditorIcons::Music, "AUDIO", IM_COL32(235, 120, 110, 255) };
        if (extension == ".ttf" || extension == ".otf")
            return { EditorIcons::File, "FONT", IM_COL32(205, 185, 115, 255) };
        if (extension == ".tilemap")
            return { EditorIcons::Map, "TILES", IM_COL32(105, 195, 180, 255) };
        return { EditorIcons::File, "FILE", IM_COL32(180, 185, 200, 255) };
    }

    static const char* GetModelChildIcon(const std::string& type) {
        if (type == "Mesh") return EditorIcons::Cube;
        if (type == "Material") return EditorIcons::Material;
        if (type == "Texture") return EditorIcons::Image;
        if (type == "Animation") return EditorIcons::Play;
        return EditorIcons::File;
    }

    static void DrawFileIcon(ImDrawList* drawList, ImVec2 min, ImVec2 max, const std::filesystem::path& path) {
        const FileIconInfo info = GetFileIconInfo(path);
        const ImU32 fill = IM_COL32(40, 47, 62, 255);
        const ImU32 border = IM_COL32(80, 92, 115, 255);
        float w = max.x - min.x, h = max.y - min.y;
        ImVec2 bodyMin(min.x + w * 0.2f, min.y + h * 0.06f);
        ImVec2 bodyMax(min.x + w * 0.8f, min.y + h * 0.94f);
        drawList->AddRectFilled(bodyMin, bodyMax, fill, 4.0f);
        drawList->AddRect(bodyMin, bodyMax, border, 4.0f);

        float fold = w * 0.18f;
        ImVec2 p1(bodyMax.x - fold, bodyMin.y);
        ImVec2 p2(bodyMax.x, bodyMin.y);
        ImVec2 p3(bodyMax.x, bodyMin.y + fold);
        drawList->AddTriangleFilled(p1, p2, p3, info.Accent);
        drawList->AddRectFilled(ImVec2(bodyMin.x, bodyMax.y - h * 0.18f), bodyMax, info.Accent, 0.0f);

        ImFont* font = ImGui::GetFont();
        const float glyphSize = std::max(14.0f, w * 0.44f);
        const ImVec2 glyphExtent = font->CalcTextSizeA(glyphSize, 1000.0f, 0.0f, info.Glyph);
        const ImVec2 glyphPos{ (bodyMin.x + bodyMax.x - glyphExtent.x) * 0.5f,
            bodyMin.y + (bodyMax.y - bodyMin.y - h * 0.18f - glyphExtent.y) * 0.5f };
        drawList->AddText(font, glyphSize, glyphPos, info.Accent, info.Glyph);
        const ImVec2 kindExtent = font->CalcTextSizeA(9.0f, 1000.0f, 0.0f, info.Kind);
        drawList->AddText(font, 9.0f, ImVec2((bodyMin.x + bodyMax.x - kindExtent.x) * 0.5f,
            bodyMax.y - h * 0.17f), IM_COL32(18, 22, 30, 255), info.Kind);
    }

    ContentBrowserPanel::ContentBrowserPanel(const std::filesystem::path& rootDirectory)
        : m_RootDirectory(rootDirectory), m_CurrentDirectory(rootDirectory) {
        if (auto project = Project::GetActive())
            m_PackagesDirectory = project->GetPackagesDirectory();
    }

    void ContentBrowserPanel::SetRootDirectory(const std::filesystem::path& rootDirectory) {
        m_RootDirectory = rootDirectory;
        m_CurrentDirectory = rootDirectory;
        if (auto project = Project::GetActive())
            m_PackagesDirectory = project->GetPackagesDirectory();
    }

    bool ContentBrowserPanel::IsPackagesView(const std::filesystem::path& path) const {
        return IsPathInside(path, m_PackagesDirectory);
    }

    std::filesystem::path ContentBrowserPanel::CurrentContentRoot() const {
        return IsPackagesView(m_CurrentDirectory) ? m_PackagesDirectory : m_RootDirectory;
    }

    void ContentBrowserPanel::DrawDirectoryTree(EditorContext& ctx, const std::filesystem::path& directory, const char* label, int depth) {
        if (depth > 24 || !std::filesystem::is_directory(directory))
            return;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (m_CurrentDirectory == directory)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (depth == 0)
            flags |= ImGuiTreeNodeFlags_DefaultOpen;

        const std::string treeLabel = std::string(EditorIcons::Folder) + " " + label;
        const bool open = ImGui::TreeNodeEx(directory.string().c_str(), flags, "%s", treeLabel.c_str());
        if (ImGui::IsItemClicked())
            m_CurrentDirectory = directory;
        if (!open)
            return;

        std::vector<std::filesystem::path> children;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
            if (error)
                break;
            // Meta sidecars are database bookkeeping, never project assets in their own right.
            if (!entry.is_directory(error) && entry.path().extension() == ".meta")
                continue;
            // Generated Mesh/Animation children are browsed below their source model, not as
            // a second implementation-detail folder beside user-authored assets.
            if (entry.is_directory(error) && entry.path().filename() == ".duality-import")
                continue;
            if (!error)
                children.push_back(entry.path());
        }
        std::sort(children.begin(), children.end(), [](const std::filesystem::path& left, const std::filesystem::path& right) {
            const bool leftDirectory = std::filesystem::is_directory(left);
            const bool rightDirectory = std::filesystem::is_directory(right);
            if (leftDirectory != rightDirectory)
                return leftDirectory > rightDirectory;
            return left.filename().string() < right.filename().string();
        });

        for (const std::filesystem::path& child : children) {
            std::error_code childError;
            const bool isDirectory = std::filesystem::is_directory(child, childError);
            if (childError)
                continue;
            if (isDirectory) {
                DrawDirectoryTree(ctx, child, child.filename().string().c_str(), depth + 1);
                continue;
            }

            const bool selected = std::find(ctx.SelectedAssetPaths.begin(), ctx.SelectedAssetPaths.end(), child.string()) !=
                ctx.SelectedAssetPaths.end();
            const ModelImportInfo& model = ModelImporter::Inspect(child);
            const bool hasModelChildren = model.Valid && !model.Children.empty();
            ImGuiTreeNodeFlags fileFlags = ImGuiTreeNodeFlags_SpanAvailWidth;
            if (!hasModelChildren)
                fileFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (selected)
                fileFlags |= ImGuiTreeNodeFlags_Selected;
            const FileIconInfo icon = GetFileIconInfo(child);
            const std::string fileLabel = std::string(icon.Glyph) + " " + child.filename().string();
            const bool modelOpen = ImGui::TreeNodeEx(child.string().c_str(), fileFlags, "%s", fileLabel.c_str());

            const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            const bool doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            if (!IsPackagesView(child) && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                const std::string guid = AssetMeta::EnsureMetaFile(child);
                AssetDatabase::Register(guid, child.string());
                ImGui::SetDragDropPayload("ASSET_GUID", guid.c_str(), guid.size() + 1);
                ImGui::TextUnformatted(child.filename().string().c_str());
                ImGui::EndDragDropSource();
            }

            if (clicked) {
                // Range selection is local to the directory displayed in the grid. Move the
                // grid there first so Shift-click behaves the same from either Project view.
                m_CurrentDirectory = child.parent_path();
                SelectAsset(ctx, child, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
            }
            if (hasModelChildren && modelOpen) {
                for (const ModelSubAssetInfo& subAsset : model.Children) {
                    const std::string childLabel = std::string(GetModelChildIcon(subAsset.Type)) + " " +
                        subAsset.Name + "  " + subAsset.Detail;
                    ImGui::TreeNodeEx((child.string() + "#" + subAsset.Type + std::to_string(subAsset.Index)).c_str(),
                        ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth,
                        "%s", childLabel.c_str());
                    // Cooked mesh/animation children have their own stable .meta GUID, so they
                    // use the exact AssetRef drag contract as an ordinary file. Inventory-only
                    // Material/Texture entries deliberately remain labels until those importers
                    // create first-class .mat/texture assets too.
                    if (!subAsset.AssetGuid.empty() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        ImGui::SetDragDropPayload("ASSET_GUID", subAsset.AssetGuid.c_str(), subAsset.AssetGuid.size() + 1);
                        ImGui::Text("%s %s", GetModelChildIcon(subAsset.Type), subAsset.Name.c_str());
                        ImGui::EndDragDropSource();
                    }
                }
                ImGui::TreePop();
            }
            if (doubleClicked && child.extension() == ".scene") {
                if (!ctx.IsEditingPrefab)
                    OpenSceneWithPrompt(ctx, child.string());
            } else if (doubleClicked && child.extension() == ".prefab") {
                ctx.RequestOpenPrefabPath = child.string();
            }
        }
        ImGui::TreePop();
    }

    void ContentBrowserPanel::BeginRename(const std::filesystem::path& path) {
        if (IsPackagesView(path))
            return;
        m_RenamingPath = path.string();
        std::string currentName = path.filename().string();
        std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", currentName.c_str());
        m_FocusRenameField = true;
    }

    void ContentBrowserPanel::CommitRename(EditorContext& ctx) {
        if (m_RenamingPath.empty())
            return;
        std::filesystem::path oldPath(m_RenamingPath);
        m_RenamingPath.clear();

        std::string newName = m_RenameBuffer;
        if (newName.empty() || newName == oldPath.filename().string())
            return; // blanked out or unchanged -- treat as a cancel, not an error

        std::filesystem::path newPath = oldPath.parent_path() / newName;
        if (std::filesystem::exists(newPath)) {
            Log::Error("ContentBrowserPanel: cannot rename to '" + newPath.string() + "' -- already exists");
            return;
        }

        std::error_code ec;
        std::filesystem::rename(oldPath, newPath, ec);
        if (ec) {
            Log::Error("ContentBrowserPanel: rename failed: " + ec.message());
            return;
        }

        if (!std::filesystem::is_directory(newPath)) {
            // Move the ".meta" alongside it (best-effort -- a file never yet browsed into view
            // has no .meta to move) so the SAME guid survives the rename; AssetDatabase's own
            // mapping is what every existing AssetRef actually resolves through, so updating it
            // here is what keeps those references intact, not the path itself.
            std::filesystem::path oldMeta = oldPath; oldMeta += ".meta";
            std::filesystem::path newMeta = newPath; newMeta += ".meta";
            if (std::filesystem::exists(oldMeta))
                std::filesystem::rename(oldMeta, newMeta, ec);

            std::string guid = AssetMeta::EnsureMetaFile(newPath); // reads the (now-renamed) existing meta rather than regenerating
            AssetDatabase::Register(guid, newPath.string());

            if (ctx.SelectedAssetPath == oldPath.string())
                ctx.SelectedAssetPath = newPath.string();
            for (std::string& selectedPath : ctx.SelectedAssetPaths) {
                if (selectedPath == oldPath.string())
                    selectedPath = newPath.string();
            }
            if (m_RangeAnchorPath == oldPath.string())
                m_RangeAnchorPath = newPath.string();
        }

        Log::Info("Renamed '" + oldPath.string() + "' to '" + newPath.string() + "'");
    }

    void ContentBrowserPanel::NormalizeAssetSelection(EditorContext& ctx) {
        auto& selection = ctx.SelectedAssetPaths;
        selection.erase(std::remove_if(selection.begin(), selection.end(), [](const std::string& path) {
            return !std::filesystem::is_regular_file(path);
        }), selection.end());

        // Selecting an Entity from Hierarchy/Scene takes precedence over stale
        // Project selection just as the old single-selection implementation did.
        if (ctx.Selected) {
            selection.clear();
            m_RangeAnchorPath.clear();
            return;
        }
        if (ctx.SelectedAssetPath.empty() || !std::filesystem::is_regular_file(ctx.SelectedAssetPath)) {
            ctx.SelectedAssetPath.clear();
            selection.clear();
            m_RangeAnchorPath.clear();
            return;
        }
        if (std::find(selection.begin(), selection.end(), ctx.SelectedAssetPath) == selection.end()) {
            selection = { ctx.SelectedAssetPath };
            m_RangeAnchorPath = ctx.SelectedAssetPath;
        }
    }

    void ContentBrowserPanel::SelectAsset(EditorContext& ctx, const std::filesystem::path& path, bool additive, bool range) {
        const std::string clickedPath = path.string();
        auto& selection = ctx.SelectedAssetPaths;

        if (range && !m_RangeAnchorPath.empty()) {
            std::vector<std::filesystem::path> files;
            std::error_code error;
            for (const auto& entry : std::filesystem::directory_iterator(m_CurrentDirectory, error)) {
                if (error)
                    break;
                const std::filesystem::path candidate = entry.path();
                if (!entry.is_regular_file() || candidate.extension() == ".meta")
                    continue;
                if (m_SearchBuffer[0] != '\0' && !ContainsCaseInsensitive(candidate.filename().string(), m_SearchBuffer))
                    continue;
                files.push_back(candidate);
            }
            std::sort(files.begin(), files.end());
            auto first = std::find_if(files.begin(), files.end(), [&](const std::filesystem::path& candidate) {
                return candidate.string() == m_RangeAnchorPath;
            });
            auto last = std::find(files.begin(), files.end(), path);
            if (first != files.end() && last != files.end()) {
                if (first > last)
                    std::swap(first, last);
                selection.clear();
                for (; first != last + 1; ++first)
                    selection.push_back(first->string());
            } else {
                selection = { clickedPath };
                m_RangeAnchorPath = clickedPath;
            }
        } else if (additive) {
            auto found = std::find(selection.begin(), selection.end(), clickedPath);
            if (found == selection.end())
                selection.push_back(clickedPath);
            else
                selection.erase(found);
            if (m_RangeAnchorPath.empty())
                m_RangeAnchorPath = clickedPath;
        } else {
            selection = { clickedPath };
            m_RangeAnchorPath = clickedPath;
        }

        ctx.SelectedAssetPath = selection.empty() ? std::string() :
            (std::find(selection.begin(), selection.end(), clickedPath) != selection.end() ? clickedPath : selection.back());
        ctx.Selected = Entity{};
        ctx.SelectedEntities.clear();
    }

    void ContentBrowserPanel::DuplicateSelectedAsset(EditorContext& ctx) {
        std::vector<std::string> sources = ctx.SelectedAssetPaths;
        if (sources.empty() && !ctx.SelectedAssetPath.empty())
            sources.push_back(ctx.SelectedAssetPath);
        if (sources.empty())
            return;

        std::vector<std::string> duplicates;
        for (const std::string& sourcePath : sources) {
            const std::filesystem::path source(sourcePath);
            if (IsPackagesView(source) || !std::filesystem::is_regular_file(source) || source.extension() == ".meta") {
                Log::Warn("ContentBrowserPanel: Ctrl+D duplicates regular editable assets only");
                continue;
            }

            // A script copy needs a simultaneous class/REGISTER_* rename to compile.
            const std::string extension = source.extension().string();
            if (extension == ".c" || extension == ".cc" || extension == ".cpp" ||
                extension == ".cxx" || extension == ".h" || extension == ".hh" ||
                extension == ".hpp" || extension == ".hxx") {
                Log::Warn("ContentBrowserPanel: skipped C++ script '" + source.filename().string() + "'");
                continue;
            }

            std::filesystem::path destination;
            for (int suffix = 1; ; ++suffix) {
                destination = source.parent_path() /
                    (source.stem().string() + " (" + std::to_string(suffix) + ")" + source.extension().string());
                if (!std::filesystem::exists(destination))
                    break;
            }

            std::error_code error;
            std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, error);
            if (error) {
                Log::Error("ContentBrowserPanel: duplicate failed: " + error.message());
                continue;
            }

            // A fresh .meta GUID makes the newly copied asset distinct in AssetDatabase.
            const std::string guid = AssetMeta::EnsureMetaFile(destination);
            AssetDatabase::Register(guid, destination.string());
            duplicates.push_back(destination.string());
            Log::Info("Duplicated asset: " + destination.string());
        }

        if (duplicates.empty())
            return;
        ctx.SelectedAssetPaths = duplicates;
        ctx.SelectedAssetPath = duplicates.back();
        ctx.Selected = Entity{};
        ctx.SelectedEntities.clear();
        m_RangeAnchorPath = ctx.SelectedAssetPath;
    }

    void ContentBrowserPanel::CreateFolder(const std::filesystem::path& parent) {
        if (IsPackagesView(parent))
            return;
        std::filesystem::path path = parent / "New Folder";
        for (int suffix = 1; std::filesystem::exists(path); suffix++)
            path = parent / ("New Folder (" + std::to_string(suffix) + ")");

        std::error_code ec;
        std::filesystem::create_directory(path, ec);
        if (ec) {
            Log::Error("ContentBrowserPanel: could not create folder: " + ec.message());
            return;
        }
        BeginRename(path);
    }

    void ContentBrowserPanel::BeginCreateScript(bool typeScript) {
        m_CreateTypeScript = typeScript;
        const std::string defaultName = DefaultScriptClassName(m_CurrentDirectory);
        std::snprintf(m_NewScriptName, sizeof(m_NewScriptName), "%s", defaultName.c_str());
        m_FocusNewScriptName = true;
        // This method is called from the nested Create context menu. Defer OpenPopup until
        // that popup has ended; otherwise ImGui records the modal at the menu's popup depth
        // and the root-level BeginPopupModal below can never find it.
        m_RequestCreateScriptDialog = true;
    }

    void ContentBrowserPanel::RequestDelete(const std::filesystem::path& path, bool isDirectory) {
        if (IsPackagesView(path))
            return;
        m_PendingDeletePath = path.string();
        m_PendingDeleteIsDirectory = isDirectory;
    }

    void ContentBrowserPanel::MoveAssetInto(const std::string& guid, const std::filesystem::path& destDir) {
        if (IsPackagesView(destDir))
            return;
        std::string sourcePathStr = AssetDatabase::ResolvePath(guid);
        if (sourcePathStr.empty())
            return;
        std::filesystem::path sourcePath(sourcePathStr);
        std::filesystem::path destPath = destDir / sourcePath.filename();
        if (sourcePath == destPath)
            return; // dropped onto the folder it's already in
        if (std::filesystem::exists(destPath)) {
            Log::Error("ContentBrowserPanel: cannot move '" + sourcePath.string() + "' -- '" + destPath.string() + "' already exists");
            return;
        }

        std::error_code ec;
        std::filesystem::rename(sourcePath, destPath, ec);
        if (ec) {
            Log::Error("ContentBrowserPanel: move failed: " + ec.message());
            return;
        }

        std::filesystem::path oldMeta = sourcePath; oldMeta += ".meta";
        std::filesystem::path newMeta = destPath; newMeta += ".meta";
        if (std::filesystem::exists(oldMeta))
            std::filesystem::rename(oldMeta, newMeta, ec);

        AssetDatabase::Register(guid, destPath.string()); // same guid, new path -- existing AssetRef fields keep resolving
        Log::Info("Moved '" + sourcePath.string() + "' to '" + destPath.string() + "'");
    }

    void ContentBrowserPanel::ImportFile(const std::filesystem::path& sourceFile) {
        if (m_CurrentDirectory.empty())
            return;
        if (IsPackagesView(m_CurrentDirectory)) {
            Log::Warn("ContentBrowserPanel: packages are read-only here; use Package Manager to install or remove them");
            return;
        }

        std::filesystem::path destination = m_CurrentDirectory / sourceFile.filename();
        std::error_code error;
        std::filesystem::copy_file(sourceFile, destination, std::filesystem::copy_options::overwrite_existing, error);
        if (error)
            Log::Error("ContentBrowserPanel: failed to import '" + sourceFile.string() + "': " + error.message());
        else {
            ModelImporter::Invalidate(destination);
            if (ModelImporter::IsSupported(destination))
                ModelImporter::Inspect(destination); // extract parent-model child inventory immediately on drop
            Log::Info("Imported asset: " + destination.string());
        }
    }

    void ContentBrowserPanel::OnImGuiRender(EditorContext& ctx) {
        if (!ctx.Session.ShowProject)
            return;
        // Unity calls this window "Project" (it was "Content Browser"; Application migrates the
        // saved imgui.ini layout entry).
        if (!ImGui::Begin("Project", &ctx.Session.ShowProject)) {
            ImGui::End();
            return;
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ctx.Session.FocusedPanel = EditorPanel::Project;
        NormalizeAssetSelection(ctx);

        // "Ping" (a reference field was clicked / Select Asset): reveal the asset's folder.
        if (!ctx.Session.PingAssetPath.empty() && ctx.Session.PingStartTime != m_HandledPingTime) {
            m_HandledPingTime = ctx.Session.PingStartTime;
            const std::filesystem::path pinged(ctx.Session.PingAssetPath);
            if (std::filesystem::exists(pinged)) {
                m_CurrentDirectory = pinged.parent_path();
                m_SearchBuffer[0] = '\0';
                m_ScrollToPath = std::filesystem::absolute(pinged).lexically_normal().string();
            }
        }

        // Unity-style Project tree: Assets and Packages are separate roots. The divider is a
        // real drag handle rather than a fixed-width child, important for deeply nested package
        // IDs and for script filenames that would otherwise be impossible to distinguish.
        const float footerHeight = ImGui::GetFrameHeightWithSpacing();
        ImVec2 browserSize = ImGui::GetContentRegionAvail();
        browserSize.y = std::max(40.0f, browserSize.y - footerHeight);
        const float minTreeWidth = 140.0f;
        const float splitterWidth = 5.0f;
        const float maxTreeWidth = std::max(minTreeWidth, browserSize.x - 220.0f);
        m_ProjectTreeWidth = std::clamp(m_ProjectTreeWidth, minTreeWidth, maxTreeWidth);
        ImGui::BeginChild("##ContentBrowserTree", ImVec2(m_ProjectTreeWidth, browserSize.y), true);
        ImGui::TextDisabled("PROJECT");
        ImGui::Separator();
        DrawDirectoryTree(ctx, m_RootDirectory, "Assets");
        if (std::filesystem::is_directory(m_PackagesDirectory))
            DrawDirectoryTree(ctx, m_PackagesDirectory, "Packages");
        else
            ImGui::TextDisabled("Packages (none)");
        ImGui::EndChild();
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::InvisibleButton("##ContentBrowserTreeSplitter", ImVec2(splitterWidth, browserSize.y));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive())
            m_ProjectTreeWidth = std::clamp(m_ProjectTreeWidth + ImGui::GetIO().MouseDelta.x, minTreeWidth, maxTreeWidth);
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::BeginChild("##ContentBrowserGrid", ImVec2(0.0f, browserSize.y), false);

        if (!std::filesystem::is_directory(m_CurrentDirectory))
            m_CurrentDirectory = m_RootDirectory;
        const bool readOnlyPackages = IsPackagesView(m_CurrentDirectory);
        const std::filesystem::path contentRoot = CurrentContentRoot();

        // ---- Toolbar: "+" Create menu, clickable breadcrumb (Assets > Folder > Sub), search.
        ImGui::BeginDisabled(readOnlyPackages);
        if (ImGui::Button((std::string(EditorIcons::Add) + " " + EditorIcons::CaretDown).c_str()))
            ImGui::OpenPopup("ProjectCreateMenu");
        ImGui::EndDisabled();
        if (ImGui::BeginPopup("ProjectCreateMenu")) {
            DrawCreateMenuItems(ctx);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        {
            std::vector<std::filesystem::path> crumbs;
            for (std::filesystem::path current = m_CurrentDirectory; ; current = current.parent_path()) {
                crumbs.push_back(current);
                if (current == contentRoot || current == current.parent_path() || current.string().size() <= contentRoot.string().size())
                    break;
            }
            std::reverse(crumbs.begin(), crumbs.end());
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, ImGui::GetStyle().ItemSpacing.y));
            for (size_t i = 0; i < crumbs.size(); ++i) {
                if (i > 0) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", EditorIcons::ChevronRight);
                    ImGui::SameLine();
                }
                const std::string label = (i == 0 ? std::string(readOnlyPackages ? "Packages" : "Assets") : crumbs[i].filename().string()) + "##crumb" + std::to_string(i);
                if (ImGui::Button(label.c_str()))
                    m_CurrentDirectory = crumbs[i];
                // Breadcrumbs accept assets (move) like folders do.
                if (!readOnlyPackages && ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
                        MoveAssetInto(std::string(static_cast<const char*>(payload->Data)), crumbs[i]);
                    ImGui::EndDragDropTarget();
                }
            }
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
        }
        const float searchWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.35f, 120.0f, 260.0f);
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, ImGui::GetWindowContentRegionMax().x - searchWidth));
        ImGui::SetNextItemWidth(searchWidth);
        const std::string searchHint = std::string(EditorIcons::Search) + " Search";
        ImGui::InputTextWithHint("##ContentBrowserSearch", searchHint.c_str(), m_SearchBuffer, sizeof(m_SearchBuffer));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Searches every folder of the current root (Assets or Packages)");
        if (readOnlyPackages)
            ImGui::TextDisabled("Package files are read-only here (Package Manager installs and removes them).");
        ImGui::Separator();

        // Unity: Ctrl+wheel over the grid zooms the icons.
        if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f)
            m_ThumbnailSize = std::clamp(m_ThumbnailSize + ImGui::GetIO().MouseWheel * 8.0f, 32.0f, 160.0f);

        if (!std::filesystem::exists(m_CurrentDirectory)) {
            ImGui::TextDisabled("(Folder not found yet)");
            ImGui::EndChild();
            ImGui::End();
            return;
        }

        const float thumbnailSize = m_ThumbnailSize;
        const float cellPadding = 16.0f;
        const float cellSize = thumbnailSize + cellPadding;
        int columnCount = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cellSize));
        ImGui::Columns(columnCount, nullptr, false);

        // Searching lists matching files from every folder under the current root (Unity's
        // Project search); otherwise just the current folder.
        const bool searching = m_SearchBuffer[0] != '\0';
        std::vector<std::filesystem::directory_entry> entries;
        if (searching) {
            // The recursive walk is cached per query/root and refreshed twice a second.
            const std::string searchKey = contentRoot.string() + "|" + m_SearchBuffer;
            if (searchKey != m_SearchCacheKey || ImGui::GetTime() - m_SearchCacheTime > 0.5) {
                m_SearchCacheKey = searchKey;
                m_SearchCacheTime = ImGui::GetTime();
                m_SearchCache.clear();
                std::error_code error;
                for (auto it = std::filesystem::recursive_directory_iterator(contentRoot, error);
                     !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
                    if (it->is_regular_file() && ContainsCaseInsensitive(it->path().filename().string(), m_SearchBuffer))
                        m_SearchCache.push_back(*it);
                }
            }
            entries = m_SearchCache;
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(m_CurrentDirectory))
                entries.push_back(entry);
        }
        std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
            const bool leftDirectory = left.is_directory();
            const bool rightDirectory = right.is_directory();
            if (leftDirectory != rightDirectory)
                return leftDirectory > rightDirectory;
            return left.path().filename().string() < right.path().filename().string();
        });

        for (const auto& entry : entries) {
            const std::filesystem::path& path = entry.path();
            bool isDirectory = entry.is_directory();

            if (isDirectory && path.filename() == ".duality-import")
                continue;

            // ".meta" sidecars are bookkeeping, not browsable assets in
            // their own right -- Unity/Unreal hide them from their asset
            // views the same way.
            if (!isDirectory && path.extension() == ".meta")
                continue;

            // Folders stay visible regardless of the search box so navigation always
            // works -- only files are filtered by name.
            if (!isDirectory && m_SearchBuffer[0] != '\0' && !ContainsCaseInsensitive(path.filename().string(), m_SearchBuffer))
                continue;

            std::string guid;
            if (!isDirectory && !readOnlyPackages) {
                guid = AssetMeta::EnsureMetaFile(path);
                AssetDatabase::Register(guid, path.string());
            }

            std::string name = path.filename().string();
            const ModelImportInfo& model = !isDirectory ? ModelImporter::Inspect(path) : ModelImportInfo{};
            const bool hasModelChildren = model.Valid && !model.Children.empty();

            ImGui::PushID(name.c_str());
            ImGui::BeginGroup();

            ImVec2 iconMin = ImGui::GetCursorScreenPos();
            ImVec2 iconMax(iconMin.x + thumbnailSize, iconMin.y + thumbnailSize);
            // InvisibleButton's own return value (true on mouse-RELEASE while still hovering
            // the item it was pressed on -- standard ImGui button semantics) instead of raw
            // IsMouseClicked (which fires on mouse-DOWN, before any drag distance is even
            // evaluated) -- the raw-down-edge version used to select this asset in Properties
            // the instant you pressed the mouse button, even when the press was actually the
            // start of dragging it onto an AssetRef field elsewhere, forcing the Properties
            // "Lock" toggle just to drag an asset without losing the field you were dragging
            // onto. BeginDragDropSource() below already withholds the button's own "pressed"
            // return once a drag-drop payload activates (the mouse releases over the drop
            // target, not this item), so this fix needs no other changes to work correctly.
            bool pressed = ImGui::InvisibleButton("##thumb", ImVec2(thumbnailSize, thumbnailSize));
            bool doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

            if (!readOnlyPackages && !isDirectory && !guid.empty() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("ASSET_GUID", guid.c_str(), guid.size() + 1);
                ImGui::Text("%s", name.c_str());
                ImGui::EndDragDropSource();
            }

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            uint32_t thumbnailTexture = 0;
            if (!isDirectory && ThumbnailCache::IsImageFile(path.extension().string()))
                thumbnailTexture = m_Thumbnails.GetThumbnail(path.string());

            if (isDirectory)
                DrawFolderIcon(drawList, iconMin, iconMax);
            else if (thumbnailTexture)
                drawList->AddImage(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(thumbnailTexture)), iconMin, iconMax);
            else
                DrawFileIcon(drawList, iconMin, iconMax, path);

            bool renamingThis = (m_RenamingPath == path.string());
            if (renamingThis) {
                if (m_FocusRenameField) {
                    ImGui::SetKeyboardFocusHere();
                    m_FocusRenameField = false;
                }
                ImGui::SetNextItemWidth(thumbnailSize);
                ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer));
                if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                    m_RenamingPath.clear(); // discard, same as clicking away from an unmodified field
                else if (ImGui::IsItemDeactivated())
                    CommitRename(ctx); // fires on both Enter and click-away
            } else {
                if (hasModelChildren) {
                    const bool expanded = m_ExpandedModelPaths.count(path.string()) != 0;
                    if (ImGui::ArrowButton("##modelChildren", expanded ? ImGuiDir_Down : ImGuiDir_Right)) {
                        if (expanded) m_ExpandedModelPaths.erase(path.string());
                        else m_ExpandedModelPaths.insert(path.string());
                    }
                    ImGui::SameLine(0.0f, 2.0f);
                }
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
                ImGui::TextWrapped("%s", name.c_str());
                ImGui::PopTextWrapPos();
            }

            ImGui::EndGroup();

            // Folders accept a dropped "ASSET_GUID" payload to move that asset into them --
            // attached to the whole group (icon+label) rather than just the thumbnail so the
            // entire cell is a valid drop target, matching how the selection highlight below
            // also reads the group's own combined bounds.
            if (!readOnlyPackages && isDirectory && ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                    std::string draggedGuid(static_cast<const char*>(payload->Data));
                    MoveAssetInto(draggedGuid, path);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
                    if (payload->DataSize == sizeof(entt::entity)) {
                        const entt::entity handle = *static_cast<const entt::entity*>(payload->Data);
                        if (ctx.SceneRef.Registry().valid(handle))
                            CreatePrefabFromDroppedEntity(ctx, Entity(handle, &ctx.SceneRef), path);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // Selection highlight, drawn as an outline after the icon/label so it reads clearly
            // on top of either -- a filled background would need the item's bounds known BEFORE
            // drawing the icon, which BeginGroup/EndGroup's own layout doesn't provide.
            const bool selected = !isDirectory &&
                std::find(ctx.SelectedAssetPaths.begin(), ctx.SelectedAssetPaths.end(), path.string()) != ctx.SelectedAssetPaths.end();
            // Ping highlight (fades out) + scroll the pinged asset into view.
            const std::string normalizedPath = std::filesystem::absolute(path).lexically_normal().string();
            if (!m_ScrollToPath.empty() && normalizedPath == m_ScrollToPath) {
                ImGui::SetScrollHereY(0.5f);
                m_ScrollToPath.clear();
            }
            const double pingAge = ImGui::GetTime() - ctx.Session.PingStartTime;
            if (pingAge < 1.5 && !ctx.Session.PingAssetPath.empty() &&
                std::filesystem::absolute(ctx.Session.PingAssetPath).lexically_normal().string() == normalizedPath) {
                ImDrawList* pingDrawList = ImGui::GetWindowDrawList();
                pingDrawList->PushClipRectFullScreen();
                const ImU32 alpha = static_cast<ImU32>((1.0 - pingAge / 1.5) * 255.0);
                pingDrawList->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), (EditorColors::Ping() & 0x00FFFFFF) | (alpha << 24), 3.0f, 0, 3.0f);
                pingDrawList->PopClipRect();
            }

            if (selected) {
                // Columns installs a per-column clip rectangle. The asset group's label can be
                // wider/taller than that temporary clip, which used to cut the selection outline
                // in half. The item bounds are already known; only the outline needs full-window
                // clipping.
                ImDrawList* selectionDrawList = ImGui::GetWindowDrawList();
                selectionDrawList->PushClipRectFullScreen();
                selectionDrawList->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                    IM_COL32(80, 160, 255, 255), 3.0f, 0, 2.0f);
                selectionDrawList->PopClipRect();
            }

            if (doubleClicked && isDirectory) {
                m_CurrentDirectory = path;
            } else if (doubleClicked && !isDirectory && path.extension() == ".scene") {
                if (!ctx.IsEditingPrefab)
                    OpenSceneWithPrompt(ctx, path.string());
            } else if (doubleClicked && !isDirectory && path.extension() == ".prefab") {
                ctx.RequestOpenPrefabPath = path.string();
            } else if (pressed && !isDirectory && !renamingThis) {
                SelectAsset(ctx, path, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
            }

            if (!isDirectory && ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected)
                SelectAsset(ctx, path, false, false);

            // Per-item context menu -- takes precedence over the background one below thanks to
            // that popup's own ImGuiPopupFlags_NoOpenOverItems.
            if (!renamingThis && ImGui::BeginPopupContextItem("ItemContextMenu")) {
                if (ImGui::MenuItem("Open"))
                    OpenWithShell(path, false);
                if (ImGui::MenuItem("Show in Explorer"))
                    OpenWithShell(path, true);
                if (ImGui::MenuItem("Copy Path"))
                    ImGui::SetClipboardText(path.generic_string().c_str());
                if (!readOnlyPackages) {
                    ImGui::Separator();
                    if (ImGui::MenuItem("Rename", "F2"))
                        BeginRename(path);
                    if (!isDirectory && ImGui::MenuItem("Duplicate", "Ctrl+D"))
                        DuplicateSelectedAsset(ctx);
                    if (ImGui::MenuItem("Delete", "Delete"))
                        RequestDelete(path, isDirectory);
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
            ImGui::NextColumn();

            // Expanded model children are normal grid tiles immediately after their source
            // model, rather than a vertical list inside the model's narrow column. This matches
            // the Project-window mental model: Mesh / Material / Texture / Animation are
            // sibling sub-assets which can be scanned and dragged horizontally.
            if (hasModelChildren && m_ExpandedModelPaths.count(path.string()) != 0) {
                for (const ModelSubAssetInfo& subAsset : model.Children) {
                    const std::string childId = path.string() + "#" + subAsset.Type + std::to_string(subAsset.Index);
                    const std::string label = std::string(GetModelChildIcon(subAsset.Type)) + " " + subAsset.Name;
                    ImGui::PushID(childId.c_str());
                    ImGui::BeginGroup();
                    const ImVec2 childIconMin = ImGui::GetCursorScreenPos();
                    const ImVec2 childIconMax(childIconMin.x + thumbnailSize, childIconMin.y + thumbnailSize);
                    ImGui::InvisibleButton("##modelChildThumb", ImVec2(thumbnailSize, thumbnailSize));
                    const bool childHovered = ImGui::IsItemHovered();
                    // Must be called while InvisibleButton remains LastItemData. Calling it
                    // after EndGroup made ImGui attempt a drag source on an ID-less group,
                    // triggering its explicit assertion in imgui.cpp:14398 when a mesh child
                    // was clicked.
                    if (!subAsset.AssetGuid.empty() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        ImGui::SetDragDropPayload("ASSET_GUID", subAsset.AssetGuid.c_str(), subAsset.AssetGuid.size() + 1);
                        ImGui::Text("%s", label.c_str());
                        ImGui::EndDragDropSource();
                    }
                    ImDrawList* childDrawList = ImGui::GetWindowDrawList();
                    const ImU32 fill = childHovered ? IM_COL32(53, 67, 90, 255) : IM_COL32(40, 47, 62, 255);
                    childDrawList->AddRectFilled(childIconMin, childIconMax, fill, 5.0f);
                    childDrawList->AddRect(childIconMin, childIconMax, IM_COL32(95, 125, 170, 255), 5.0f, 0, 1.5f);
                    const char* glyph = GetModelChildIcon(subAsset.Type);
                    const ImVec2 glyphSize = ImGui::CalcTextSize(glyph);
                    childDrawList->AddText(ImVec2(childIconMin.x + (thumbnailSize - glyphSize.x) * 0.5f,
                        childIconMin.y + (thumbnailSize - glyphSize.y) * 0.5f), IM_COL32(190, 215, 255, 255), glyph);
                    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
                    ImGui::TextWrapped("%s", subAsset.Name.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::TextDisabled("%s", subAsset.Type.c_str());
                    ImGui::EndGroup();
                    if (childHovered && !subAsset.Detail.empty())
                        ImGui::SetTooltip("%s", subAsset.Detail.c_str());
                    ImGui::PopID();
                    ImGui::NextColumn();
                }
            }
        }

        ImGui::Columns(1);

        // Right-click empty space (NoOpenOverItems lets a future per-item context menu take
        // precedence over a row) for Unity's "Create > ScriptableObject > <Type>" -- lists
        // whatever GameScripts currently has REGISTER_SCRIPTABLE_OBJECT'd; empty until GameScripts
        // has been (re)loaded at least once.
        // Blank space below the tiles: Unity's drop target for "drag a GameObject from the
        // Hierarchy to make a prefab", and a click that clears the asset selection.
        {
            ImVec2 blank = ImGui::GetContentRegionAvail();
            blank.x = std::max(blank.x, 1.0f);
            blank.y = std::max(blank.y, ImGui::GetFrameHeight());
            ImGui::InvisibleButton("##ProjectBlank", blank);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift) {
                ctx.SelectedAssetPath.clear();
                ctx.SelectedAssetPaths.clear();
            }
        }
        if (!readOnlyPackages) {
            // The whole grid accepts Hierarchy objects (-> prefab in the current folder).
            const ImRect gridRect(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y));
            if (ImGui::BeginDragDropTargetCustom(gridRect, ImGui::GetID("##ProjectGridDrop"))) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
                    if (payload->DataSize == sizeof(entt::entity)) {
                        const entt::entity handle = *static_cast<const entt::entity*>(payload->Data);
                        if (ctx.SceneRef.Registry().valid(handle))
                            CreatePrefabFromDroppedEntity(ctx, Entity(handle, &ctx.SceneRef), m_CurrentDirectory);
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        if (ImGui::BeginPopupContextWindow("ContentBrowserContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (!readOnlyPackages && ImGui::BeginMenu("Create")) {
                DrawCreateMenuItems(ctx);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Show in Explorer"))
                OpenWithShell(m_CurrentDirectory, false);
            if (ImGui::MenuItem("Refresh")) {
                if (auto project = Project::GetActive())
                    AssetDatabase::Refresh(project->GetAssetsDirectory());
            }
            ImGui::EndPopup();
        }

        // Name the C++ type before writing either file. A Behaviour's filename, class name,
        // include and REGISTER_BEHAVIOUR argument must agree, so a single pre-create dialog is
        // safer and much faster than two ordinary file renames after a NewBehaviour pair exists.
        if (m_RequestCreateScriptDialog) {
            ImGui::OpenPopup("Create Script");
            m_RequestCreateScriptDialog = false;
        }
        if (ImGui::BeginPopupModal("Create Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (m_FocusNewScriptName) {
                ImGui::SetKeyboardFocusHere();
                m_FocusNewScriptName = false;
            }
            ImGui::TextUnformatted(m_CreateTypeScript ? "TypeScript class name (creates <Name>.ts):"
                                                      : "C++ class name (creates matching .h and .cpp):");
            const bool submitted = ImGui::InputText("##NewScriptName", m_NewScriptName, sizeof(m_NewScriptName), ImGuiInputTextFlags_EnterReturnsTrue);
            const std::string className = m_NewScriptName;
            const bool validName = IsCppIdentifier(className);
            const std::filesystem::path headerPath = m_CurrentDirectory / (className + ".h");
            const std::filesystem::path sourcePath = m_CurrentDirectory / (className + ".cpp");
            const std::filesystem::path tsPath = m_CurrentDirectory / (className + ".ts");
            const bool alreadyExists = validName && (m_CreateTypeScript ? std::filesystem::exists(tsPath)
                : (std::filesystem::exists(headerPath) || std::filesystem::exists(sourcePath)));
            if (!validName)
                ImGui::TextDisabled("Use an identifier: letters, digits and _, beginning with a letter or _.");
            else if (alreadyExists)
                ImGui::TextDisabled("A script file with this name already exists.");
            else if (m_CreateTypeScript)
                ImGui::TextDisabled("%s.ts (compiled automatically, no Reload Scripts needed)", className.c_str());
            else
                ImGui::TextDisabled("%s.h and %s.cpp", className.c_str(), className.c_str());
            ImGui::Separator();
            ImGui::BeginDisabled(!validName || alreadyExists);
            if (submitted || ImGui::Button("Create", ImVec2(100.0f, 0.0f))) {
                if (m_CreateTypeScript ? CreateTypeScriptAsset(m_CurrentDirectory, className) : CreateScriptAsset(m_CurrentDirectory, className)) {
                    Log::Info(m_CreateTypeScript ? "Created TypeScript script '" + className + ".ts'"
                                                 : "Created Behaviour script pair '" + className + ".h/.cpp'");
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // Delete confirmation -- there's no Recycle Bin/undo here, so unlike Unity's own
        // immediate-delete Project window context menu, this asks first.
        if (!m_PendingDeletePath.empty() && IsPackagesView(m_PendingDeletePath)) {
            m_PendingDeletePath.clear(); // defensive: package removal belongs to Package Manager.
        }
        if (!m_PendingDeletePath.empty()) {
            ImGui::OpenPopup("Delete Asset?");
            if (ImGui::BeginPopupModal("Delete Asset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                std::string displayName = std::filesystem::path(m_PendingDeletePath).filename().string();
                ImGui::Text("Delete '%s'?", displayName.c_str());
                ImGui::TextDisabled("This cannot be undone.");
                ImGui::Separator();
                if (ImGui::Button("Delete", ImVec2(100, 0))) {
                    std::filesystem::path path(m_PendingDeletePath);
                    std::error_code ec;
                    if (m_PendingDeleteIsDirectory) {
                        std::filesystem::remove_all(path, ec);
                    } else {
                        std::filesystem::remove(path, ec);
                        std::filesystem::path metaPath = path;
                        metaPath += ".meta";
                        std::error_code metaEc;
                        std::filesystem::remove(metaPath, metaEc); // best-effort, doesn't affect the main result
                    }
                    if (ec)
                        Log::Error("ContentBrowserPanel: delete failed: " + ec.message());
                    else
                        Log::Info("Deleted '" + m_PendingDeletePath + "'");
                    m_PendingDeletePath.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                    m_PendingDeletePath.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        // Scope this shortcut to the Project window. WantTextInput keeps Ctrl+D
        // usable by the search and inline-rename fields without creating a file.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_D, false)) {
            DuplicateSelectedAsset(ctx);
        }
        // Unity's Project shortcuts: F2 rename, Delete (asks first).
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
            !ctx.SelectedAssetPath.empty() && !IsPackagesView(ctx.SelectedAssetPath) && m_RenamingPath.empty()) {
            if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
                BeginRename(ctx.SelectedAssetPath);
            else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
                RequestDelete(ctx.SelectedAssetPath, std::filesystem::is_directory(ctx.SelectedAssetPath));
        }

        ImGui::EndChild();

        // ---- Footer: selected asset path (left), icon size slider (right) -- Unity.
        ImGui::AlignTextToFramePadding();
        if (!ctx.SelectedAssetPath.empty()) {
            const std::filesystem::path selectedPath(ctx.SelectedAssetPath);
            const std::string shown = (std::string(EditorIcons::File) + "  " + selectedPath.lexically_relative(m_RootDirectory.parent_path()).generic_string());
            ImGui::TextDisabled("%s", shown.c_str());
        } else {
            ImGui::TextDisabled(" ");
        }
        const float sliderWidth = 120.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, ImGui::GetWindowContentRegionMax().x - sliderWidth));
        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderFloat("##ProjectIconSize", &m_ThumbnailSize, 32.0f, 160.0f, "");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Icon size (Ctrl+mouse wheel over the grid)");
        ImGui::End();
    }

    void ContentBrowserPanel::DrawCreateMenuItems(EditorContext& ctx) {
        (void)ctx;
        if (ImGui::MenuItem("Folder"))
            CreateFolder(m_CurrentDirectory);
        ImGui::Separator();
        if (ImGui::MenuItem("C++ Script"))
            BeginCreateScript(false);
        if (ImGui::MenuItem("TypeScript Script"))
            BeginCreateScript(true);
        ImGui::Separator();
        if (ImGui::MenuItem("Scene"))
            CreateSceneAsset(m_CurrentDirectory);
        if (ImGui::MenuItem("Material"))
            CreateMaterialAsset(m_CurrentDirectory);
        if (ImGui::BeginMenu("ScriptableObject")) {
            const auto& types = ScriptableObjectRegistry::All();
            if (types.empty())
                ImGui::TextDisabled("(none -- load GameScripts first)");
            for (auto& type : types) {
                if (ImGui::MenuItem(type.Name))
                    CreateScriptableObjectAsset(m_CurrentDirectory, type.Name);
            }
            ImGui::EndMenu();
        }
    }

    void ContentBrowserPanel::OpenWithShell(const std::filesystem::path& path, bool revealInExplorer) {
        const std::string absolute = std::filesystem::absolute(path).lexically_normal().string();
        if (revealInExplorer)
            ShellExecuteA(nullptr, "open", "explorer.exe", ("/select,\"" + absolute + "\"").c_str(), nullptr, SW_SHOWNORMAL);
        else
            ShellExecuteA(nullptr, "open", absolute.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

}
