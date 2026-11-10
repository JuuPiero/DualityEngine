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

        // Asset files edited from scene tools that do not live in the scene JSON (a .tilemap
        // painted by the Tile Palette, an .anim keyed by the Animation window) take part in the
        // same Undo/Redo: once tracked, every state also snapshots the file's content, a
        // Capture appends a state when only such a file changed, and Undo/Redo writes the file
        // back. Call TrackFile BEFORE the first modification: the current content is recorded
        // into every existing state (it is what the file looked like at each of them).
        void TrackFile(const std::string& path);
        bool IsTrackingFile(const std::string& path) const;
        // Incremented whenever Undo/Redo rewrote a tracked file; tools reload when it changes.
        static uint64_t RestoredFileSerial();
        static const std::vector<std::string>& LastRestoredFiles();

    private:
        struct State {
            std::string SceneJson;
            std::vector<uint64_t> Ids;
            std::string Label;
            HistorySelection SelectionBefore; // restored when this change is undone
            HistorySelection SelectionAfter;  // restored when this change is redone
            std::vector<std::string> Files;   // content of m_TrackedFiles[i] at this state
        };

        bool Restore(Scene& scene, EntityIdRegistry& ids, const State& state) const;
        std::vector<std::string> SnapshotFiles() const;

        std::vector<State> m_States;
        std::size_t m_Current = 0;
        std::string m_SavedState;
        std::vector<std::string> m_TrackedFiles;
    };
}
