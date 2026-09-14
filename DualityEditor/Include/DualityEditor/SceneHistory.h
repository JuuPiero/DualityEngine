#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace Duality {
    class Scene;

    // Editor-only, snapshot-based history. Scene JSON already knows how to preserve entity
    // references, component data and hierarchy order, making it substantially safer than a
    // separate inverse-command implementation for every component type.
    class SceneHistory {
    public:
        void Reset(Scene& scene);
        void Capture(Scene& scene);
        void MarkSaved(Scene& scene);

        bool CanUndo() const { return m_Current > 0; }
        bool CanRedo() const { return !m_States.empty() && m_Current + 1 < m_States.size(); }
        bool Undo(Scene& scene);
        bool Redo(Scene& scene);
        bool IsAtSavedState() const;

    private:
        static constexpr std::size_t MaxStates = 128;
        bool Restore(Scene& scene, const std::string& snapshot) const;

        std::vector<std::string> m_States;
        std::size_t m_Current = 0;
        std::string m_SavedState;
    };
}
