#include "DualityEditor/Panels/ContentBrowserPanel.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

#include <imgui.h>

#include "DualityEditor/EditorContext.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Asset/Material.h"
#include "DualityEngine/Asset/MaterialLoader.h"
#include "DualityEngine/Asset/ScriptableObjectLoader.h"
#include "DualityEngine/Core/Log.h"
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
            ImGui::InvisibleButton("##thumb", ImVec2(thumbnailSize, thumbnailSize));
            bool doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            bool clicked = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

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

            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
            ImGui::TextWrapped("%s", name.c_str());
            ImGui::PopTextWrapPos();

            ImGui::EndGroup();

            // Selection highlight, drawn as an outline after the icon/label so it reads clearly
            // on top of either -- a filled background would need the item's bounds known BEFORE
            // drawing the icon, which BeginGroup/EndGroup's own layout doesn't provide.
            if (!isDirectory && !guid.empty() && path.string() == ctx.SelectedAssetPath) {
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                    IM_COL32(80, 160, 255, 255), 3.0f, 0, 2.0f);
            }

            if (doubleClicked && isDirectory) {
                m_CurrentDirectory = path;
            } else if (clicked && !isDirectory) {
                ctx.SelectedAssetPath = path.string();
                ctx.Selected = Entity{};
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

        ImGui::End();
    }

}
