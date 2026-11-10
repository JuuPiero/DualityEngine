#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace Duality {

    struct EditorContext;
    class Window;

    // In-process scripted driver for the editor (`DualityEditor --editor-test script.txt`).
    //
    // Exists so the editor's UI can be verified for real -- selection, shortcuts, menus, undo,
    // screenshots -- without synthesizing OS-level mouse/keyboard input (which lands on whatever
    // window happens to be on top of the user's desktop). Keyboard/mouse commands are fed
    // straight into ImGui's own input queue of this process; everything else calls the editor's
    // command layer. One command runs per frame (or several frames for `wait`/`key`/`drag`).
    //
    // Script syntax: one command per line, `#` comments. See Docs/pages/editor.html
    // ("Editor automation") for the command list. `expect*` commands record PASS/FAIL lines on
    // stdout; the editor exits with the number of failures after `quit`.
    class EditorAutomation {
    public:
        bool Load(const std::string& scriptPath);
        bool IsActive() const { return m_Active; }

        // Called once per frame after the EditorContext is built and before the panels run.
        void Update(EditorContext& ctx, Window& window);

        bool WantsQuit() const { return m_Quit; }
        int FailureCount() const { return m_Failures; }

    private:
        void Execute(EditorContext& ctx, Window& window, const std::string& line);
        void Expect(bool condition, const std::string& what);

        std::deque<std::string> m_Lines;
        bool m_Active = false;
        bool m_Quit = false;
        int m_WaitFrames = 0;
        int m_Failures = 0;
        int m_Passes = 0;
        // Keys/mouse buttons pressed by the previous command, released this frame.
        std::vector<int> m_KeysToRelease;
        int m_MouseButtonToRelease = -1;
        // Multi-frame mouse drag in progress.
        struct Drag { float FromX, FromY, ToX, ToY; int Button; int Step; int Steps; };
        bool m_Dragging = false;
        Drag m_Drag{};
        std::string m_ScriptDirectory;
    };

}
