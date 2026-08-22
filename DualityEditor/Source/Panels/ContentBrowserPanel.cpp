#include "DualityEditor/Panels/ContentBrowserPanel.h"

#include <algorithm>
#include <cstdint>

#include <imgui.h>

namespace Duality {

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

    void ContentBrowserPanel::OnImGuiRender() {
        ImGui::Begin("Content Browser");

        if (m_CurrentDirectory != m_RootDirectory) {
            if (ImGui::Button("Up"))
                m_CurrentDirectory = m_CurrentDirectory.parent_path();
            ImGui::SameLine();
        }
        ImGui::TextDisabled("%s", m_CurrentDirectory.string().c_str());
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
            std::string name = path.filename().string();
            bool isDirectory = entry.is_directory();

            ImGui::PushID(name.c_str());
            ImGui::BeginGroup();

            ImVec2 iconMin = ImGui::GetCursorScreenPos();
            ImVec2 iconMax(iconMin.x + thumbnailSize, iconMin.y + thumbnailSize);
            ImGui::InvisibleButton("##thumb", ImVec2(thumbnailSize, thumbnailSize));
            bool doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

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

            if (doubleClicked && isDirectory)
                m_CurrentDirectory = path;

            ImGui::PopID();
            ImGui::NextColumn();
        }

        ImGui::Columns(1);
        ImGui::End();
    }

}
