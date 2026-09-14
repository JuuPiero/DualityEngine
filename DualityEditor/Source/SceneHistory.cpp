#include "DualityEditor/SceneHistory.h"

#include <nlohmann/json.hpp>

#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneSerializer.h"

namespace Duality {

    namespace {
        std::string Snapshot(Scene& scene) {
            return SceneSerializer(scene).SerializeToJson().dump();
        }
    }

    void SceneHistory::Reset(Scene& scene) {
        m_States = { Snapshot(scene) };
        m_Current = 0;
        m_SavedState = m_States.front();
    }

    void SceneHistory::Capture(Scene& scene) {
        const std::string snapshot = Snapshot(scene);
        if (!m_States.empty() && m_States[m_Current] == snapshot)
            return;
        if (m_Current + 1 < m_States.size())
            m_States.erase(m_States.begin() + static_cast<std::ptrdiff_t>(m_Current + 1), m_States.end());
        m_States.push_back(snapshot);
        m_Current = m_States.size() - 1;
        if (m_States.size() > MaxStates) {
            m_States.erase(m_States.begin());
            --m_Current;
        }
    }

    void SceneHistory::MarkSaved(Scene& scene) {
        Capture(scene);
        m_SavedState = m_States.empty() ? std::string() : m_States[m_Current];
    }

    bool SceneHistory::Restore(Scene& scene, const std::string& snapshot) const {
        try {
            scene.Clear();
            if (!SceneSerializer(scene).DeserializeFromJson(nlohmann::json::parse(snapshot))) {
                Log::Error("SceneHistory: could not restore scene snapshot");
                return false;
            }
            return true;
        } catch (const nlohmann::json::exception& error) {
            Log::Error(std::string("SceneHistory: invalid scene snapshot: ") + error.what());
            return false;
        }
    }

    bool SceneHistory::Undo(Scene& scene) {
        if (!CanUndo())
            return false;
        const std::size_t target = m_Current - 1;
        if (!Restore(scene, m_States[target]))
            return false;
        m_Current = target;
        return true;
    }

    bool SceneHistory::Redo(Scene& scene) {
        if (!CanRedo())
            return false;
        const std::size_t target = m_Current + 1;
        if (!Restore(scene, m_States[target]))
            return false;
        m_Current = target;
        return true;
    }

    bool SceneHistory::IsAtSavedState() const {
        return !m_States.empty() && m_States[m_Current] == m_SavedState;
    }
}
