#include "DualityEditor/SceneHistory.h"

#include <algorithm>
#include <fstream>
#include <iterator>

#include <nlohmann/json.hpp>

#include "DualityEditor/EntityIdRegistry.h"
#include "DualityEngine/Asset/AnimationClipLoader.h"
#include "DualityEngine/Asset/TilemapAsset.h"
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

        std::string ReadFile(const std::string& path) {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return {};
            return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        }

        uint64_t s_RestoredFileSerial = 0;
        std::vector<std::string> s_LastRestoredFiles;
    }

    std::vector<std::string> SceneHistory::SnapshotFiles() const {
        std::vector<std::string> files;
        files.reserve(m_TrackedFiles.size());
        for (const std::string& path : m_TrackedFiles)
            files.push_back(ReadFile(path));
        return files;
    }

    void SceneHistory::TrackFile(const std::string& path) {
        if (path.empty() || IsTrackingFile(path))
            return;
        m_TrackedFiles.push_back(path);
        const std::string content = ReadFile(path);
        for (State& state : m_States)
            state.Files.push_back(content);
    }

    bool SceneHistory::IsTrackingFile(const std::string& path) const {
        return std::find(m_TrackedFiles.begin(), m_TrackedFiles.end(), path) != m_TrackedFiles.end();
    }

    uint64_t SceneHistory::RestoredFileSerial() {
        return s_RestoredFileSerial;
    }

    const std::vector<std::string>& SceneHistory::LastRestoredFiles() {
        return s_LastRestoredFiles;
    }

    void SceneHistory::Reset(Scene& scene, EntityIdRegistry& ids, const HistorySelection& selection) {
        State state;
        state.SceneJson = Snapshot(scene);
        state.Ids = ids.CaptureOrder(scene);
        state.SelectionBefore = selection;
        state.SelectionAfter = selection;
        state.Files = SnapshotFiles();
        m_States.clear();
        m_States.push_back(std::move(state));
        m_Current = 0;
        m_SavedState = m_States.front().SceneJson;
    }

    bool SceneHistory::Capture(Scene& scene, EntityIdRegistry& ids, const std::string& label,
        const HistorySelection& selectionBefore, const HistorySelection& selectionAfter) {
        std::string snapshot = Snapshot(scene);
        std::vector<std::string> files = SnapshotFiles();
        if (!m_States.empty() && m_States[m_Current].SceneJson == snapshot && m_States[m_Current].Files == files) {
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
        state.Files = std::move(files);
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
        // Tracked asset files first (they do not depend on the scene restoring successfully).
        s_LastRestoredFiles.clear();
        for (size_t i = 0; i < m_TrackedFiles.size() && i < state.Files.size(); ++i) {
            const std::string& path = m_TrackedFiles[i];
            if (ReadFile(path) == state.Files[i])
                continue;
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file) {
                Log::Error("SceneHistory: could not restore '" + path + "'");
                continue;
            }
            file << state.Files[i];
            file.close();
            // Runtime caches of the asset kinds tools edit this way.
            AnimationClipLoader::Invalidate(path);
            TilemapAsset::Invalidate(path);
            s_LastRestoredFiles.push_back(path);
        }
        if (!s_LastRestoredFiles.empty())
            ++s_RestoredFileSerial;

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
