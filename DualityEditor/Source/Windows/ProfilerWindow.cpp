#include "DualityEditor/Windows/ProfilerWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <imgui.h>

#include "DualityEditor/EditorAutomation.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEngine/Core/Profiler.h"

namespace Duality {

    namespace {
        ProfilerWindow* s_Instance = nullptr;

        // Unity's CPU Usage module categories.
        enum Category { Rendering, Scripts, Physics, Animation, UI, Editor, Others, CategoryCount };
        const char* kCategoryNames[CategoryCount] = { "Rendering", "Scripts", "Physics", "Animation", "UI", "Editor", "Others" };
        const ImU32 kCategoryColors[CategoryCount] = {
            IM_COL32(80, 170, 70, 255), IM_COL32(70, 130, 220, 255), IM_COL32(240, 160, 50, 255), IM_COL32(200, 90, 200, 255),
            IM_COL32(60, 200, 200, 255), IM_COL32(150, 150, 150, 255), IM_COL32(110, 110, 80, 255) };

        Category CategoryOf(const char* name) {
            const std::string n(name);
            if (n.rfind("Render", 0) == 0) return Rendering;
            if (n.rfind("Scripts", 0) == 0) return Scripts;
            if (n.rfind("Physics", 0) == 0) return Physics;
            if (n.rfind("Animation", 0) == 0 || n.rfind("Scene.RuntimeSystems", 0) == 0) return Animation;
            if (n.rfind("UI", 0) == 0) return UI;
            if (n.rfind("Editor", 0) == 0) return Editor;
            return Others;
        }

        // Self time per category (children subtracted, so nesting is never counted twice);
        // frame time no marker covers goes to Others.
        void CategoryTimes(const Profiler::Frame& frame, double out[CategoryCount]) {
            for (int i = 0; i < CategoryCount; ++i)
                out[i] = 0.0;
            std::vector<double> childTime(frame.Samples.size(), 0.0);
            double rootTime = 0.0;
            for (size_t i = 0; i < frame.Samples.size(); ++i) {
                const Profiler::Sample& sample = frame.Samples[i];
                if (sample.Parent >= 0)
                    childTime[static_cast<size_t>(sample.Parent)] += sample.DurationMs;
                else
                    rootTime += sample.DurationMs;
            }
            for (size_t i = 0; i < frame.Samples.size(); ++i) {
                const Profiler::Sample& sample = frame.Samples[i];
                out[CategoryOf(sample.Name)] += std::max(0.0, sample.DurationMs - childTime[i]);
            }
            out[Others] += std::max(0.0, frame.DurationMs - rootTime);
        }

        struct HierarchyNode {
            std::string Name;
            double Total = 0.0;
            double Self = 0.0;
            int Calls = 0;
            std::map<std::string, int> ChildIndex;
            std::vector<int> Children;
        };

        // Merges same-named samples under the same parent path (Unity's Hierarchy view).
        std::vector<HierarchyNode> BuildHierarchy(const Profiler::Frame& frame) {
            std::vector<HierarchyNode> nodes(1);
            nodes[0].Name = "Frame";
            nodes[0].Total = frame.DurationMs;
            nodes[0].Calls = 1;
            std::vector<int> nodeOfSample(frame.Samples.size(), 0);
            for (size_t i = 0; i < frame.Samples.size(); ++i) {
                const Profiler::Sample& sample = frame.Samples[i];
                const int parentNode = sample.Parent >= 0 ? nodeOfSample[static_cast<size_t>(sample.Parent)] : 0;
                auto found = nodes[static_cast<size_t>(parentNode)].ChildIndex.find(sample.Name);
                int node;
                if (found == nodes[static_cast<size_t>(parentNode)].ChildIndex.end()) {
                    node = static_cast<int>(nodes.size());
                    HierarchyNode created;
                    created.Name = sample.Name;
                    nodes.push_back(created);
                    nodes[static_cast<size_t>(parentNode)].ChildIndex[sample.Name] = node;
                    nodes[static_cast<size_t>(parentNode)].Children.push_back(node);
                } else {
                    node = found->second;
                }
                nodes[static_cast<size_t>(node)].Total += sample.DurationMs;
                nodes[static_cast<size_t>(node)].Calls += 1;
                nodeOfSample[i] = node;
            }
            for (HierarchyNode& node : nodes) {
                double children = 0.0;
                for (int child : node.Children)
                    children += nodes[static_cast<size_t>(child)].Total;
                node.Self = std::max(0.0, node.Total - children);
            }
            for (HierarchyNode& node : nodes)
                std::sort(node.Children.begin(), node.Children.end(), [&](int a, int b) { return nodes[static_cast<size_t>(a)].Total > nodes[static_cast<size_t>(b)].Total; });
            return nodes;
        }

