#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "DualityEngine/Asset/AnimationClipLoader.h"
#include "DualityEngine/ECS/Entity.h"

namespace Duality {

    struct EditorContext;
    class Scene;
    class EntityIdRegistry;

    // Unity's Animation window (Window > Animation > Animation, Ctrl+6) for the selected
    // object's Animation component clip: a property list + dopesheet / curve view, keyframes
    // (add / move / delete), Add Property, record mode (editing a field while recording keys
    // it), edit-mode playback / scrubbing that previews the pose without dirtying the scene,
    // and sprite keyframes (drop sprites, or a sliced sheet, to build a flipbook clip).
    // Clip edits are written to the .anim right away and are part of the scene Undo history
    // (SceneHistory::TrackFile).
    class AnimationWindow {
    public:
        AnimationWindow();
        ~AnimationWindow();

        void OnImGuiRender(EditorContext& ctx);
        static bool& IsOpen();
        static AnimationWindow* Instance();

        // ---- Model (UI, automation, tests) -------------------------------------------------
        struct Track {
            bool IsObject = false;   // sprite / AssetRef keys
            bool IsChannel = false;  // imported node channel (read-only)
            int Index = 0;           // into Curves / ObjectCurves / Channels
            std::string Label;
        };
        const std::vector<Track>& Tracks() const { return m_Tracks; }
        bool HasClip() const { return !m_ClipPath.empty(); }
        const std::string& ClipPath() const { return m_ClipPath; }
        const AnimationClipData& Clip() const { return m_Clip; }
        Entity Root() const;
        float Time() const { return m_Time; }
        int Frame() const;
        void SetFrame(int frame);
        bool IsPreviewing() const { return m_Previewing; }
        bool IsRecording() const { return m_Recording; }
        bool IsReadOnly() const { return !m_Clip.Channels.empty(); }

        void SetPreview(EditorContext& ctx, bool on);
        void SetRecording(EditorContext& ctx, bool on);
        void SetPlaying(bool playing) { m_Playing = playing; }
        void SetCurvesView(bool curves) { m_CurvesView = curves; }
        bool CreateClip(EditorContext& ctx, const std::string& assetPath);
        // Adds (or replaces) a key for a property of root / a descendant ("" = root).
        void SetFloatKey(EditorContext& ctx, const std::string& path, const std::string& component, const std::string& property, float time, float value);
        void SetObjectKey(EditorContext& ctx, const std::string& path, const std::string& component, const std::string& property, float time, const std::string& value);
        // Unity's Add Property: keys at 0 and at the clip end with the current value.
        void AddProperty(EditorContext& ctx, const std::string& path, const std::string& component, const std::string& property);
        void RemoveTrack(EditorContext& ctx, int trackIndex);
        void DeleteKey(EditorContext& ctx, int trackIndex, int keyIndex);
        void MoveKey(EditorContext& ctx, int trackIndex, int keyIndex, float newTime);
        // Sprite frames from `startFrame` on the root's Sprite Renderer.Texture track.
        void AddSpriteFrames(EditorContext& ctx, const std::vector<std::string>& sprites, int startFrame);
        int KeyCount(int trackIndex) const;
        float KeyTime(int trackIndex, int keyIndex) const;

    private:
        struct PropertyKey {
            std::string Path, Component, Property;
            bool IsObject = false;
            bool operator<(const PropertyKey& o) const {
                return std::tie(Path, Component, Property, IsObject) < std::tie(o.Path, o.Component, o.Property, o.IsObject);
            }
        };
        struct PropertyValue { float Number = 0.0f; std::string Object; };

        void SyncTarget(EditorContext& ctx);
        void LoadClip(const std::string& path);
        void RebuildTracks();
        void CommitClip(EditorContext& ctx, const char* label);
        float FrameDuration() const;
        float ClipLength() const;

        // Preview (Unity's Animation-mode): originals are restored when preview stops.
        void InstallPreviewHook(EditorContext& ctx);
        void RemovePreviewHook(EditorContext& ctx);
        void SnapshotOriginals();
        void RestoreOriginals();
        void ApplyPreview();
        void CaptureObserved();
        bool ReadProperty(const PropertyKey& key, PropertyValue& out) const;
        void WriteProperty(const PropertyKey& key, const PropertyValue& value) const;
        void DetectRecordedChanges(EditorContext& ctx);

        void DrawToolbar(EditorContext& ctx);
        void DrawNoClip(EditorContext& ctx);
        void DrawPropertyList(EditorContext& ctx, float width, float rowHeight, float headerHeight);
        void DrawAddPropertyPopup(EditorContext& ctx);
        void DrawDopesheet(EditorContext& ctx, float rowHeight, float headerHeight);
        void DrawCurves(EditorContext& ctx, float headerHeight);
        float TimeToX(float time) const;
        float XToTime(float x) const;

        Scene* m_Scene = nullptr;
        EntityIdRegistry* m_Ids = nullptr;
        uint64_t m_RootId = 0;
        bool m_Locked = false;
        std::string m_ClipPath;
        AnimationClipData m_Clip;
        std::vector<Track> m_Tracks;
        uint64_t m_RestoreSerial = 0;

        float m_Time = 0.0f;
        bool m_Playing = false;
        bool m_Previewing = false;
        bool m_Recording = false;
        bool m_HookInstalled = false;
        bool m_CurvesView = false;
        std::map<PropertyKey, PropertyValue> m_Originals;
        std::map<std::string, std::vector<float>> m_OriginalChannels; // node name -> T R S
        std::map<PropertyKey, PropertyValue> m_Observed;
        size_t m_HistoryIndexSeen = 0;

        // Dopesheet view.
        float m_PixelsPerSecond = 200.0f;
        float m_ViewStart = 0.0f;
        float m_AreaX = 0.0f, m_AreaWidth = 1.0f;
        std::set<std::pair<int, int>> m_SelectedKeys; // (track, key)
        bool m_DraggingKeys = false;
        float m_DragStartTime = 0.0f;
        bool m_Scrubbing = false;
        int m_SelectedTrack = -1;
        // Curve view drag: (curve index, key index) and the value range shown.
        int m_CurveDragCurve = -1, m_CurveDragKey = -1;
        float m_CurveMin = 0.0f, m_CurveMax = 1.0f;
    };

}
