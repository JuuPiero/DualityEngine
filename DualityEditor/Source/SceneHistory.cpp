#include "DualityEditor/SceneHistory.h"

#include <nlohmann/json.hpp>

#include "DualityEditor/EntityIdRegistry.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneSerializer.h"

namespace Duality {

    namespace {
        std::string Snapshot(Scene& scene) {
            return SceneSerializer(scene).SerializeToJson().dump();
        }

        const std::string& EmptyLabel() {
            static const std::string empty;
            return empty;
        }
    }

    void SceneHistory::Reset(Scene& scene, EntityIdRegistry& ids, const HistorySelection& selection) {
        State state;
        state.SceneJson = Snapshot(scene);
        state.Ids = ids.CaptureOrder(scene);
        state.SelectionBefore = selection;
        state.SelectionAfter = selection;
        m_States.clear();
        m_States.push_back(std::move(state));
        m_Current = 0;
        m_SavedState = m_States.front().SceneJson;
    }

    bool SceneHistory::Capture(Scene& scene, EntityIdRegistry& ids, const std::string& label,
        const HistorySelection& selectionBefore, const HistorySelection& selectionAfter) {
        std::string snapshot = Snapshot(scene);
        if (!m_States.empty() && m_States[m_Current].SceneJson == snapshot) {
            m_States[m_Current].SelectionAfter = selectionAfter;
            return false;
        }
        if (m_Current + 1 < m_States.size())
            m_States.erase(m_States.begin() + static_cast<std::ptrdiff_t>(m_Current + 1), m_States.end());
        State state;
        state.SceneJson = std::move(snapshot);
        state.Ids = ids.CaptureOrder(scene);
        state.Label = label.empty() ? std::string("Change") : label;
        state.SelectionBefore = selectionBefore;
        state.SelectionAfter = selectionAfter;
        m_States.push_back(std::move(state));
        m_Current = m_States.size() - 1;
        if (m_States.size() > MaxStates) {
            m_States.erase(m_States.begin());
            --m_Current;
        }
        return true;
    }

    void SceneHistory::MarkSaved(Scene& scene, EntityIdRegistry& ids) {
        {
            const HistorySelection current = m_States.empty() ? HistorySelection{} : m_States[m_Current].SelectionAfter;
            Capture(scene, ids, "Change", current, current);
        }
        m_SavedState = m_States.empty() ? std::string() : m_States[m_Current].SceneJson;
    }

    bool SceneHistory::Restore(Scene& scene, EntityIdRegistry& ids, const State& state) const {
        try {
            scene.Clear();
            if (!SceneSerializer(scene).DeserializeFromJson(nlohmann::json::parse(state.SceneJson))) {
                Log::Error("SceneHistory: could not restore scene snapshot");
                return false;
            }
            ids.ApplyOrder(scene, state.Ids);
            return true;
        } catch (const nlohmann::json::exception& error) {
            Log::Error(std::string("SceneHistory: invalid scene snapshot: ") + error.what());
            return false;
        }
    }

    bool SceneHistory::Undo(Scene& scene, EntityIdRegistry& ids, HistorySelection* outSelection) {
        if (!CanUndo())
            return false;
        const std::size_t target = m_Current - 1;
        if (!Restore(scene, ids, m_States[target]))
            return false;
        // Unity restores the selection that existed before the undone change: that is the
        // selection recorded with the state we land on.
        if (outSelection)
            *outSelection = m_States[m_Current].SelectionBefore;
        m_Current = target;
        return true;
    }

    bool SceneHistory::Redo(Scene& scene, EntityIdRegistry& ids, HistorySelection* outSelection) {
        if (!CanRedo())
            return false;
        const std::size_t target = m_Current + 1;
        if (!Restore(scene, ids, m_States[target]))
            return false;
        if (outSelection)
            *outSelection = m_States[target].SelectionAfter;
        m_Current = target;
        return true;
    }

    bool SceneHistory::IsAtSavedState() const {
        return !m_States.empty() && m_States[m_Current].SceneJson == m_SavedState;
    }

    const std::string& SceneHistory::UndoLabel() const {
        return CanUndo() ? m_States[m_Current].Label : EmptyLabel();
    }

    const std::string& SceneHistory::RedoLabel() const {
        return CanRedo() ? m_States[m_Current + 1].Label : EmptyLabel();
    }
}
