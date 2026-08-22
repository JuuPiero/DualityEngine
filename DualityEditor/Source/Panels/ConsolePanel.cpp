#include "DualityEditor/Panels/ConsolePanel.h"

#include <string>

#include <imgui.h>

#include "DualityEngine/Core/Log.h"

namespace Duality {

    static ImVec4 LevelColor(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
            case LogLevel::Info:  return ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
            case LogLevel::Warn:  return ImVec4(0.95f, 0.8f, 0.25f, 1.0f);
            case LogLevel::Error: return ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
        }
        return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    static const char* LevelLabel(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return "Trace";
            case LogLevel::Info:  return "Info";
            case LogLevel::Warn:  return "Warn";
            case LogLevel::Error: return "Error";
        }
        return "";
    }

    void ConsolePanel::OnImGuiRender() {
        ImGui::Begin("Console");

        if (ImGui::Button("Clear"))
            Log::Clear();
        ImGui::SameLine();
        ImGui::Checkbox("Trace", &m_ShowTrace);
        ImGui::SameLine();
        ImGui::Checkbox("Info", &m_ShowInfo);
        ImGui::SameLine();
        ImGui::Checkbox("Warn", &m_ShowWarn);
        ImGui::SameLine();
        ImGui::Checkbox("Error", &m_ShowError);
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_AutoScroll);

        ImGui::Separator();

        ImGui::BeginChild("ConsoleScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        for (const LogEntry& entry : Log::GetEntries()) {
            bool visible = (entry.Level == LogLevel::Trace && m_ShowTrace) ||
                           (entry.Level == LogLevel::Info && m_ShowInfo) ||
                           (entry.Level == LogLevel::Warn && m_ShowWarn) ||
                           (entry.Level == LogLevel::Error && m_ShowError);
            if (!visible)
                continue;

            ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(entry.Level));
            ImGui::TextUnformatted(("[" + std::string(LevelLabel(entry.Level)) + "] " + entry.Message).c_str());
            ImGui::PopStyleColor();
        }
        if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();

        ImGui::End();
    }

}
