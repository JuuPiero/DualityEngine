#include "DualityEngine/Input/Input.h"

#include <array>
#include <unordered_map>

namespace Duality {

    namespace {
        constexpr size_t KeyCount = static_cast<size_t>(KeyCode::Count);
        std::array<bool, KeyCount> s_CurrentKeys{};
        std::array<bool, KeyCount> s_PreviousKeys{};

        std::unordered_map<std::string, float> s_Axes;

        bool s_PointerDown = false;
        bool s_PreviousPointerDown = false;
        glm::vec2 s_PointerPosition{ 0.0f, 0.0f };
        Screen s_PointerScreen = Screen::Top;
    }

    bool Input::GetKey(KeyCode key) {
        return s_CurrentKeys[static_cast<size_t>(key)];
    }

    bool Input::GetKeyDown(KeyCode key) {
        size_t index = static_cast<size_t>(key);
        return s_CurrentKeys[index] && !s_PreviousKeys[index];
    }

    bool Input::GetKeyUp(KeyCode key) {
        size_t index = static_cast<size_t>(key);
        return !s_CurrentKeys[index] && s_PreviousKeys[index];
    }

    float Input::GetAxis(const std::string& axisName) {
        auto it = s_Axes.find(axisName);
        return it != s_Axes.end() ? it->second : 0.0f;
    }

    bool Input::GetPointerDown() {
        return s_PointerDown;
    }

    bool Input::GetPointerUp() {
        return !s_PointerDown && s_PreviousPointerDown;
    }

    glm::vec2 Input::GetPointerPosition() {
        return s_PointerPosition;
    }

    Screen Input::GetPointerScreen() {
        return s_PointerScreen;
    }

    void Input::BeginFrame() {
        s_PreviousKeys = s_CurrentKeys;
        s_PreviousPointerDown = s_PointerDown;
    }

    void Input::SetKeyState(KeyCode key, bool isDown) {
        s_CurrentKeys[static_cast<size_t>(key)] = isDown;
    }

    void Input::SetAxis(const std::string& axisName, float value) {
        s_Axes[axisName] = value;
    }

    void Input::SetPointer(bool isDown, const glm::vec2& position, Screen screen) {
        s_PointerDown = isDown;
        s_PointerPosition = position;
        s_PointerScreen = screen;
    }

}
