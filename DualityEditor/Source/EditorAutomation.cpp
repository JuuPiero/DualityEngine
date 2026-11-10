#include "DualityEditor/EditorAutomation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "DualityEditor/EditorCommands.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEditor/Window.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Reflection/FieldSerialization.h"
#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Scene/Components.h"

namespace Duality {

    namespace {
        std::string Trim(const std::string& text) {
            size_t begin = text.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos)
                return {};
            size_t end = text.find_last_not_of(" \t\r\n");
            return text.substr(begin, end - begin + 1);
        }

        std::string Lower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        // Splits on spaces, keeping "quoted strings" together (names with spaces).
        std::vector<std::string> Tokenize(const std::string& line) {
            std::vector<std::string> tokens;
            std::string current;
            bool quoted = false;
            for (char c : line) {
                if (c == '"') {
                    quoted = !quoted;
                    continue;
                }
                if (!quoted && (c == ' ' || c == '\t')) {
                    if (!current.empty()) {
                        tokens.push_back(current);
                        current.clear();
                    }
                    continue;
                }
                current.push_back(c);
            }
            if (!current.empty())
                tokens.push_back(current);
            return tokens;
        }

        // Everything after the first `count` tokens of `line` (raw, e.g. a JSON value).
        std::string RestAfter(const std::string& line, int count) {
            size_t position = 0;
            bool quoted = false;
            int seen = 0;
            while (position < line.size() && seen < count) {
                while (position < line.size() && (line[position] == ' ' || line[position] == '\t'))
                    ++position;
                while (position < line.size() && (quoted || (line[position] != ' ' && line[position] != '\t'))) {
                    if (line[position] == '"')
                        quoted = !quoted;
                    ++position;
                }
                ++seen;
            }
            return Trim(line.substr(std::min(position, line.size())));
        }

        std::string PathOf(Entity entity) {
            std::string path = entity.GetComponent<NameComponent>().Name;
            for (Entity parent = entity.GetComponent<HierarchyComponent>().Parent; parent; parent = parent.GetComponent<HierarchyComponent>().Parent)
                path = parent.GetComponent<NameComponent>().Name + "/" + path;
            return path;
        }

        // "Name" = first entity with that name; "Parent/Child" = by hierarchy path (suffix match,
        // so "Copy/Child" finds "Root/Copy/Child"); "#2:Name" = the second entity with that name.
        Entity FindByName(Scene& scene, const std::string& query) {
            std::string name = query;
            int occurrence = 1;
            if (name.size() > 2 && name[0] == '#') {
                const size_t colon = name.find(':');
                if (colon != std::string::npos) {
                    occurrence = std::max(1, std::atoi(name.substr(1, colon - 1).c_str()));
                    name = name.substr(colon + 1);
                }
            }
            const bool isPath = name.find('/') != std::string::npos;
            for (Entity entity : scene.GetHierarchyTraversalOrder()) {
                bool match = false;
                if (isPath) {
                    const std::string path = PathOf(entity);
                    match = path == name || (path.size() > name.size() && path.compare(path.size() - name.size(), name.size(), name) == 0 &&
                        path[path.size() - name.size() - 1] == '/');
                } else {
                    match = entity.GetComponent<NameComponent>().Name == name;
                }
                if (match && --occurrence == 0)
                    return entity;
            }
            return {};
        }

        int CountByName(Scene& scene, const std::string& name) {
            int count = 0;
            for (Entity entity : scene.GetHierarchyTraversalOrder())
                count += entity.GetComponent<NameComponent>().Name == name ? 1 : 0;
            return count;
        }

        const FieldHandle* FindField(ComponentTypeInfo& type, const std::string& name) {
            for (const FieldHandle& field : type.Fields) {
                if (field.Name == name)
                    return &field;
            }
            return nullptr;
        }

