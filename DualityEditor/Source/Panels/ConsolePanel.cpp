#include "DualityEditor/Panels/ConsolePanel.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "DualityEditor/EditorContext.h"
#include "DualityEditor/EditorIcons.h"
#include "DualityEditor/EditorTheme.h"
#include "DualityEngine/Core/Log.h"

namespace Duality {

    namespace {
        bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle) {
            if (needle.empty())
                return true;
            auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
            return it != haystack.end();
        }

        ImVec4 LevelColor(LogLevel level) {
            switch (level) {
                case LogLevel::Trace: return ImVec4(0.62f, 0.62f, 0.62f, 1.0f);
                case LogLevel::Info:  return ImVec4(0.82f, 0.82f, 0.82f, 1.0f);
                case LogLevel::Warn:  return ImVec4(1.00f, 0.80f, 0.25f, 1.0f);
                case LogLevel::Error: return ImVec4(1.00f, 0.42f, 0.38f, 1.0f);
            }
            return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        }

        const char* LevelIcon(LogLevel level) {
            switch (level) {
                case LogLevel::Warn: return EditorIcons::Warning;
                case LogLevel::Error: return EditorIcons::Error;
                default: return EditorIcons::Info;
            }
        }

        // First line only, for the list row (Unity shows the first lines; the details pane shows
        // everything).
        std::string FirstLine(const std::string& message) {
            const size_t newline = message.find('\n');
            return newline == std::string::npos ? message : message.substr(0, newline) + "  ...";
        }