        void DrawHierarchyRow(const std::vector<HierarchyNode>& nodes, int index, double frameMs) {
            const HierarchyNode& node = nodes[static_cast<size_t>(index)];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_DefaultOpen;
            if (node.Children.empty())
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            ImGui::PushID(index);
            const bool open = ImGui::TreeNodeEx(node.Name.c_str(), flags);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.1f%%", frameMs > 0.0 ? node.Total / frameMs * 100.0 : 0.0);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.1f%%", frameMs > 0.0 ? node.Self / frameMs * 100.0 : 0.0);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", node.Calls);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.3f", node.Total);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%.3f", node.Self);
            if (open && !node.Children.empty()) {
                for (int child : node.Children)
                    DrawHierarchyRow(nodes, child, frameMs);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    ProfilerWindow::ProfilerWindow() { s_Instance = this; }

    bool& ProfilerWindow::IsOpen() {
        static bool open = false;
        return open;
    }

    ProfilerWindow* ProfilerWindow::Instance() { return s_Instance; }

    void ProfilerWindow::OnImGuiRender(EditorContext& ctx) {
        bool& open = IsOpen();
        // Unity records while the Profiler window is open (Record toggle).
        if (open && !m_WasOpen && m_RecordOnOpen)
            Profiler::SetRecording(true);
        if (!open && m_WasOpen)
            Profiler::SetRecording(false);
        m_WasOpen = open;
        if (!open)
            return;

        ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Profiler", &open)) {
            ImGui::End();
            return;
        }
        const auto& frames = Profiler::Frames();

        // ---- Toolbar --------------------------------------------------------------------------
        bool recording = Profiler::IsRecording();
        if (ImGui::Checkbox("Record", &recording)) {
            Profiler::SetRecording(recording);
            m_RecordOnOpen = recording;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            Profiler::Clear();
            m_Selected = -1;
        }
        ImGui::SameLine();
        const int frameCount = static_cast<int>(frames.size());
        int selected = m_Selected < 0 ? frameCount - 1 : std::min(m_Selected, frameCount - 1);
        if (ImGui::ArrowButton("##prev", ImGuiDir_Left) && frameCount > 0)
            m_Selected = std::max(0, selected - 1);
        ImGui::SameLine();
        if (ImGui::ArrowButton("##next", ImGuiDir_Right) && frameCount > 0)
            m_Selected = std::min(frameCount - 1, selected + 1);
        ImGui::SameLine();
        if (ImGui::Button("Current"))
            m_Selected = -1;
        selected = m_Selected < 0 ? frameCount - 1 : std::min(m_Selected, frameCount - 1);
        ImGui::SameLine();
        if (selected >= 0)
            ImGui::Text("Frame: %llu  (%.2f ms)", static_cast<unsigned long long>(frames[static_cast<size_t>(selected)].Index), frames[static_cast<size_t>(selected)].DurationMs);
        else
            ImGui::TextDisabled("No frames recorded");
        ImGui::SameLine();
        ImGui::TextDisabled(ctx.IsPlaying ? "| Play mode" : "| Edit mode (press Play for game timings)");

        // ---- CPU Usage chart ------------------------------------------------------------------
        const float chartHeight = 150.0f;
        ImGui::SeparatorText("CPU Usage");
        double selectedCategories[CategoryCount] = {};
        if (selected >= 0)
            CategoryTimes(frames[static_cast<size_t>(selected)], selectedCategories);
        for (int c = 0; c < CategoryCount; ++c) {
            ImGui::ColorButton(kCategoryNames[c], ImGui::ColorConvertU32ToFloat4(kCategoryColors[c]), ImGuiColorEditFlags_NoTooltip, ImVec2(10.0f, 10.0f));
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::Text("%s %.2fms", kCategoryNames[c], selectedCategories[c]);
            ImGui::SameLine(0.0f, 14.0f);
        }
        ImGui::NewLine();
        {
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float width = std::max(100.0f, ImGui::GetContentRegionAvail().x);
            ImGui::InvisibleButton("##CpuChart", ImVec2(width, chartHeight));
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + chartHeight), IM_COL32(28, 28, 28, 255));
            double maxMs = 1000.0 / 60.0;
            for (const auto& frame : frames)
                maxMs = std::max(maxMs, frame.DurationMs);
            maxMs = std::min(maxMs * 1.1, 100.0);
            const float barWidth = width / static_cast<float>(Profiler::MaxFrames);
            for (int i = 0; i < frameCount; ++i) {
                const float x = origin.x + width - (frameCount - i) * barWidth;
                double times[CategoryCount];
                CategoryTimes(frames[static_cast<size_t>(i)], times);
                float y = origin.y + chartHeight;
                for (int c = 0; c < CategoryCount; ++c) {
                    const float h = static_cast<float>(times[c] / maxMs) * chartHeight;
                    if (h <= 0.0f)
                        continue;
                    draw->AddRectFilled(ImVec2(x, std::max(origin.y, y - h)), ImVec2(x + std::max(1.0f, barWidth), y), kCategoryColors[c]);
                    y -= h;
                }
            }
            // 60 / 30 fps guides.
            for (double guide : { 1000.0 / 60.0, 1000.0 / 30.0 }) {
                if (guide > maxMs)
                    continue;
                const float y = origin.y + chartHeight - static_cast<float>(guide / maxMs) * chartHeight;
                draw->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + width, y), IM_COL32(255, 255, 255, 60));
                char text[32];
                std::snprintf(text, sizeof(text), "%.1fms (%dFPS)", guide, static_cast<int>(std::lround(1000.0 / guide)));
                draw->AddText(ImVec2(origin.x + 4.0f, y - 14.0f), IM_COL32(200, 200, 200, 160), text);
            }
            if (selected >= 0) {
                const float x = origin.x + width - (frameCount - selected) * barWidth + barWidth * 0.5f;
                draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + chartHeight), IM_COL32(255, 255, 255, 220), 1.5f);
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left) && frameCount > 0) {
                const float mx = ImGui::GetIO().MousePos.x;
                const int index = frameCount - 1 - static_cast<int>((origin.x + width - mx) / barWidth);
                m_Selected = std::clamp(index, 0, frameCount - 1);
            }
        }

        // ---- Rendering / Memory counters -------------------------------------------------------
        ImGui::SeparatorText("Rendering / Memory");
        std::vector<std::string> counterNames;
        if (!frames.empty())
            for (const auto& counter : frames.back().Counters)
                counterNames.push_back(counter.Name);
        if (!counterNames.empty()) {
            m_ChartCounter = std::clamp(m_ChartCounter, 0, static_cast<int>(counterNames.size()) - 1);
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::BeginCombo("##Counter", counterNames[static_cast<size_t>(m_ChartCounter)].c_str())) {
                for (int i = 0; i < static_cast<int>(counterNames.size()); ++i)
                    if (ImGui::Selectable(counterNames[static_cast<size_t>(i)].c_str(), i == m_ChartCounter))
                        m_ChartCounter = i;
                ImGui::EndCombo();
            }
            std::vector<float> values;
            values.reserve(frames.size());
            float maxValue = 1.0f;
            for (const auto& frame : frames) {
                values.push_back(static_cast<float>(frame.CounterValue(counterNames[static_cast<size_t>(m_ChartCounter)])));
                maxValue = std::max(maxValue, values.back());
            }
            ImGui::SameLine();
            ImGui::PlotLines("##CounterPlot", values.data(), static_cast<int>(values.size()), 0, nullptr, 0.0f, maxValue * 1.1f,
                ImVec2(std::max(100.0f, ImGui::GetContentRegionAvail().x), 50.0f));
            if (selected >= 0) {
                const auto& frame = frames[static_cast<size_t>(selected)];
                int column = 0;
                for (const auto& counter : frame.Counters) {
                    if (column++ % 4 != 0)
                        ImGui::SameLine(0.0f, 24.0f);
                    ImGui::Text("%s: %.0f", counter.Name.c_str(), counter.Value);
                }
            }
        }

        // ---- Hierarchy -----------------------------------------------------------------------
        ImGui::SeparatorText("Hierarchy");
        if (selected >= 0) {
            const Profiler::Frame& frame = frames[static_cast<size_t>(selected)];
            const std::vector<HierarchyNode> nodes = BuildHierarchy(frame);
            if (ImGui::BeginTable("##ProfilerHierarchy", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                    ImVec2(0.0f, std::max(120.0f, ImGui::GetContentRegionAvail().y)))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Overview", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Self", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn("Time ms", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Self ms", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableHeadersRow();
                DrawHierarchyRow(nodes, 0, frame.DurationMs);
                ImGui::EndTable();
            }
        } else {
            ImGui::TextDisabled("Enable Record (and press Play) to capture frames.");
        }
        ImGui::End();
    }

    // ---- Automation ----------------------------------------------------------------------

    namespace {
        bool ProfilerAutomation(EditorContext& ctx, const std::vector<std::string>& args, AutomationApi& api) {
            (void)ctx;
            const std::string& command = args[0];
            if (command.rfind("profiler-", 0) != 0 && command.rfind("expect-profiler", 0) != 0)
                return false;
            auto arg = [&](size_t i) { return i < args.size() ? args[i] : std::string(); };
            if (command == "profiler-open") {
                ProfilerWindow::IsOpen() = true;
                api.Wait(2);
            } else if (command == "profiler-close") {
                ProfilerWindow::IsOpen() = false;
            } else if (command == "profiler-record") {
                Profiler::SetRecording(arg(1) == "1");
            } else if (command == "profiler-select") {
                if (ProfilerWindow::Instance())
                    ProfilerWindow::Instance()->SelectFrame(std::atoi(arg(1).c_str()));
            } else if (command == "expect-profiler-frames-min") {
                const int frames = static_cast<int>(Profiler::Frames().size());
                api.Expect(frames >= std::atoi(arg(1).c_str()), "profiler frames >= " + arg(1) + " (actual " + std::to_string(frames) + ")");
            } else if (command == "expect-profiler-sample") {
                // expect-profiler-sample <name> : some recorded frame contains that marker
                bool found = false;
                for (const auto& frame : Profiler::Frames())
                    for (const auto& sample : frame.Samples)
                        if (arg(1) == sample.Name)
                            found = true;
                api.Expect(found, "profiler recorded sample '" + arg(1) + "'");
            } else if (command == "expect-profiler-counter") {
                const bool found = !Profiler::Frames().empty() && Profiler::Frames().back().CounterValue(arg(1), -1.0) >= 0.0;
                api.Expect(found, "profiler counter '" + arg(1) + "'");
            } else {
                api.Expect(false, "unknown profiler command '" + command + "'");
            }
            return true;
        }

        const bool s_AutomationRegistered = (EditorAutomation::RegisterExtension(&ProfilerAutomation), true);
    }

}
