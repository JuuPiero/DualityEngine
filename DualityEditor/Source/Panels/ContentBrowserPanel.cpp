#include "DualityEditor/Panels/ContentBrowserPanel.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

#include <imgui.h>

#include "DualityEditor/EditorIcons.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Asset/Material.h"
#include "DualityEngine/Asset/MaterialLoader.h"
#include "DualityEngine/Asset/ScriptableObjectLoader.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Project/Project.h"
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
             std::filesystem::exists(directory / (className + ".cpp")); ++suffix)
            className = "NewBehaviour" + std::to_string(suffix);
        return className;
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
    static bool CreateScriptAsset(const std::filesystem::path& directory, const std::string& className) {
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

    void ContentBrowserPanel::DrawDirectoryTree(const std::filesystem::path& directory, const char* label, int depth) {
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
            if (!error && entry.is_directory(error))
                children.push_back(entry.path());
        }
        std::sort(children.begin(), children.end());
        for (const std::filesystem::path& child : children)
            DrawDirectoryTree(child, child.filename().string().c_str(), depth + 1);
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

    void ContentBrowserPanel::BeginCreateScript() {
        const std::string defaultName = DefaultScriptClassName(m_CurrentDirectory);
        std::snprintf(m_NewScriptName, sizeof(m_NewScriptName), "%s", defaultName.c_str());
        m_FocusNewScriptName = true;
        ImGui::OpenPopup("Create C++ Behaviour");
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
        else
            Log::Info("Imported asset: " + destination.string());
    }

    void ContentBrowserPanel::OnImGuiRender(EditorContext& ctx) {
        ImGui::Begin("Content Browser");
        NormalizeAssetSelection(ctx);

        // Unity-style Project tree: Assets and Packages are separate roots. Packages are shown
        // here for discovery, but Package Manager owns install/enable/remove so browsing them
        // cannot accidentally create .meta files beside third-party source or manifests.
        ImGui::BeginChild("##ContentBrowserTree", ImVec2(190.0f, 0.0f), true);
        ImGui::TextDisabled("PROJECT");
        ImGui::Separator();
        DrawDirectoryTree(m_RootDirectory, "Assets");
        if (std::filesystem::is_directory(m_PackagesDirectory))
            DrawDirectoryTree(m_PackagesDirectory, "Packages");
        else
            ImGui::TextDisabled("Packages (none)");
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##ContentBrowserGrid", ImVec2(0.0f, 0.0f), false);

        if (!std::filesystem::is_directory(m_CurrentDirectory))
            m_CurrentDirectory = m_RootDirectory;
        const bool readOnlyPackages = IsPackagesView(m_CurrentDirectory);
        const std::filesystem::path contentRoot = CurrentContentRoot();
        if (m_CurrentDirectory != contentRoot) {
            if (ImGui::Button((std::string(EditorIcons::Up) + " Up").c_str()))
                m_CurrentDirectory = m_CurrentDirectory.parent_path();
            ImGui::SameLine();
        }
        std::filesystem::path relativePath = m_CurrentDirectory.lexically_relative(contentRoot);
        const char* rootLabel = readOnlyPackages ? "Packages" : "Assets";
        if (relativePath.empty() || relativePath == ".")
            ImGui::TextDisabled("%s", rootLabel);
        else
            ImGui::TextDisabled("%s / %s", rootLabel, relativePath.generic_string().c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        const std::string searchHint = std::string(EditorIcons::Search) + " Search...";
        ImGui::InputTextWithHint("##ContentBrowserSearch", searchHint.c_str(), m_SearchBuffer, sizeof(m_SearchBuffer));
        if (readOnlyPackages) {
            ImGui::SameLine();
            ImGui::TextDisabled("Package files are read-only here");
        }
        ImGui::Separator();

        if (!std::filesystem::exists(m_CurrentDirectory)) {
            ImGui::TextDisabled("(Folder not found yet)");
            ImGui::EndChild();
            ImGui::End();
            return;
        }

        const float thumbnailSize = 72.0f;
        const float cellPadding = 16.0f;
        const float cellSize = thumbnailSize + cellPadding;
        int columnCount = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cellSize));
        ImGui::Columns(columnCount, nullptr, false);

        std::vector<std::filesystem::directory_entry> entries;
        for (const auto& entry : std::filesystem::directory_iterator(m_CurrentDirectory))
            entries.push_back(entry);
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

            if (!readOnlyPackages && !isDirectory && !guid.empty() && ImGui::BeginDragDropSource()) {
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
                ImGui::EndDragDropTarget();
            }

            // Selection highlight, drawn as an outline after the icon/label so it reads clearly
            // on top of either -- a filled background would need the item's bounds known BEFORE
            // drawing the icon, which BeginGroup/EndGroup's own layout doesn't provide.
            const bool selected = !isDirectory &&
                std::find(ctx.SelectedAssetPaths.begin(), ctx.SelectedAssetPaths.end(), path.string()) != ctx.SelectedAssetPaths.end();
            if (selected) {
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                    IM_COL32(80, 160, 255, 255), 3.0f, 0, 2.0f);
            }

            if (doubleClicked && isDirectory) {
                m_CurrentDirectory = path;
            } else if (doubleClicked && !isDirectory && path.extension() == ".scene") {
                OpenScene(ctx, path.string());
            } else if (pressed && !isDirectory && !renamingThis) {
                SelectAsset(ctx, path, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
            }

            if (!isDirectory && ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected)
                SelectAsset(ctx, path, false, false);

            // Per-item context menu -- takes precedence over the background one below thanks to
            // that popup's own ImGuiPopupFlags_NoOpenOverItems.
            if (!readOnlyPackages && !renamingThis && ImGui::BeginPopupContextItem("ItemContextMenu")) {
                if (ImGui::MenuItem("Rename"))
                    BeginRename(path);
                if (!isDirectory && ImGui::MenuItem("Duplicate", "Ctrl+D")) {
                    DuplicateSelectedAsset(ctx);
                }
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
        if (!readOnlyPackages && ImGui::BeginPopupContextWindow("ContentBrowserContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::BeginMenu("Create")) {
                if (ImGui::MenuItem("Folder"))
                    CreateFolder(m_CurrentDirectory);
                if (ImGui::MenuItem("Scene"))
                    CreateSceneAsset(m_CurrentDirectory);
                if (ImGui::MenuItem("Script"))
                    BeginCreateScript();
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

        // Name the C++ type before writing either file. A Behaviour's filename, class name,
        // include and REGISTER_BEHAVIOUR argument must agree, so a single pre-create dialog is
        // safer and much faster than two ordinary file renames after a NewBehaviour pair exists.
        if (ImGui::BeginPopupModal("Create C++ Behaviour", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (m_FocusNewScriptName) {
                ImGui::SetKeyboardFocusHere();
                m_FocusNewScriptName = false;
            }
            ImGui::TextUnformatted("Class name (creates matching .h and .cpp):");
            const bool submitted = ImGui::InputText("##NewScriptName", m_NewScriptName, sizeof(m_NewScriptName), ImGuiInputTextFlags_EnterReturnsTrue);
            const std::string className = m_NewScriptName;
            const bool validName = IsCppIdentifier(className);
            const std::filesystem::path headerPath = m_CurrentDirectory / (className + ".h");
            const std::filesystem::path sourcePath = m_CurrentDirectory / (className + ".cpp");
            const bool alreadyExists = validName && (std::filesystem::exists(headerPath) || std::filesystem::exists(sourcePath));
            if (!validName)
                ImGui::TextDisabled("Use a C++ identifier: letters, digits and _, beginning with a letter or _.");
            else if (alreadyExists)
                ImGui::TextDisabled("A .h or .cpp with this name already exists.");
            else
                ImGui::TextDisabled("%s.h and %s.cpp", className.c_str(), className.c_str());
            ImGui::Separator();
            ImGui::BeginDisabled(!validName || alreadyExists);
            if (submitted || ImGui::Button("Create", ImVec2(100.0f, 0.0f))) {
                if (CreateScriptAsset(m_CurrentDirectory, className)) {
                    Log::Info("Created Behaviour script pair '" + className + ".h/.cpp'");
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

        ImGui::EndChild();
        ImGui::End();
    }

}