        // A toolbar toggle that looks pressed while on (Unity's Collapse / Clear on Play...).
        bool ToolbarToggle(const char* label, bool& value) {
            if (value)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(EditorColors::Selection()));
            const bool pressed = ImGui::Button(label);
            if (value)
                ImGui::PopStyleColor();
            if (pressed)
                value = !value;
            return pressed;
        }

        struct Row {
            const LogEntry* Entry;
            int Count;
        };
    }

    void ConsolePanel::OnImGuiRender(EditorContext& ctx) {
        if (!ctx.Session.ShowConsole)
            return;
        if (!ImGui::Begin("Console", &ctx.Session.ShowConsole)) {
            ImGui::End();
            return;
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ctx.Session.FocusedPanel = EditorPanel::Console;

        const std::deque<LogEntry> entries = Log::GetEntries();
        int infoCount = 0, warnCount = 0, errorCount = 0;
        for (const LogEntry& entry : entries) {
            if (entry.Level == LogLevel::Error) ++errorCount;
            else if (entry.Level == LogLevel::Warn) ++warnCount;
            else ++infoCount;
        }

        // ---- Toolbar (Unity: Clear | Collapse | Clear on Play | Error Pause ... search  i ! x)
        if (ImGui::Button("Clear")) {
            Log::Clear();
            m_SelectedSerial = 0;
        }
        ImGui::SameLine();
        ToolbarToggle("Collapse", m_Collapse);
        ImGui::SameLine();
        ToolbarToggle("Clear on Play", ctx.Session.ClearConsoleOnPlay);
        ImGui::SameLine();
        ToolbarToggle("Error Pause", ctx.Session.ErrorPause);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Pause Play mode as soon as an error is logged");

        const ImGuiStyle& style = ImGui::GetStyle();
        auto countLabel = [](const char* icon, int count) { return std::string(icon) + " " + std::to_string(std::min(count, 999)) + (count > 999 ? "+" : ""); };
        const std::string infoLabel = countLabel(EditorIcons::Info, infoCount);
        const std::string warnLabel = countLabel(EditorIcons::Warning, warnCount);
        const std::string errorLabel = countLabel(EditorIcons::Error, errorCount);
        const float countsWidth = ImGui::CalcTextSize(infoLabel.c_str()).x + ImGui::CalcTextSize(warnLabel.c_str()).x +
            ImGui::CalcTextSize(errorLabel.c_str()).x + style.FramePadding.x * 6.0f + style.ItemSpacing.x * 2.0f;
        const float searchWidth = std::clamp(ImGui::GetContentRegionAvail().x - countsWidth - style.ItemSpacing.x * 2.0f, 60.0f, 260.0f);

        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + style.ItemSpacing.x, ImGui::GetWindowContentRegionMax().x - countsWidth - searchWidth - style.ItemSpacing.x));
        ImGui::SetNextItemWidth(searchWidth);
        ImGui::InputTextWithHint("##ConsoleSearch", (std::string(EditorIcons::Search) + " Search").c_str(), m_SearchBuffer, sizeof(m_SearchBuffer));
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(LogLevel::Info));
        ToolbarToggle(infoLabel.c_str(), m_ShowInfo);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(LogLevel::Warn));
        ToolbarToggle(warnLabel.c_str(), m_ShowWarn);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(LogLevel::Error));
        ToolbarToggle(errorLabel.c_str(), m_ShowError);
        ImGui::PopStyleColor();
        ImGui::Separator();

        // ---- Rows (optionally collapsed: identical level+message shown once with a count)
        std::vector<Row> rows;
        std::map<std::pair<int, std::string>, size_t> collapsedIndex;
        for (const LogEntry& entry : entries) {
            const bool visible = (entry.Level == LogLevel::Error && m_ShowError) ||
                (entry.Level == LogLevel::Warn && m_ShowWarn) ||
                ((entry.Level == LogLevel::Info || entry.Level == LogLevel::Trace) && m_ShowInfo);
            if (!visible || !ContainsCaseInsensitive(entry.Message, m_SearchBuffer))
                continue;
            if (m_Collapse) {
                auto key = std::make_pair(static_cast<int>(entry.Level), entry.Message);
                auto found = collapsedIndex.find(key);
                if (found != collapsedIndex.end()) {
                    ++rows[found->second].Count;
                    continue;
                }
                collapsedIndex.emplace(std::move(key), rows.size());
            }
            rows.push_back({ &entry, 1 });
        }

        const LogEntry* selected = nullptr;
        for (const LogEntry& entry : entries) {
            if (entry.Serial == m_SelectedSerial)
                selected = &entry;
        }

        const float detailsHeight = selected ? std::clamp(m_DetailsHeight, 40.0f, ImGui::GetContentRegionAvail().y * 0.7f) : 0.0f;
        const float listHeight = std::max(20.0f, ImGui::GetContentRegionAvail().y - detailsHeight - (selected ? style.ItemSpacing.y + 4.0f : 0.0f));
        ImGui::BeginChild("ConsoleList", ImVec2(0.0f, listHeight), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        if (ImGui::BeginTable("ConsoleRows", 1, ImGuiTableFlags_RowBg)) {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(rows.size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const Row& row = rows[static_cast<size_t>(i)];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushID(i);
                    const bool isSelected = row.Entry->Serial == m_SelectedSerial;
                    ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(row.Entry->Level));
                    std::string label = std::string(LevelIcon(row.Entry->Level)) + "  " + FirstLine(row.Entry->Message);
                    if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                        m_SelectedSerial = row.Entry->Serial;
                    ImGui::PopStyleColor();
                    if (ImGui::BeginPopupContextItem("ConsoleRowMenu")) {
                        if (ImGui::MenuItem("Copy"))
                            ImGui::SetClipboardText(row.Entry->Message.c_str());
                        ImGui::EndPopup();
                    }
                    if (m_Collapse && row.Count > 1) {
                        const std::string badge = std::to_string(row.Count);
                        const float badgeWidth = ImGui::CalcTextSize(badge.c_str()).x + 12.0f;
                        ImGui::SameLine(ImGui::GetContentRegionMax().x - badgeWidth);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.30f, 0.30f, 1.0f));
                        ImGui::SmallButton(badge.c_str());
                        ImGui::PopStyleColor();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f)
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();

        // ---- Details pane: the selected message in full (selectable / copyable).
        if (selected) {
            ImGui::InvisibleButton("##ConsoleSplitter", ImVec2(-1.0f, 4.0f));
            if (ImGui::IsItemActive())
                m_DetailsHeight = std::max(40.0f, m_DetailsHeight - ImGui::GetIO().MouseDelta.y);
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            ImGui::GetWindowDrawList()->AddLine(ImGui::GetItemRectMin(), ImVec2(ImGui::GetItemRectMax().x, ImGui::GetItemRectMin().y),
                ImGui::GetColorU32(ImGuiCol_Separator));
            std::string text = selected->Message;
            ImGui::InputTextMultiline("##ConsoleDetails", text.data(), text.size() + 1, ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_ReadOnly);
        }

        ImGui::End();
    }

}