        ImGuiKey KeyFromName(const std::string& name) {
            const std::string key = Lower(name);
            if (key.size() == 1 && key[0] >= 'a' && key[0] <= 'z')
                return static_cast<ImGuiKey>(ImGuiKey_A + (key[0] - 'a'));
            if (key.size() == 1 && key[0] >= '0' && key[0] <= '9')
                return static_cast<ImGuiKey>(ImGuiKey_0 + (key[0] - '0'));
            if (key.size() >= 2 && key[0] == 'f' && std::isdigit(static_cast<unsigned char>(key[1]))) {
                const int index = std::atoi(key.c_str() + 1);
                if (index >= 1 && index <= 12)
                    return static_cast<ImGuiKey>(ImGuiKey_F1 + index - 1);
            }
            if (key == "delete" || key == "del") return ImGuiKey_Delete;
            if (key == "backspace") return ImGuiKey_Backspace;
            if (key == "enter" || key == "return") return ImGuiKey_Enter;
            if (key == "escape" || key == "esc") return ImGuiKey_Escape;
            if (key == "tab") return ImGuiKey_Tab;
            if (key == "space") return ImGuiKey_Space;
            if (key == "up") return ImGuiKey_UpArrow;
            if (key == "down") return ImGuiKey_DownArrow;
            if (key == "left") return ImGuiKey_LeftArrow;
            if (key == "right") return ImGuiKey_RightArrow;
            return ImGuiKey_None;
        }

        int MouseButtonFromName(const std::string& name) {
            const std::string button = Lower(name);
            if (button == "right") return ImGuiMouseButton_Right;
            if (button == "middle") return ImGuiMouseButton_Middle;
            return ImGuiMouseButton_Left;
        }

        // Script coordinates are relative to the editor window's top-left corner; with
        // multi-viewports enabled ImGui works in desktop coordinates.
        ImVec2 ToScreen(float x, float y) {
            const ImVec2 origin = ImGui::GetMainViewport()->Pos;
            return ImVec2(origin.x + x, origin.y + y);
        }

        const char* PanelWindowName(const std::string& panel) {
            const std::string name = Lower(panel);
            if (name == "hierarchy") return "Hierarchy";
            if (name == "scene") return "Scene";
            if (name == "game") return "Game";
            if (name == "inspector") return "Inspector";
            if (name == "project") return "Project";
            if (name == "console") return "Console";
            return nullptr;
        }

