#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Duality {
    class Scene;
    class EntityIdRegistry;

    // Selection as stable ids (see EntityIdRegistry) -- what each history state restores.
    struct HistorySelection {
        std::vector<uint64_t> Entities;
        uint64_t Primary = 0;
    };

    // Editor-only, snapshot-based history (Unity's Undo). Scene JSON already knows how to
    // preserve entity references, component data and hierarchy order, making it substantially
    // safer than a separate inverse-command implementation for every component type.
    //
    // Each state also records the stable entity ids of that snapshot (so selection, Hierarchy
    // foldouts and the clipboard survive Undo/Redo), the selection right after the change, and
    // the user-facing label of the change that produced it ("Undo Move" / "Redo Add Component").
    class SceneHistory {
    public:
        void Reset(Scene& scene, EntityIdRegistry& ids, const HistorySelection& selection = {});
        // Appends a state when the scene differs from the current one; returns true if it did.
        // An identical snapshot only refreshes the stored post-change selection.
        bool Capture(Scene& scene, EntityIdRegistry& ids, const std::string& label,
            const HistorySelection& selectionBefore = {}, const HistorySelection& selectionAfter = {});
        void MarkSaved(Scene& scene, EntityIdRegistry& ids);

        bool CanUndo() const { return m_Current > 0; }
        bool CanRedo() const { return !m_States.empty() && m_Current + 1 < m_States.size(); }
        // Restores the previous/next state into `scene` (cleared first) and re-attaches ids.
        // `outSelection` receives the selection recorded with the restored state.
        bool Undo(Scene& scene, EntityIdRegistry& ids, HistorySelection* outSelection = nullptr);
        bool Redo(Scene& scene, EntityIdRegistry& ids, HistorySelection* outSelection = nullptr);
        bool IsAtSavedState() const;

        // Label of the change Undo would revert / Redo would re-apply ("" if none).
        const std::string& UndoLabel() const;
        const std::string& RedoLabel() const;

        std::size_t StateCount() const { return m_States.size(); }
        std::size_t CurrentIndex() const { return m_Current; }

        static constexpr std::size_t MaxStates = 128;

    private:
        struct State {
            std::string SceneJson;
            std::vector<uint64_t> Ids;
            std::string Label;
            HistorySelection SelectionBefore; // restored when this change is undone
            HistorySelection SelectionAfter;  // restored when this change is redone
        };

        bool Restore(Scene& scene, EntityIdRegistry& ids, const State& state) const;

        std::vector<State> m_States;
        std::size_t m_Current = 0;
        std::string m_SavedState;
    };
}
