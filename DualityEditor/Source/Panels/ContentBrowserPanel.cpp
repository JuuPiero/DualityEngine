#include "DualityEditor/Panels/ContentBrowserPanel.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

#include <imgui.h>

#include "DualityEditor/EditorContext.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Asset/Material.h"
#include "DualityEngine/Asset/MaterialLoader.h"
#include "DualityEngine/Asset/ScriptableObjectLoader.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Scene/Scene.h"
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

    // Writes a brand-new default (white, no texture) Material to "<directory>/NewMaterial[
    // (N)].mat" -- same numbered-suffix/3-step-registration shape as CreateScriptableObjectAsset
    // above, the Content Browser's "Create Material" flow.
    static void CreateMaterialAsset(const std::filesystem::path& directory) {
        std::filesystem::path path = directory / "NewMaterial.mat";
        for (int suffix = 1; std::filesystem::exists(path); suffix++)
            path = directory / ("NewMaterial (" + std::to_string(suffix) + ").mat");

        if (!MaterialLoader::Save(path.string(), Material{})) {
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
    static void CreateScriptAsset(const std::filesystem::path& directory) {
        std::string className = "NewBehaviour";
        std::filesystem::path headerPath = directory / (className + ".h");
        std::filesystem::path sourcePath = directory / (className + ".cpp");
        for (int suffix = 1; std::filesystem::exists(headerPath) || std::filesystem::exists(sourcePath); suffix++) {
            className = "NewBehaviour" + std::to_string(suffix);
            headerPath = directory / (className + ".h");
            sourcePath = directory / (className + ".cpp");
        }

        std::ofstream header(headerPath);
        if (!header.is_open()) {
            Log::Error("ContentBrowserPanel: failed to create '" + headerPath.string() + "'");
            return;
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
            return;
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

    static void DrawFileIcon(ImDrawList* drawList, ImVec2 min, ImVec2 max) {
        const ImU32 fill = IM_COL32(205, 205, 210, 255);
        const ImU32 border = IM_COL32(110, 110, 115, 255);
        float w = max.x - min.x, h = max.y - min.y;
        ImVec2 bodyMin(min.x + w * 0.2f, min.y + h * 0.06f);
        ImVec2 bodyMax(min.x + w * 0.8f, min.y + h * 0.94f);
        drawList->AddRectFilled(bodyMin, bodyMax, fill, 2.0f);
        drawList->AddRect(bodyMin, bodyMax, border, 2.0f);

        float fold = w * 0.18f;
        ImVec2 p1(bodyMax.x - fold, bodyMin.y);
        ImVec2 p2(bodyMax.x, bodyMin.y);
        ImVec2 p3(bodyMax.x, bodyMin.y + fold);
        drawList->AddTriangleFilled(p1, p2, p3, border);
    }

    ContentBrowserPanel::ContentBrowserPanel(const std::filesystem::path& rootDirectory)
        : m_RootDirectory(rootDirectory), m_CurrentDirectory(rootDirectory) {
    }

    void ContentBrowserPanel::SetRootDirectory(const std::filesystem::path& rootDirectory) {
        m_RootDirectory = rootDirectory;
        m_CurrentDirectory = rootDirectory;
    }

    void ContentBrowserPanel::BeginRename(const std::filesystem::path& path) {
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
        }

        Log::Info("Renamed '" + oldPath.string() + "' to '" + newPath.string() + "'");
    }

    void ContentBrowserPanel::CreateFolder(const std::filesystem::path& parent) {
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

    void ContentBrowserPanel::RequestDelete(const std::filesystem::path& path, bool isDirectory) {
        m_PendingDeletePath = path.string();
        m_PendingDeleteIsDirectory = isDirectory;
    }

    void ContentBrowserPanel::MoveAssetInto(const std::string& guid, const std::filesystem::path& destDir) {
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

        std::filesystem::path destination = m_CurrentDirectory / sourceFile.filename();
        std::error_code error;
        std::filesystem::copy_file(sourceFile, destination, std::filesystem::copy_options::overwrite_existing, error);
        if (error)
            Log::Error("ContentBrowserPanel: failed to import '" + sourceFile.string() + "': " + error.message());
        else
            Log::Info("Imported asset: " + destination.string());
    }

    void ContentBrowserPanel::OnImGuiRender(EditorContext& ctx) {
        ImGui::Begin("Content Browser");

        if (m_CurrentDirectory != m_RootDirectory) {
            if (ImGui::Button("Up"))
                m_CurrentDirectory = m_CurrentDirectory.parent_path();
            ImGui::SameLine();
        }
        ImGui::TextDisabled("%s", m_CurrentDirectory.string().c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##ContentBrowserSearch", "Search...", m_SearchBuffer, sizeof(m_SearchBuffer));
        ImGui::Separator();

        if (!std::filesystem::exists(m_CurrentDirectory)) {
            ImGui::TextDisabled("(Assets folder not found yet)");
            ImGui::End();
            return;
        }

        const float thumbnailSize = 72.0f;
        const float cellPadding = 16.0f;
        const float cellSize = thumbnailSize + cellPadding;
        int columnCount = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cellSize));
        ImGui::Columns(columnCount, nullptr, false);

        for (auto& entry : std::filesystem::directory_iterator(m_CurrentDirectory)) {
            const std::filesystem::path& path = entry.path();
            bool isDirectory = entry.is_directory();

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
            if (!isDirectory) {
                guid = AssetMeta::EnsureMetaFile(path);
                AssetDatabase::Register(guid, path.string());
            }

            std::string name = path.filename().string();

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

            if (!isDirectory && !guid.empty() && ImGui::BeginDragDropSource()) {
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
                DrawFileIcon(drawList, iconMin, iconMax);

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
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
                ImGui::TextWrapped("%s", name.c_str());
                ImGui::PopTextWrapPos();
            }

            ImGui::EndGroup();

            // Folders accept a dropped "ASSET_GUID" payload to move that asset into them --
            // attached to the whole group (icon+label) rather than just the thumbnail so the
            // entire cell is a valid drop target, matching how the selection highlight below
            // also reads the group's own combined bounds.
            if (isDirectory && ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                    std::string draggedGuid(static_cast<const char*>(payload->Data));
                    MoveAssetInto(draggedGuid, path);
                }
                ImGui::EndDragDropTarget();
            }

            // Selection highlight, drawn as an outline after the icon/label so it reads clearly
            // on top of either -- a filled background would need the item's bounds known BEFORE
            // drawing the icon, which BeginGroup/EndGroup's own layout doesn't provide.
            if (!isDirectory && !guid.empty() && path.string() == ctx.SelectedAssetPath) {
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                    IM_COL32(80, 160, 255, 255), 3.0f, 0, 2.0f);
            }

            if (doubleClicked && isDirectory) {
                m_CurrentDirectory = path;
            } else if (doubleClicked && !isDirectory && path.extension() == ".scene") {
                OpenScene(ctx, path.string());
            } else if (pressed && !isDirectory && !renamingThis) {
                ctx.SelectedAssetPath = path.string();
                ctx.Selected = Entity{};
            }

            // Per-item context menu -- takes precedence over the background one below thanks to
            // that popup's own ImGuiPopupFlags_NoOpenOverItems.
            if (!renamingThis && ImGui::BeginPopupContextItem("ItemContextMenu")) {
                if (ImGui::MenuItem("Rename"))
                    BeginRename(path);
                if (ImGui::MenuItem("Delete"))
                    RequestDelete(path, isDirectory);
                ImGui::EndPopup();
            }

            ImGui::PopID();
            ImGui::NextColumn();
        }

        ImGui::Columns(1);

        // Right-click empty space (NoOpenOverItems lets a future per-item context menu take
        // precedence over a row) for Unity's "Create > ScriptableObject > <Type>" -- lists
        // whatever GameScripts currently has REGISTER_SCRIPTABLE_OBJECT'd; empty until GameScripts
        // has been (re)loaded at least once.
        if (ImGui::BeginPopupContextWindow("ContentBrowserContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::BeginMenu("Create")) {
                if (ImGui::MenuItem("Folder"))
                    CreateFolder(m_CurrentDirectory);
                if (ImGui::MenuItem("Scene"))
                    CreateSceneAsset(m_CurrentDirectory);
                if (ImGui::MenuItem("Script"))
                    CreateScriptAsset(m_CurrentDirectory);
                ImGui::Separator();
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
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        // Delete confirmation -- there's no Recycle Bin/undo here, so unlike Unity's own
        // immediate-delete Project window context menu, this asks first.
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

        ImGui::End();
    }

}