        EditorPanel PanelFromName(const std::string& panel) {
            const std::string name = Lower(panel);
            if (name == "hierarchy") return EditorPanel::Hierarchy;
            if (name == "scene") return EditorPanel::Scene;
            if (name == "game") return EditorPanel::Game;
            if (name == "inspector") return EditorPanel::Inspector;
            if (name == "project") return EditorPanel::Project;
            if (name == "console") return EditorPanel::Console;
            return EditorPanel::Other;
        }
    }

    bool EditorAutomation::Load(const std::string& scriptPath) {
        std::ifstream file(scriptPath);
        if (!file.is_open()) {
            Log::Error("Editor automation: could not open '" + scriptPath + "'");
            return false;
        }
        std::string line;
        while (std::getline(file, line)) {
            line = Trim(line);
            if (line.empty() || line[0] == '#')
                continue;
            m_Lines.push_back(line);
        }
        m_ScriptDirectory = std::filesystem::path(scriptPath).parent_path().string();
        m_Active = true;
        // Let the first frames lay out the dock space before any command runs.
        m_WaitFrames = 3;
        return true;
    }

    void EditorAutomation::Expect(bool condition, const std::string& what) {
        if (condition)
            ++m_Passes;
        else
            ++m_Failures;
        const std::string line = std::string(condition ? "[editor-test] PASS " : "[editor-test] FAIL ") + what;
        std::printf("%s\n", line.c_str());
        std::fflush(stdout);
    }

    void EditorAutomation::Update(EditorContext& ctx, Window& window) {
        if (!m_Active)
            return;
        ImGuiIO& io = ImGui::GetIO();

        // Release what the previous command pressed (one frame later, so ImGui sees a press).
        for (int key : m_KeysToRelease)
            io.AddKeyEvent(static_cast<ImGuiKey>(key), false);
        m_KeysToRelease.clear();
        if (m_MouseButtonToRelease >= 0) {
            io.AddMouseButtonEvent(m_MouseButtonToRelease, false);
            m_MouseButtonToRelease = -1;
            m_WaitFrames = std::max(m_WaitFrames, 1);
        }

        if (m_Dragging) {
            Drag& drag = m_Drag;
            ++drag.Step;
            if (drag.Step <= 0) {
                if (drag.Step == 0)
                    io.AddMouseButtonEvent(drag.Button, true);
                return;
            }
            const float t = std::min(1.0f, static_cast<float>(drag.Step) / static_cast<float>(drag.Steps));
            const ImVec2 position = ToScreen(drag.FromX + (drag.ToX - drag.FromX) * t, drag.FromY + (drag.ToY - drag.FromY) * t);
            io.AddMousePosEvent(position.x, position.y);
            if (drag.Step > drag.Steps) {
                io.AddMouseButtonEvent(drag.Button, false);
                m_Dragging = false;
                m_WaitFrames = 2;
            }
            return;
        }

        if (m_WaitFrames > 0) {
            --m_WaitFrames;
            return;
        }
        if (m_Lines.empty()) {
            if (!m_Quit) {
                std::printf("[editor-test] script finished: %d passed, %d failed\n", m_Passes, m_Failures);
                std::fflush(stdout);
                m_Quit = true;
            }
            return;
        }
        const std::string line = m_Lines.front();
        m_Lines.pop_front();
        std::printf("[editor-test] > %s\n", line.c_str());
        std::fflush(stdout);
        Execute(ctx, window, line);
    }

    void EditorAutomation::Execute(EditorContext& ctx, Window& window, const std::string& line) {
        const std::vector<std::string> args = Tokenize(line);
        if (args.empty())
            return;
        const std::string command = Lower(args[0]);
        ImGuiIO& io = ImGui::GetIO();
        auto arg = [&](size_t index) { return index < args.size() ? args[index] : std::string(); };
        auto argf = [&](size_t index) { return static_cast<float>(std::atof(arg(index).c_str())); };

        if (command == "wait") {
            m_WaitFrames = std::max(1, std::atoi(arg(1).c_str()));
        } else if (command == "log") {
            Log::Info("[editor-test] " + RestAfter(line, 1));
        } else if (command == "screenshot") {
            window.RequestScreenshot(arg(1));
            m_WaitFrames = 1;
        } else if (command == "quit") {
            std::printf("[editor-test] script finished: %d passed, %d failed\n", m_Passes, m_Failures);
            std::fflush(stdout);
            m_Quit = true;
            m_Lines.clear();
        } else if (command == "select" || command == "select-add") {
            Entity entity = FindByName(ctx.SceneRef, arg(1));
            Expect(static_cast<bool>(entity), "select finds '" + arg(1) + "'");
            if (entity) {
                if (command == "select")
                    EditorCommands::SelectOnly(ctx, entity);
                else if (!EditorCommands::IsSelected(ctx, entity))
                    EditorCommands::ToggleSelected(ctx, entity);
                ctx.Session.RequestRevealSelection = true;
            }
        } else if (command == "select-none") {
            EditorCommands::ClearSelection(ctx);
        } else if (command == "focus") {
            if (const char* window = PanelWindowName(arg(1)))
                ImGui::SetWindowFocus(window);
            ctx.Session.FocusedPanel = PanelFromName(arg(1));
        } else if (command == "window") {
            const std::string name = Lower(arg(1));
            const bool on = Lower(arg(2)) != "off";
            if (name == "scene") ctx.Session.ShowScene = on;
            else if (name == "game") ctx.Session.ShowGame = on;
            else if (name == "hierarchy") ctx.Session.ShowHierarchy = on;
            else if (name == "inspector") ctx.Session.ShowInspector = on;
            else if (name == "project") ctx.Session.ShowProject = on;
            else if (name == "console") ctx.Session.ShowConsole = on;
        } else if (command == "tool") {
            const std::string tool = Lower(arg(1));
            if (tool == "view") ctx.ActiveGizmoMode = GizmoMode::View;
            else if (tool == "move") ctx.ActiveGizmoMode = GizmoMode::Translate;
            else if (tool == "rotate") ctx.ActiveGizmoMode = GizmoMode::Rotate;
            else if (tool == "scale") ctx.ActiveGizmoMode = GizmoMode::Scale;
            else if (tool == "rect") ctx.ActiveGizmoMode = GizmoMode::Rect;
        } else if (command == "cmd") {
            const std::string name = Lower(arg(1));
            using namespace EditorCommands;
            if (name == "undo") UndoScene(ctx);
            else if (name == "redo") RedoScene(ctx);
            else if (name == "duplicate") DuplicateSelection(ctx);
            else if (name == "delete") DeleteSelection(ctx);
            else if (name == "copy") CopySelection(ctx);
            else if (name == "cut") CutSelection(ctx);
            else if (name == "paste") Paste(ctx, false);
            else if (name == "paste-child") Paste(ctx, true);
            else if (name == "create-empty") CreateEmpty(ctx);
            else if (name == "create-empty-child") CreateEmpty(ctx, ctx.Selected);
            else if (name == "create-parent") CreateEmptyParent(ctx);
            else if (name == "toggle-active") ToggleActiveSelection(ctx);
            else if (name == "create-prefab") CreatePrefab(ctx, ctx.Selected);
            else if (name == "select-all") SelectAll(ctx);
            else if (name == "play") StartPlay(ctx);
            else if (name == "stop") StopPlay(ctx);
            else if (name == "pause") TogglePause(ctx);
            else if (name == "step") Step(ctx);
            else if (name == "save") SaveScene(ctx);
            else if (name == "new-scene") NewSceneWithPrompt(ctx);
            else if (name == "frame") ctx.Session.RequestFrameSelected = true;
            else if (name == "rename") ctx.Session.RequestRename = true;
            else if (name == "reset-layout") ctx.Session.RequestResetLayout = true;
            else Expect(false, "unknown cmd '" + name + "'");
            m_WaitFrames = 1;
        } else if (command == "open-scene") {
            OpenScene(ctx, arg(1));
        } else if (command == "set" || command == "expect-field") {
            // set <entity> <Component> <Field> <json>
            Entity entity = FindByName(ctx.SceneRef, arg(1));
            ComponentTypeInfo* type = TypeRegistry::Find(arg(2));
            const FieldHandle* field = type ? FindField(*type, arg(3)) : nullptr;
            const std::string valueText = RestAfter(line, 4);
            if (!entity || !type || !field || !type->Has(entity)) {
                Expect(false, command + " " + arg(1) + "." + arg(2) + "." + arg(3) + " (not found)");
                return;
            }
            void* component = type->GetPtr(entity);
            const nlohmann::json expected = nlohmann::json::parse(valueText, nullptr, false);
            if (command == "set") {
                field->Set(component, JsonToFieldValue(expected, field->Get(component)));
                MarkSceneDirty(ctx, "Set Field");
            } else {
                const nlohmann::json actual = FieldValueToJson(field->Get(component));
                bool equal = actual == expected;
                // Float tolerance for numbers / numeric arrays.
                if (!equal && actual.is_array() && expected.is_array() && actual.size() == expected.size()) {
                    equal = true;
                    for (size_t i = 0; i < actual.size() && equal; ++i)
                        equal = actual[i].is_number() && expected[i].is_number() &&
                            std::abs(actual[i].get<double>() - expected[i].get<double>()) < 1e-3;
                } else if (!equal && actual.is_number() && expected.is_number()) {
                    equal = std::abs(actual.get<double>() - expected.get<double>()) < 1e-3;
                }
                Expect(equal, arg(1) + "." + arg(2) + "." + arg(3) + " == " + valueText + " (actual " + actual.dump() + ")");
            }
        } else if (command == "set-ref") {
            // set-ref <entity> <Component> <Field> <target name | none>
            Entity entity = FindByName(ctx.SceneRef, arg(1));
            ComponentTypeInfo* type = TypeRegistry::Find(arg(2));
            const FieldHandle* field = type ? FindField(*type, arg(3)) : nullptr;
            Entity target = FindByName(ctx.SceneRef, arg(4));
            if (!entity || !type || !field || !type->Has(entity)) {
                Expect(false, "set-ref " + arg(1) + " (not found)");
                return;
            }
            field->Set(type->GetPtr(entity), FieldValue(EntityRef{ target ? static_cast<uint32_t>(target.Handle()) : EntityRef::Invalid }));
            MarkSceneDirty(ctx, "Set Reference");
        } else if (command == "add-component") {
            Entity entity = FindByName(ctx.SceneRef, arg(1));
            ComponentTypeInfo* type = TypeRegistry::Find(arg(2));
            if (!entity || !type) {
                Expect(false, "add-component " + arg(1) + " " + arg(2) + " (not found)");
                return;
            }
            type->AddDefault(entity);
            MarkSceneDirty(ctx, ("Add " + type->DisplayName).c_str());
        } else if (command == "rename-selected") {
            if (ctx.Selected) {
                ctx.Selected.GetComponent<NameComponent>().Name = RestAfter(line, 1);
                MarkSceneDirty(ctx, "Rename");
            }
        } else if (command == "expect-ref") {
            // expect-ref <entity> <Component> <Field> <target name | none>
            Entity entity = FindByName(ctx.SceneRef, arg(1));
            ComponentTypeInfo* type = TypeRegistry::Find(arg(2));
            const FieldHandle* field = type ? FindField(*type, arg(3)) : nullptr;
            if (!entity || !type || !field || !type->Has(entity)) {
                Expect(false, "expect-ref " + arg(1) + " (not found)");
                return;
            }
            const FieldValue value = field->Get(type->GetPtr(entity));
            std::string targetName = "none";
            if (const EntityRef* reference = std::get_if<EntityRef>(&value)) {
                const entt::entity handle = static_cast<entt::entity>(reference->Handle);
                if (reference->Handle != EntityRef::Invalid && ctx.SceneRef.Registry().valid(handle))
                    targetName = Entity(handle, &ctx.SceneRef).GetComponent<NameComponent>().Name;
            }
            Expect(targetName == arg(4), "expect-ref " + arg(1) + "." + arg(3) + " -> " + arg(4) + " (actual " + targetName + ")");
        } else if (command == "expect-count") {
            const int expected = std::atoi(arg(1).c_str());
            const int actual = static_cast<int>(ctx.SceneRef.GetHierarchyTraversalOrder().size());
            Expect(actual == expected, "entity count == " + arg(1) + " (actual " + std::to_string(actual) + ")");
        } else if (command == "expect-name-count") {
            const int expected = std::atoi(arg(2).c_str());
            const int actual = CountByName(ctx.SceneRef, arg(1));
            Expect(actual == expected, "count of '" + arg(1) + "' == " + arg(2) + " (actual " + std::to_string(actual) + ")");
        } else if (command == "expect-selected") {
            const int expected = std::atoi(arg(1).c_str());
            const int actual = static_cast<int>(ctx.SelectedEntities.size());
            Expect(actual == expected, "selection size == " + arg(1) + " (actual " + std::to_string(actual) + ")");
        } else if (command == "print-field") {
            Entity entity = FindByName(ctx.SceneRef, arg(1));
            ComponentTypeInfo* type = TypeRegistry::Find(arg(2));
            const FieldHandle* field = type ? FindField(*type, arg(3)) : nullptr;
            if (entity && type && field && type->Has(entity))
                std::printf("[editor-test] %s.%s.%s = %s\n", arg(1).c_str(), arg(2).c_str(), arg(3).c_str(), FieldValueToJson(field->Get(type->GetPtr(entity))).dump().c_str());
        } else if (command == "expect-selected-min") {
            const int expected = std::atoi(arg(1).c_str());
            const int actual = static_cast<int>(ctx.SelectedEntities.size());
            Expect(actual >= expected, "selection size >= " + arg(1) + " (actual " + std::to_string(actual) + ")");
        } else if (command == "keydown" || command == "keyup") {
            const std::string name = Lower(arg(1));
            const ImGuiKey key = name == "ctrl" ? ImGuiMod_Ctrl : name == "shift" ? ImGuiMod_Shift : name == "alt" ? ImGuiMod_Alt : KeyFromName(name);
            io.AddKeyEvent(key, command == "keydown");
        } else if (command == "expect-primary") {
            const std::string actual = ctx.Selected ? ctx.Selected.GetComponent<NameComponent>().Name : std::string("none");
            Expect(actual == arg(1), "primary selection == '" + arg(1) + "' (actual '" + actual + "')");
        } else if (command == "expect-exists" || command == "expect-missing") {
            const bool exists = static_cast<bool>(FindByName(ctx.SceneRef, arg(1)));
            Expect(command == "expect-exists" ? exists : !exists, command + " '" + arg(1) + "'");
        } else if (command == "expect-parent") {
            Entity entity = FindByName(ctx.SceneRef, arg(1));
            Entity parent = entity ? entity.GetComponent<HierarchyComponent>().Parent : Entity{};
            const std::string actual = parent ? parent.GetComponent<NameComponent>().Name : std::string("none");
            Expect(entity && actual == arg(2), "parent of '" + arg(1) + "' == '" + arg(2) + "' (actual '" + actual + "')");
        } else if (command == "expect-dirty") {
            Expect(ctx.SceneDirty == (arg(1) == "1"), "scene dirty == " + arg(1));
        } else if (command == "expect-playing") {
            Expect(ctx.IsPlaying == (arg(1) == "1"), "playing == " + arg(1));
        } else if (command == "expect-paused") {
            Expect(ctx.Session.IsPaused == (arg(1) == "1"), "paused == " + arg(1));
        } else if (command == "expect-undo-label") {
            const std::string expected = RestAfter(line, 1);
            Expect(ctx.History.UndoLabel() == expected, "undo label == '" + expected + "' (actual '" + ctx.History.UndoLabel() + "')");
        } else if (command == "expect-undo-steps") {
            const int expected = std::atoi(arg(1).c_str());
            const int actual = static_cast<int>(ctx.History.CurrentIndex());
            Expect(actual == expected, "undo steps == " + arg(1) + " (actual " + std::to_string(actual) + ")");
        } else if (command == "expect-tool") {
            const char* names[] = { "move", "rotate", "scale", "view", "rect" };
            const std::string actual = names[static_cast<int>(ctx.ActiveGizmoMode)];
            Expect(actual == Lower(arg(1)), "tool == " + arg(1) + " (actual " + actual + ")");
        } else if (command == "expect-focused") {
            Expect(ctx.Session.FocusedPanel == PanelFromName(arg(1)), "focused panel == " + arg(1));
        } else if (command == "key") {
            // key ctrl+shift+z
            std::string chord = Lower(arg(1));
            std::vector<std::string> parts;
            std::stringstream stream(chord);
            std::string part;
            while (std::getline(stream, part, '+'))
                parts.push_back(part);
            for (const std::string& p : parts) {
                ImGuiKey key = ImGuiKey_None;
                if (p == "ctrl") key = ImGuiMod_Ctrl;
                else if (p == "shift") key = ImGuiMod_Shift;
                else if (p == "alt") key = ImGuiMod_Alt;
                else key = KeyFromName(p);
                if (key == ImGuiKey_None) {
                    Expect(false, "unknown key '" + p + "'");
                    continue;
                }
                io.AddKeyEvent(key, true);
                m_KeysToRelease.push_back(static_cast<int>(key));
            }
            m_WaitFrames = 1;
        } else if (command == "type") {
            io.AddInputCharactersUTF8(RestAfter(line, 1).c_str());
        } else if (command == "mouse") {
            const ImVec2 position = ToScreen(argf(1), argf(2));
            io.AddMousePosEvent(position.x, position.y);
        } else if (command == "click" || command == "dblclick" || command == "press") {
            // click [x y] [button]
            int buttonArg = 1;
            if (args.size() >= 3 && (std::isdigit(static_cast<unsigned char>(arg(1)[0])) || arg(1)[0] == '-')) {
                // Hover first (ImGui resolves hovered items from the previous frame), then press.
                const ImVec2 position = ToScreen(argf(1), argf(2));
                io.AddMousePosEvent(position.x, position.y);
                m_Lines.push_front(command + " " + arg(3));
                m_WaitFrames = 2;
                return;
            }
            const int button = MouseButtonFromName(arg(buttonArg));
            io.AddMouseButtonEvent(button, true);
            if (command == "dblclick") {
                io.AddMouseButtonEvent(button, false);
                io.AddMouseButtonEvent(button, true);
            }
            m_MouseButtonToRelease = button;
        } else if (command == "drag") {
            // drag x1 y1 x2 y2 [button] [steps]
            m_Drag = { argf(1), argf(2), argf(3), argf(4), MouseButtonFromName(arg(5)), 0, args.size() > 6 ? std::max(2, std::atoi(arg(6).c_str())) : 8 };
            const ImVec2 start = ToScreen(m_Drag.FromX, m_Drag.FromY);
            io.AddMousePosEvent(start.x, start.y);
            m_Drag.Step = -3; // hover a couple of frames before pressing
            m_Dragging = true;
        } else if (command == "wheel") {
            io.AddMouseWheelEvent(0.0f, argf(1));
        } else {
            Expect(false, "unknown command '" + command + "'");
        }
    }

}
