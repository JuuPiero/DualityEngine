#include "DualityEditor/Windows/AnimationWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

#include <imgui.h>

#include "DualityEditor/EditorAutomation.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/EditorIcons.h"
#include "DualityEditor/EntityIdRegistry.h"
#include "DualityEditor/SceneHistory.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEditor/ThumbnailCache.h"
#include "DualityEditor/Windows/SpriteAssetUI.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Asset/SpriteDatabase.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Project/Project.h"
#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Scene/AnimationPlayback.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    namespace {
        AnimationWindow* s_Instance = nullptr;

        // Font Awesome glyphs used only here.
        constexpr const char* kIconRecord = "\xEF\x84\x91";     // circle
        constexpr const char* kIconFirst = "\xEF\x81\x88";      // backward-step
        constexpr const char* kIconPrev = "\xEF\x81\x8A";       // backward (f04a)
        constexpr const char* kIconNext = "\xEF\x81\x8E";       // forward (f04e)
        constexpr const char* kIconLast = "\xEF\x81\x91";       // forward-step
        constexpr const char* kIconKey = "\xEF\x82\x84";        // key

        const ImU32 kKeyColor = IM_COL32(200, 200, 200, 255);
        const ImU32 kKeySelected = IM_COL32(64, 160, 255, 255);
        const ImU32 kSpriteKeyColor = IM_COL32(180, 220, 120, 255);
        const ImU32 kPlayheadColor = IM_COL32(255, 80, 80, 255);
        const ImU32 kRecordTint = IM_COL32(150, 30, 30, 60);
        const ImU32 kCurveColors[] = { IM_COL32(240, 90, 90, 255), IM_COL32(110, 220, 110, 255), IM_COL32(90, 150, 255, 255),
            IM_COL32(240, 200, 80, 255), IM_COL32(220, 120, 240, 255), IM_COL32(90, 220, 220, 255) };

        std::string TrackLabel(const std::string& path, const std::string& component, const std::string& property) {
            std::string label = path.empty() ? std::string() : path + " : ";
            return label + component + "." + property;
        }

        void DrawDiamond(ImDrawList* draw, ImVec2 center, float radius, ImU32 color) {
            draw->AddQuadFilled(ImVec2(center.x, center.y - radius), ImVec2(center.x + radius, center.y),
                ImVec2(center.x, center.y + radius), ImVec2(center.x - radius, center.y), color);
            draw->AddQuad(ImVec2(center.x, center.y - radius), ImVec2(center.x + radius, center.y),
                ImVec2(center.x, center.y + radius), ImVec2(center.x - radius, center.y), IM_COL32(20, 20, 20, 255));
        }
    }

    AnimationWindow::AnimationWindow() { s_Instance = this; }
    AnimationWindow::~AnimationWindow() { if (s_Instance == this) s_Instance = nullptr; }

    bool& AnimationWindow::IsOpen() {
        static bool open = false;
        return open;
    }

    AnimationWindow* AnimationWindow::Instance() { return s_Instance; }

    Entity AnimationWindow::Root() const {
        if (!m_Scene || !m_Ids || m_RootId == 0)
            return {};
        return m_Ids->Find(*m_Scene, m_RootId);
    }

    float AnimationWindow::FrameDuration() const {
        return 1.0f / std::max(1.0f, m_Clip.SampleRate);
    }

    int AnimationWindow::Frame() const {
        return static_cast<int>(std::lround(m_Time * std::max(1.0f, m_Clip.SampleRate)));
    }

    void AnimationWindow::SetFrame(int frame) {
        m_Time = std::max(0, frame) * FrameDuration();
    }

    float AnimationWindow::ClipLength() const {
        return std::max(m_Clip.Duration, m_Clip.LastKeyTime());
    }

    // ---- Target / clip -------------------------------------------------------------------

    void AnimationWindow::SyncTarget(EditorContext& ctx) {
        m_Scene = &ctx.SceneRef;
        m_Ids = &ctx.Session.Ids;
        if (!m_Locked && ctx.Selected) {
            // Unity animates the selected object's nearest Animation root (self or ancestor).
            Entity root = ctx.Selected;
            for (Entity current = ctx.Selected; current; current = current.GetComponent<HierarchyComponent>().Parent) {
                if (current.HasComponent<AnimationComponent>()) {
                    root = current;
                    break;
                }
            }
            const uint64_t id = ctx.Session.Ids.Get(root);
            if (id != m_RootId) {
                SetPreview(ctx, false);
                m_RootId = id;
                m_SelectedKeys.clear();
                m_SelectedTrack = -1;
            }
        }
        Entity root = Root();
        std::string clipPath;
        if (root && root.HasComponent<AnimationComponent>())
            clipPath = AssetDatabase::ResolvePath(root.GetComponent<AnimationComponent>().Clip.Guid);
        if (clipPath != m_ClipPath) {
            SetPreview(ctx, false);
            LoadClip(clipPath);
        }
        // Undo/Redo rewrote the clip file.
        if (SceneHistory::RestoredFileSerial() != m_RestoreSerial) {
            m_RestoreSerial = SceneHistory::RestoredFileSerial();
            const auto& restored = SceneHistory::LastRestoredFiles();
            if (!m_ClipPath.empty() && std::find(restored.begin(), restored.end(), m_ClipPath) != restored.end())
                LoadClip(m_ClipPath);
        }
    }

    void AnimationWindow::LoadClip(const std::string& path) {
        m_ClipPath = path;
        m_Clip = path.empty() ? AnimationClipData{} : AnimationClipLoader::Load(path);
        m_SelectedKeys.clear();
        RebuildTracks();
    }

    void AnimationWindow::RebuildTracks() {
        m_Tracks.clear();
        for (int i = 0; i < static_cast<int>(m_Clip.ObjectCurves.size()); ++i) {
            const AnimationObjectCurve& curve = m_Clip.ObjectCurves[static_cast<size_t>(i)];
            m_Tracks.push_back({ true, false, i, TrackLabel(curve.Path, curve.Component, curve.Property) });
        }
        for (int i = 0; i < static_cast<int>(m_Clip.Curves.size()); ++i) {
            const AnimationFloatCurve& curve = m_Clip.Curves[static_cast<size_t>(i)];
            m_Tracks.push_back({ false, false, i, TrackLabel(curve.Path, curve.Component, curve.Property) });
        }
        for (int i = 0; i < static_cast<int>(m_Clip.Channels.size()); ++i)
            m_Tracks.push_back({ false, true, i, m_Clip.Channels[static_cast<size_t>(i)].NodeName + " : Transform (imported)" });
        if (m_SelectedTrack >= static_cast<int>(m_Tracks.size()))
            m_SelectedTrack = -1;
    }

    void AnimationWindow::CommitClip(EditorContext& ctx, const char* label) {
        if (m_ClipPath.empty() || IsReadOnly())
            return;
        // Undo/Redo covers the .anim: track it before the first write of this session.
        if (!ctx.History.IsTrackingFile(m_ClipPath))
            ctx.History.TrackFile(m_ClipPath);
        // Unity: a clip lasts until its last key; sprite clips hold their last frame one frame.
        m_Clip.Duration = m_Clip.LastKeyTime() + (m_Clip.ObjectCurves.empty() ? 0.0f : FrameDuration());
        AnimationClipLoader::Save(m_ClipPath, m_Clip);
        RebuildTracks();
        MarkSceneDirty(ctx, label);
        if (m_Previewing) {
            SnapshotOriginals();
            ApplyPreview();
            CaptureObserved();
        }
    }

    bool AnimationWindow::CreateClip(EditorContext& ctx, const std::string& assetPath) {
        Entity root = Root();
        if (!root)
            return false;
        std::filesystem::path path(assetPath);
        if (path.is_relative()) {
            if (auto project = Project::GetActive())
                path = std::filesystem::path(project->GetAssetsDirectory()) / path;
        }
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        // Unique name, like Unity's "New Animation 1".
        std::filesystem::path unique = path;
        for (int i = 1; std::filesystem::exists(unique); ++i)
            unique = path.parent_path() / (path.stem().string() + " " + std::to_string(i) + path.extension().string());
        AnimationClipData clip;
        clip.SampleRate = 12.0f;
        if (!AnimationClipLoader::Save(unique.string(), clip))
            return false;
        const std::string guid = AssetMeta::EnsureMetaFile(unique);
        AssetDatabase::Register(guid, unique.string());
        if (!root.HasComponent<AnimationComponent>())
            root.AddComponent<AnimationComponent>();
        root.GetComponent<AnimationComponent>().Clip.Guid = guid;
        MarkSceneDirty(ctx, "Create Animation Clip");
        LoadClip(unique.string());
        Log::Info("Animation: created clip " + unique.filename().string());
        return true;
    }

    void AnimationWindow::SetFloatKey(EditorContext& ctx, const std::string& path, const std::string& component, const std::string& property, float time, float value) {
        if (IsReadOnly())
            return;
        AnimationFloatCurve* curve = AnimationClipLoader::FindCurve(m_Clip, path, component, property);
        if (!curve) {
            AnimationFloatCurve created;
            created.Path = path;
            created.Component = component;
            created.Property = property;
            // Bools / ints step (Unity's constant tangents for discrete values).
            float probe = 0.0f;
            Entity target = AnimationPlayback::FindTarget(*m_Scene, Root(), path);
            if (target && AnimationPlayback::ReadFloat(target, component, property, probe)) {
                ComponentTypeInfo* type = TypeRegistry::Find(component);
                for (const FieldHandle& field : type->Fields)
                    if (field.Name == property) {
                        const FieldValue v = field.Get(type->GetPtr(target));
                        if (std::holds_alternative<bool>(v) || std::holds_alternative<int>(v))
                            created.Interpolation = AnimationInterpolation::Constant;
                    }
            }
            m_Clip.Curves.push_back(created);
            curve = &m_Clip.Curves.back();
        }
        AnimationClipLoader::SetKey(*curve, time, value);
        CommitClip(ctx, "Add Keyframe");
    }

    void AnimationWindow::SetObjectKey(EditorContext& ctx, const std::string& path, const std::string& component, const std::string& property, float time, const std::string& value) {
        if (IsReadOnly())
            return;
        AnimationObjectCurve* curve = AnimationClipLoader::FindObjectCurve(m_Clip, path, component, property);
        if (!curve) {
            AnimationObjectCurve created;
            created.Path = path;
            created.Component = component;
            created.Property = property;
            m_Clip.ObjectCurves.push_back(created);
            curve = &m_Clip.ObjectCurves.back();
        }
        AnimationClipLoader::SetKey(*curve, time, value);
        CommitClip(ctx, "Add Keyframe");
    }

    void AnimationWindow::AddProperty(EditorContext& ctx, const std::string& path, const std::string& component, const std::string& property) {
        Entity target = AnimationPlayback::FindTarget(*m_Scene, Root(), path);
        if (!target || IsReadOnly())
            return;
        const float end = std::max(ClipLength(), 1.0f);
        std::string object;
        float value = 0.0f;
        if (AnimationPlayback::ReadObject(target, component, property, object)) {
            AnimationObjectCurve curve{ path, component, property, {} };
            AnimationClipLoader::SetKey(curve, 0.0f, object);
            if (!AnimationClipLoader::FindObjectCurve(m_Clip, path, component, property))
                m_Clip.ObjectCurves.push_back(curve);
        } else if (AnimationPlayback::ReadFloat(target, component, property, value)) {
            if (!AnimationClipLoader::FindCurve(m_Clip, path, component, property)) {
                SetFloatKey(ctx, path, component, property, 0.0f, value);
                AnimationFloatCurve* curve = AnimationClipLoader::FindCurve(m_Clip, path, component, property);
                AnimationClipLoader::SetKey(*curve, end, value);
            }
        }
        CommitClip(ctx, "Add Property");
    }

    void AnimationWindow::RemoveTrack(EditorContext& ctx, int trackIndex) {
        if (trackIndex < 0 || trackIndex >= static_cast<int>(m_Tracks.size()) || IsReadOnly())
            return;
        const Track track = m_Tracks[static_cast<size_t>(trackIndex)];
        if (track.IsChannel)
            return;
        RestoreOriginals(); // the removed property must not keep a previewed value
        if (track.IsObject)
            m_Clip.ObjectCurves.erase(m_Clip.ObjectCurves.begin() + track.Index);
        else
            m_Clip.Curves.erase(m_Clip.Curves.begin() + track.Index);
        m_SelectedKeys.clear();
        CommitClip(ctx, "Remove Property");
    }

    int AnimationWindow::KeyCount(int trackIndex) const {
        if (trackIndex < 0 || trackIndex >= static_cast<int>(m_Tracks.size()))
            return 0;
        const Track& track = m_Tracks[static_cast<size_t>(trackIndex)];
        if (track.IsChannel) {
            const AnimationChannel& c = m_Clip.Channels[static_cast<size_t>(track.Index)];
            return static_cast<int>(std::max({ c.PositionKeys.size(), c.RotationKeys.size(), c.ScaleKeys.size() }));
        }
        return track.IsObject ? static_cast<int>(m_Clip.ObjectCurves[static_cast<size_t>(track.Index)].Keys.size())
                              : static_cast<int>(m_Clip.Curves[static_cast<size_t>(track.Index)].Keys.size());
    }

    float AnimationWindow::KeyTime(int trackIndex, int keyIndex) const {
        const Track& track = m_Tracks[static_cast<size_t>(trackIndex)];
        if (track.IsChannel) {
            const AnimationChannel& c = m_Clip.Channels[static_cast<size_t>(track.Index)];
            if (keyIndex < static_cast<int>(c.PositionKeys.size())) return c.PositionKeys[static_cast<size_t>(keyIndex)].Time;
            if (keyIndex < static_cast<int>(c.RotationKeys.size())) return c.RotationKeys[static_cast<size_t>(keyIndex)].Time;
            return c.ScaleKeys[static_cast<size_t>(keyIndex)].Time;
        }
        return track.IsObject ? m_Clip.ObjectCurves[static_cast<size_t>(track.Index)].Keys[static_cast<size_t>(keyIndex)].Time
                              : m_Clip.Curves[static_cast<size_t>(track.Index)].Keys[static_cast<size_t>(keyIndex)].Time;
    }

    void AnimationWindow::DeleteKey(EditorContext& ctx, int trackIndex, int keyIndex) {
        if (trackIndex < 0 || trackIndex >= static_cast<int>(m_Tracks.size()) || IsReadOnly())
            return;
        const Track& track = m_Tracks[static_cast<size_t>(trackIndex)];
        if (track.IsChannel || keyIndex < 0 || keyIndex >= KeyCount(trackIndex))
            return;
        if (track.IsObject)
            m_Clip.ObjectCurves[static_cast<size_t>(track.Index)].Keys.erase(m_Clip.ObjectCurves[static_cast<size_t>(track.Index)].Keys.begin() + keyIndex);
        else
            m_Clip.Curves[static_cast<size_t>(track.Index)].Keys.erase(m_Clip.Curves[static_cast<size_t>(track.Index)].Keys.begin() + keyIndex);
        m_SelectedKeys.clear();
        CommitClip(ctx, "Delete Keyframe");
    }

    void AnimationWindow::MoveKey(EditorContext& ctx, int trackIndex, int keyIndex, float newTime) {
        if (trackIndex < 0 || trackIndex >= static_cast<int>(m_Tracks.size()) || IsReadOnly())
            return;
        const Track& track = m_Tracks[static_cast<size_t>(trackIndex)];
        if (track.IsChannel || keyIndex < 0 || keyIndex >= KeyCount(trackIndex))
            return;
        newTime = std::max(0.0f, newTime);
        if (track.IsObject) {
            AnimationObjectCurve& curve = m_Clip.ObjectCurves[static_cast<size_t>(track.Index)];
            const std::string value = curve.Keys[static_cast<size_t>(keyIndex)].Value;
            curve.Keys.erase(curve.Keys.begin() + keyIndex);
            AnimationClipLoader::SetKey(curve, newTime, value);
        } else {
            AnimationFloatCurve& curve = m_Clip.Curves[static_cast<size_t>(track.Index)];
            const float value = curve.Keys[static_cast<size_t>(keyIndex)].Value;
            curve.Keys.erase(curve.Keys.begin() + keyIndex);
            AnimationClipLoader::SetKey(curve, newTime, value);
        }
        m_SelectedKeys.clear();
        CommitClip(ctx, "Move Keyframe");
    }

    void AnimationWindow::AddSpriteFrames(EditorContext& ctx, const std::vector<std::string>& sprites, int startFrame) {
        Entity root = Root();
        if (!root || sprites.empty() || IsReadOnly())
            return;
        if (m_Clip.Curves.empty() && m_Clip.ObjectCurves.empty())
            m_Clip.SampleRate = 12.0f; // Unity's sprite-animation default
        if (!root.HasComponent<SpriteRendererComponent>()) {
            root.AddComponent<SpriteRendererComponent>();
            MarkSceneDirty(ctx, "Add Sprite Renderer");
        }
        AnimationObjectCurve* curve = AnimationClipLoader::FindObjectCurve(m_Clip, "", "Sprite Renderer", "Texture");
        if (!curve) {
            m_Clip.ObjectCurves.push_back({ "", "Sprite Renderer", "Texture", {} });
            curve = &m_Clip.ObjectCurves.back();
        }
        for (size_t i = 0; i < sprites.size(); ++i)
            AnimationClipLoader::SetKey(*curve, (startFrame + static_cast<int>(i)) * FrameDuration(), sprites[i]);
        CommitClip(ctx, "Add Sprite Keyframes");
        SetFrame(startFrame + static_cast<int>(sprites.size()));
    }

    // ---- Preview -------------------------------------------------------------------------

    bool AnimationWindow::ReadProperty(const PropertyKey& key, PropertyValue& out) const {
        Entity target = AnimationPlayback::FindTarget(*m_Scene, Root(), key.Path);
        if (!target)
            return false;
        return key.IsObject ? AnimationPlayback::ReadObject(target, key.Component, key.Property, out.Object)
                            : AnimationPlayback::ReadFloat(target, key.Component, key.Property, out.Number);
    }

    void AnimationWindow::WriteProperty(const PropertyKey& key, const PropertyValue& value) const {
        Entity target = AnimationPlayback::FindTarget(*m_Scene, Root(), key.Path);
        if (!target)
            return;
        if (key.IsObject)
            AnimationPlayback::WriteObject(target, key.Component, key.Property, value.Object);
        else
            AnimationPlayback::WriteFloat(target, key.Component, key.Property, value.Number);
    }

    void AnimationWindow::SnapshotOriginals() {
        if (!m_Scene || !Root())
            return;
        for (const AnimationFloatCurve& curve : m_Clip.Curves) {
            const PropertyKey key{ curve.Path, curve.Component, curve.Property, false };
            PropertyValue value;
            if (!m_Originals.count(key) && ReadProperty(key, value))
                m_Originals[key] = value;
        }
        for (const AnimationObjectCurve& curve : m_Clip.ObjectCurves) {
            const PropertyKey key{ curve.Path, curve.Component, curve.Property, true };
            PropertyValue value;
            if (!m_Originals.count(key) && ReadProperty(key, value))
                m_Originals[key] = value;
        }
        for (const AnimationChannel& channel : m_Clip.Channels) {
            if (m_OriginalChannels.count(channel.NodeName))
                continue;
            for (auto handle : m_Scene->Registry().view<NameComponent, TransformComponent>()) {
                if (m_Scene->Registry().get<NameComponent>(handle).Name != channel.NodeName)
                    continue;
                const TransformComponent& t = m_Scene->Registry().get<TransformComponent>(handle);
                m_OriginalChannels[channel.NodeName] = { t.Translation.x, t.Translation.y, t.Translation.z,
                    t.Rotation.x, t.Rotation.y, t.Rotation.z, t.Scale.x, t.Scale.y, t.Scale.z };
                break;
            }
        }
    }

    void AnimationWindow::RestoreOriginals() {
        if (!m_Scene)
            return;
        for (const auto& [key, value] : m_Originals)
            WriteProperty(key, value);
        for (const auto& [name, v] : m_OriginalChannels) {
            for (auto handle : m_Scene->Registry().view<NameComponent, TransformComponent>()) {
                if (m_Scene->Registry().get<NameComponent>(handle).Name != name)
                    continue;
                TransformComponent& t = m_Scene->Registry().get<TransformComponent>(handle);
                t.Translation = { v[0], v[1], v[2] };
                t.Rotation = { v[3], v[4], v[5] };
                t.Scale = { v[6], v[7], v[8] };
                break;
            }
        }
    }

    void AnimationWindow::ApplyPreview() {
        Entity root = Root();
        if (!root)
            return;
        AnimationPlayback::Sample(*m_Scene, root, m_Clip, m_Time, true);
    }

    void AnimationWindow::CaptureObserved() {
        m_Observed.clear();
        Entity root = Root();
        if (!root || !m_Recording)
            return;
        // Every animatable property of the root and its descendants: a difference next frame
        // is an edit made by the user (Inspector, gizmo...) -> keyed while recording.
        std::vector<std::pair<Entity, std::string>> pending{ { root, std::string() } };
        while (!pending.empty()) {
            auto [entity, path] = pending.back();
            pending.pop_back();
            for (ComponentTypeInfo& type : TypeRegistry::All()) {
                if (!type.Has(entity) || type.DisplayName == "Name" || type.DisplayName == "Hierarchy" || type.DisplayName == "Animation")
                    continue;
                for (const AnimationPlayback::PropertyInfo& info : AnimationPlayback::AnimatableProperties(entity, type.DisplayName)) {
                    const PropertyKey key{ path, type.DisplayName, info.Property, info.IsObject };
                    PropertyValue value;
                    if (ReadProperty(key, value))
                        m_Observed[key] = value;
                }
            }
            for (Entity child : entity.GetComponent<HierarchyComponent>().Children)
                if (child)
                    pending.push_back({ child, (path.empty() ? std::string() : path + "/") + child.GetComponent<NameComponent>().Name });
        }
    }

    void AnimationWindow::DetectRecordedChanges(EditorContext& ctx) {
        if (!m_Recording || m_Observed.empty() || IsReadOnly())
            return;
        bool changed = false;
        for (const auto& [key, before] : m_Observed) {
            PropertyValue now;
            if (!ReadProperty(key, now))
                continue;
            const bool differs = key.IsObject ? now.Object != before.Object : std::fabs(now.Number - before.Number) > 1e-5f;
            if (!differs)
                continue;
            // The pre-edit value becomes the original restored when preview ends.
            if (!m_Originals.count(key))
                m_Originals[key] = before;
            if (key.IsObject) {
                AnimationObjectCurve* curve = AnimationClipLoader::FindObjectCurve(m_Clip, key.Path, key.Component, key.Property);
                if (!curve) { m_Clip.ObjectCurves.push_back({ key.Path, key.Component, key.Property, {} }); curve = &m_Clip.ObjectCurves.back(); }
                AnimationClipLoader::SetKey(*curve, m_Time, now.Object);
            } else {
                AnimationFloatCurve* curve = AnimationClipLoader::FindCurve(m_Clip, key.Path, key.Component, key.Property);
                if (!curve) { m_Clip.Curves.push_back({ key.Path, key.Component, key.Property, AnimationInterpolation::Linear, {} }); curve = &m_Clip.Curves.back(); }
                AnimationClipLoader::SetKey(*curve, m_Time, now.Number);
            }
            changed = true;
        }
        if (changed)
            CommitClip(ctx, "Record Keyframe");
    }

    void AnimationWindow::InstallPreviewHook(EditorContext& ctx) {
        ctx.Session.ScenePreviewHook = [this](bool suspend) {
            if (suspend) {
                RestoreOriginals();
            } else {
                // The scene may have been replaced (Undo/Redo): re-read the authored values.
                m_Originals.clear();
                m_OriginalChannels.clear();
                SnapshotOriginals();
                ApplyPreview();
                CaptureObserved();
            }
        };
        m_HookInstalled = true;
    }

    void AnimationWindow::RemovePreviewHook(EditorContext& ctx) {
        if (m_HookInstalled)
            ctx.Session.ScenePreviewHook = nullptr;
        m_HookInstalled = false;
    }

    void AnimationWindow::SetPreview(EditorContext& ctx, bool on) {
        if (on == m_Previewing)
            return;
        if (on) {
            if (ctx.IsPlaying || !Root() || m_ClipPath.empty())
                return;
            m_Previewing = true;
            m_Originals.clear();
            m_OriginalChannels.clear();
            SnapshotOriginals();
            InstallPreviewHook(ctx);
            ApplyPreview();
            CaptureObserved();
        } else {
            RestoreOriginals();
            m_Originals.clear();
            m_OriginalChannels.clear();
            m_Observed.clear();
            m_Previewing = false;
            m_Recording = false;
            m_Playing = false;
            RemovePreviewHook(ctx);
        }
    }

    void AnimationWindow::SetRecording(EditorContext& ctx, bool on) {
        if (on) {
            if (IsReadOnly())
                return;
            SetPreview(ctx, true);
            if (!m_Previewing)
                return;
            m_Recording = true;
            CaptureObserved();
        } else {
            m_Recording = false;
            m_Observed.clear();
        }
    }

    // ---- UI ----------------------------------------------------------------------------------

    float AnimationWindow::TimeToX(float time) const { return m_AreaX + (time - m_ViewStart) * m_PixelsPerSecond; }
    float AnimationWindow::XToTime(float x) const { return (x - m_AreaX) / m_PixelsPerSecond + m_ViewStart; }

    void AnimationWindow::OnImGuiRender(EditorContext& ctx) {
        bool& open = IsOpen();
        if (!open || ctx.IsPlaying) {
            if (m_Previewing)
                SetPreview(ctx, false);
            if (!open)
                return;
        }
        SyncTarget(ctx);

        // Undo/Redo moved through history: the scene now holds authored values again.
        if (ctx.History.CurrentIndex() != m_HistoryIndexSeen) {
            m_HistoryIndexSeen = ctx.History.CurrentIndex();
            if (m_Previewing)
                CaptureObserved();
        }

        // Edit-mode playback.
        if (m_Playing && m_Previewing) {
            const float length = ClipLength();
            m_Time += ImGui::GetIO().DeltaTime;
            if (length > 0.0f && m_Time > length)
                m_Time = std::fmod(m_Time, length);
        }

        ImGui::SetNextWindowSize(ImVec2(900.0f, 360.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Animation", &open)) {
            ImGui::End();
            return;
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            ctx.Session.FocusedPanel = EditorPanel::Other;
            ctx.Session.FocusedWindowUndo = nullptr; // clip edits live in the scene history
        }

        // Recording: keys whatever the user changed since the last frame, then re-samples.
        if (m_Previewing) {
            DetectRecordedChanges(ctx);
            ApplyPreview();
            CaptureObserved();
        }

        Entity root = Root();
        if (!root) {
            ImGui::TextDisabled("Select a GameObject to animate it.");
            ImGui::End();
            return;
        }
        if (m_ClipPath.empty()) {
            DrawNoClip(ctx);
            ImGui::End();
            return;
        }

        DrawToolbar(ctx);
        const float rowHeight = ImGui::GetFrameHeight() + 2.0f;
        const float headerHeight = 22.0f;
        const float listWidth = 280.0f;
        ImGui::BeginChild("##AnimProperties", ImVec2(listWidth, 0.0f), ImGuiChildFlags_Borders);
        DrawPropertyList(ctx, listWidth, rowHeight, headerHeight);
        ImGui::EndChild();
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::BeginChild("##AnimTimeline", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollWithMouse);
        if (m_CurvesView)
            DrawCurves(ctx, headerHeight);
        else
            DrawDopesheet(ctx, rowHeight, headerHeight);
        ImGui::EndChild();

        ImGui::End();
    }

    void AnimationWindow::DrawNoClip(EditorContext& ctx) {
        Entity root = Root();
        const std::string name = root.GetComponent<NameComponent>().Name;
        ImGui::Spacing();
        ImGui::TextWrapped("To begin animating %s, create an Animation Clip.", name.c_str());
        if (ImGui::Button("Create")) {
            std::string fileName = name;
            for (char& c : fileName)
                if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                    c = '_';
            CreateClip(ctx, "Animations/" + fileName + ".anim");
        }
        ImGui::TextDisabled("Creates Assets/Animations/%s.anim and an Animation component.", name.c_str());
    }

    void AnimationWindow::DrawToolbar(EditorContext& ctx) {
        const float square = ImGui::GetFrameHeight();
        auto toggle = [&](const char* label, bool active, const char* tooltip, ImU32 activeColor) {
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(activeColor));
            const bool pressed = ImGui::Button(label, ImVec2(square * 1.2f, square));
            if (active)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tooltip);
            return pressed;
        };
        ImGui::BeginDisabled(IsReadOnly());
        ImGui::PushStyleColor(ImGuiCol_Text, m_Recording ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
        if (toggle(kIconRecord, m_Recording, "Enable/disable keyframe recording mode", IM_COL32(120, 40, 40, 255)))
            SetRecording(ctx, !m_Recording);
        ImGui::PopStyleColor();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(m_Previewing ? "Preview " "\xEF\x81\xAE" : "Preview", ImVec2(0.0f, square)))
            SetPreview(ctx, !m_Previewing);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Preview the clip in the Scene (values are restored when preview ends)");
        ImGui::SameLine();
        if (ImGui::Button(kIconFirst, ImVec2(square, square))) { SetFrame(0); SetPreview(ctx, true); }
        ImGui::SameLine(0.0f, 1.0f);
        if (ImGui::Button(kIconPrev, ImVec2(square, square))) { SetFrame(Frame() - 1); SetPreview(ctx, true); }
        ImGui::SameLine(0.0f, 1.0f);
        if (toggle(EditorIcons::Play, m_Playing, "Play the clip in the Scene", IM_COL32(60, 90, 140, 255))) {
            SetPreview(ctx, true);
            m_Playing = !m_Playing && m_Previewing;
        }
        ImGui::SameLine(0.0f, 1.0f);
        if (ImGui::Button(kIconNext, ImVec2(square, square))) { SetFrame(Frame() + 1); SetPreview(ctx, true); }
        ImGui::SameLine(0.0f, 1.0f);
        if (ImGui::Button(kIconLast, ImVec2(square, square))) { m_Time = ClipLength(); SetPreview(ctx, true); }
        ImGui::SameLine();
        int frame = Frame();
        ImGui::SetNextItemWidth(60.0f);
        if (ImGui::InputInt("##Frame", &frame, 0, 0)) { SetFrame(frame); SetPreview(ctx, true); }
        ImGui::SameLine();
        ImGui::TextDisabled("%.2fs", m_Time);

        ImGui::SameLine(0.0f, 16.0f);
        ImGui::TextUnformatted(std::filesystem::path(m_ClipPath).stem().string().c_str());
        if (IsReadOnly()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(Read-Only: imported clip)");
        }
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Samples");
        ImGui::SameLine();
        int samples = static_cast<int>(std::lround(m_Clip.SampleRate));
        ImGui::SetNextItemWidth(50.0f);
        ImGui::BeginDisabled(IsReadOnly());
        if (ImGui::InputInt("##Samples", &samples, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
            m_Clip.SampleRate = static_cast<float>(std::clamp(samples, 1, 240));
            CommitClip(ctx, "Change Sample Rate");
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, 16.0f);
        if (ImGui::RadioButton("Dopesheet", !m_CurvesView)) m_CurvesView = false;
        ImGui::SameLine();
        if (ImGui::RadioButton("Curves", m_CurvesView)) m_CurvesView = true;
        ImGui::SameLine(0.0f, 16.0f);
        if (toggle(m_Locked ? EditorIcons::Lock : EditorIcons::Unlock, m_Locked, "Lock the animated object", IM_COL32(70, 70, 70, 255)))
            m_Locked = !m_Locked;
    }

    void AnimationWindow::DrawPropertyList(EditorContext& ctx, float width, float rowHeight, float headerHeight) {
        (void)width;
        ImGui::Dummy(ImVec2(0.0f, headerHeight - ImGui::GetStyle().ItemSpacing.y));
        // Summary row ("all keys"), aligned with the dopesheet's summary row.
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s  %s", kIconKey, Root().GetComponent<NameComponent>().Name.c_str());
        Entity root = Root();
        int removeTrack = -1;
        for (int i = 0; i < static_cast<int>(m_Tracks.size()); ++i) {
            const Track& track = m_Tracks[static_cast<size_t>(i)];
            ImGui::PushID(i);
            const float rowY = ImGui::GetCursorPosY();
            if (ImGui::Selectable("##track", m_SelectedTrack == i, ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, rowHeight - 2.0f)))
                m_SelectedTrack = i;
            if (ImGui::BeginPopupContextItem("TrackContext")) {
                if (ImGui::MenuItem("Remove Properties", nullptr, false, !track.IsChannel && !IsReadOnly()))
                    removeTrack = i;
                ImGui::EndPopup();
            }
            ImGui::SetCursorPosY(rowY + 1.0f);
            ImGui::SameLine(6.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(track.Label.c_str());
            // Value column: the property's current value; editing it keys it (Unity).
            if (!track.IsChannel) {
                ImGui::SameLine(ImGui::GetContentRegionMax().x - 70.0f);
                ImGui::SetNextItemWidth(68.0f);
                if (track.IsObject) {
                    const AnimationObjectCurve& curve = m_Clip.ObjectCurves[static_cast<size_t>(track.Index)];
                    const std::string* value = AnimationClipLoader::Sample(curve, m_Time);
                    ImGui::TextDisabled("%s", value ? SpriteAssetUI::DisplayName(*value).c_str() : "-");
                } else {
                    const AnimationFloatCurve& curve = m_Clip.Curves[static_cast<size_t>(track.Index)];
                    Entity target = AnimationPlayback::FindTarget(ctx.SceneRef, root, curve.Path);
                    float value = AnimationClipLoader::Sample(curve, m_Time, 0.0f);
                    if (!m_Previewing && target)
                        AnimationPlayback::ReadFloat(target, curve.Component, curve.Property, value);
                    ImGui::BeginDisabled(IsReadOnly() || !target);
                    if (ImGui::DragFloat("##value", &value, 0.01f, 0.0f, 0.0f, "%.3f")) {
                        // One undo step for the whole drag (the history commits once no item
                        // is active any more).
                        const std::string path = curve.Path, component = curve.Component, property = curve.Property;
                        SetFloatKey(ctx, path, component, property, m_Time, value);
                    }
                    ImGui::EndDisabled();
                }
            }
            ImGui::SetCursorPosY(rowY + rowHeight);
            ImGui::PopID();
        }
        if (removeTrack >= 0)
            RemoveTrack(ctx, removeTrack);

        ImGui::Spacing();
        ImGui::BeginDisabled(IsReadOnly());
        if (ImGui::Button("Add Property", ImVec2(-1.0f, 0.0f)))
            ImGui::OpenPopup("AddProperty");
        ImGui::EndDisabled();
        DrawAddPropertyPopup(ctx);
        ImGui::TextDisabled("Drop sprites (or a sliced texture)\non the timeline for a flipbook.");
    }

    void AnimationWindow::DrawAddPropertyPopup(EditorContext& ctx) {
        if (!ImGui::BeginPopup("AddProperty"))
            return;
        Entity root = Root();
        // Root + descendants, Unity's tree of GameObject > Component > property.
        std::vector<std::pair<Entity, std::string>> pending{ { root, std::string() } };
        std::vector<std::pair<Entity, std::string>> ordered;
        while (!pending.empty()) {
            auto item = pending.front();
            pending.erase(pending.begin());
            ordered.push_back(item);
            for (Entity child : item.first.GetComponent<HierarchyComponent>().Children)
                if (child)
                    pending.push_back({ child, (item.second.empty() ? std::string() : item.second + "/") + child.GetComponent<NameComponent>().Name });
        }
        for (const auto& [entity, path] : ordered) {
            const std::string label = path.empty() ? entity.GetComponent<NameComponent>().Name : path;
            if (!ImGui::BeginMenu(label.c_str()))
                continue;
            for (ComponentTypeInfo& type : TypeRegistry::All()) {
                if (!type.Has(entity) || type.DisplayName == "Name" || type.DisplayName == "Hierarchy" || type.DisplayName == "Animation")
                    continue;
                const auto properties = AnimationPlayback::AnimatableProperties(entity, type.DisplayName);
                if (properties.empty() || !ImGui::BeginMenu(type.DisplayName.c_str()))
                    continue;
                for (const AnimationPlayback::PropertyInfo& info : properties) {
                    const bool exists = info.IsObject ? AnimationClipLoader::FindObjectCurve(m_Clip, path, type.DisplayName, info.Property) != nullptr
                                                      : AnimationClipLoader::FindCurve(m_Clip, path, type.DisplayName, info.Property) != nullptr;
                    if (ImGui::MenuItem(info.Property.c_str(), nullptr, false, !exists))
                        AddProperty(ctx, path, type.DisplayName, info.Property);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    void AnimationWindow::DrawDopesheet(EditorContext& ctx, float rowHeight, float headerHeight) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        m_AreaX = origin.x + 8.0f;
        m_AreaWidth = std::max(50.0f, avail.x - 16.0f);
        ImGuiIO& io = ImGui::GetIO();
        const float frameDuration = FrameDuration();

        // Whole area is one interaction surface.
        ImGui::InvisibleButton("##Dopesheet", ImVec2(std::max(1.0f, avail.x), std::max(1.0f, avail.y)),
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 mouse = io.MousePos;

        // Zoom (wheel) / pan (MMB or Alt+drag).
        if (hovered && io.MouseWheel != 0.0f) {
            const float t = XToTime(mouse.x);
            m_PixelsPerSecond = std::clamp(m_PixelsPerSecond * std::pow(1.15f, io.MouseWheel), 20.0f, 4000.0f);
            m_ViewStart = std::max(0.0f, t - (mouse.x - m_AreaX) / m_PixelsPerSecond);
        }
        if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
            m_ViewStart = std::max(0.0f, m_ViewStart - io.MouseDelta.x / m_PixelsPerSecond);

        if (m_Recording)
            draw->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), kRecordTint);

        // Ruler with frame ticks.
        const float rulerBottom = origin.y + headerHeight;
        draw->AddRectFilled(origin, ImVec2(origin.x + avail.x, rulerBottom), IM_COL32(45, 45, 45, 255));
        const float pixelsPerFrame = m_PixelsPerSecond * frameDuration;
        // Label spacing: a divisor of the sample rate (0:00, 0:06, 1:00 at 12 fps like Unity),
        // or whole seconds when zoomed far out.
        const int sampleRateInt = std::max(1, static_cast<int>(std::lround(m_Clip.SampleRate)));
        int labelEvery = sampleRateInt;
        for (int candidate = 1; candidate <= sampleRateInt; ++candidate) {
            if (sampleRateInt % candidate == 0 && pixelsPerFrame * candidate >= 40.0f) {
                labelEvery = candidate;
                break;
            }
        }
        while (pixelsPerFrame * labelEvery < 40.0f)
            labelEvery *= 2;
        const int firstFrame = std::max(0, static_cast<int>(std::floor(m_ViewStart / frameDuration)));
        const int lastFrame = static_cast<int>(std::ceil(XToTime(m_AreaX + m_AreaWidth) / frameDuration));
        for (int f = firstFrame; f <= lastFrame; ++f) {
            const float x = TimeToX(f * frameDuration);
            const bool major = f % labelEvery == 0;
            if (!major && pixelsPerFrame < 4.0f)
                continue;
            draw->AddLine(ImVec2(x, rulerBottom - (major ? 10.0f : 4.0f)), ImVec2(x, rulerBottom), IM_COL32(150, 150, 150, 255));
            if (major) {
                char text[32];
                const int sampleRate = static_cast<int>(std::lround(m_Clip.SampleRate));
                std::snprintf(text, sizeof(text), "%d:%02d", f / std::max(1, sampleRate), f % std::max(1, sampleRate));
                draw->AddText(ImVec2(x + 2.0f, origin.y + 1.0f), IM_COL32(190, 190, 190, 255), text);
                draw->AddLine(ImVec2(x, rulerBottom), ImVec2(x, origin.y + avail.y), IM_COL32(60, 60, 60, 255));
            }
        }
        // Clip end marker.
        const float endX = TimeToX(ClipLength());
        draw->AddRectFilled(ImVec2(endX, rulerBottom), ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(0, 0, 0, 50));

        // Rows: summary + tracks (aligned with the property list).
        const float firstRowY = rulerBottom + ImGui::GetStyle().ItemSpacing.y;
        auto rowCenterY = [&](int row) { return firstRowY + rowHeight * (row + 0.5f) + 2.0f; };
        const int trackCount = static_cast<int>(m_Tracks.size());
        for (int row = 0; row <= trackCount; ++row) {
            const float y = firstRowY + rowHeight * row;
            if (row % 2 == 1)
                draw->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + avail.x, y + rowHeight), IM_COL32(255, 255, 255, 8));
        }

        // Pending key drag offset (in frames), drawn as ghosts until release.
        float dragDelta = 0.0f;
        if (m_DraggingKeys)
            dragDelta = std::round((XToTime(mouse.x) - m_DragStartTime) / frameDuration) * frameDuration;

        // Keys (and hit testing).
        int hitTrack = -1, hitKey = -1;
        std::set<float> summaryTimes;
        for (int t = 0; t < trackCount; ++t) {
            const Track& track = m_Tracks[static_cast<size_t>(t)];
            const int keys = KeyCount(t);
            for (int k = 0; k < keys; ++k) {
                float time = KeyTime(t, k);
                const bool selected = m_SelectedKeys.count({ t, k }) != 0;
                if (selected)
                    time = std::max(0.0f, time + dragDelta);
                summaryTimes.insert(time);
                const ImVec2 center(TimeToX(time), rowCenterY(t + 1));
                if (center.x < m_AreaX - 8.0f || center.x > m_AreaX + m_AreaWidth + 8.0f)
                    continue;
                ImU32 color = track.IsObject ? kSpriteKeyColor : kKeyColor;
                if (track.IsChannel)
                    color = IM_COL32(140, 140, 140, 255);
                DrawDiamond(draw, center, 5.5f, selected ? kKeySelected : color);
                if (std::fabs(mouse.x - center.x) <= 6.0f && std::fabs(mouse.y - center.y) <= 7.0f) {
                    hitTrack = t;
                    hitKey = k;
                    if (track.IsObject && hovered) {
                        const std::string& value = m_Clip.ObjectCurves[static_cast<size_t>(track.Index)].Keys[static_cast<size_t>(k)].Value;
                        ImGui::SetTooltip("%s", SpriteAssetUI::DisplayName(value).c_str());
                    }
                }
            }
        }
        for (float time : summaryTimes)
            DrawDiamond(draw, ImVec2(TimeToX(time), rowCenterY(0)), 5.0f, IM_COL32(120, 120, 120, 255));

        // Playhead.
        const float playheadX = TimeToX(m_Time);
        draw->AddLine(ImVec2(playheadX, origin.y), ImVec2(playheadX, origin.y + avail.y), kPlayheadColor, 1.5f);

        // ---- Mouse ---------------------------------------------------------------------------
        const bool inRuler = mouse.y < rulerBottom;
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (inRuler) {
                m_Scrubbing = true;
                SetPreview(ctx, true);
            } else if (hitTrack >= 0) {
                if (!io.KeyCtrl && !m_SelectedKeys.count({ hitTrack, hitKey }))
                    m_SelectedKeys.clear();
                m_SelectedKeys.insert({ hitTrack, hitKey });
                m_SelectedTrack = hitTrack;
                if (!IsReadOnly()) {
                    m_DraggingKeys = true;
                    m_DragStartTime = XToTime(mouse.x);
                }
            } else {
                m_SelectedKeys.clear();
            }
        }
        if (m_Scrubbing) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                // Snap to frames like Unity.
                m_Time = std::max(0.0f, std::round(XToTime(mouse.x) / frameDuration) * frameDuration);
                m_Playing = false;
            } else {
                m_Scrubbing = false;
            }
        }
        if (m_DraggingKeys && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_DraggingKeys = false;
            if (dragDelta != 0.0f) {
                // Apply the move to every selected key (re-sorted per curve).
                for (const auto& [t, k] : m_SelectedKeys) {
                    const Track& track = m_Tracks[static_cast<size_t>(t)];
                    if (track.IsChannel)
                        continue;
                    if (track.IsObject)
                        m_Clip.ObjectCurves[static_cast<size_t>(track.Index)].Keys[static_cast<size_t>(k)].Time += dragDelta;
                    else
                        m_Clip.Curves[static_cast<size_t>(track.Index)].Keys[static_cast<size_t>(k)].Time += dragDelta;
                }
                for (AnimationFloatCurve& curve : m_Clip.Curves) {
                    for (auto& key : curve.Keys) key.Time = std::max(0.0f, key.Time);
                    std::stable_sort(curve.Keys.begin(), curve.Keys.end(), [](const auto& a, const auto& b) { return a.Time < b.Time; });
                }
                for (AnimationObjectCurve& curve : m_Clip.ObjectCurves) {
                    for (auto& key : curve.Keys) key.Time = std::max(0.0f, key.Time);
                    std::stable_sort(curve.Keys.begin(), curve.Keys.end(), [](const auto& a, const auto& b) { return a.Time < b.Time; });
                }
                m_SelectedKeys.clear();
                CommitClip(ctx, "Move Keyframes");
            }
        }
        // Double-click on a row adds a key there (current value of that property).
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !inRuler && hitTrack < 0 && !IsReadOnly()) {
            const int row = static_cast<int>(std::floor((mouse.y - firstRowY) / rowHeight)) - 1;
            if (row >= 0 && row < trackCount && !m_Tracks[static_cast<size_t>(row)].IsChannel) {
                const float time = std::max(0.0f, std::round(XToTime(mouse.x) / frameDuration) * frameDuration);
                const Track track = m_Tracks[static_cast<size_t>(row)];
                if (track.IsObject) {
                    const AnimationObjectCurve curve = m_Clip.ObjectCurves[static_cast<size_t>(track.Index)];
                    const std::string* value = AnimationClipLoader::Sample(curve, time);
                    if (value)
                        SetObjectKey(ctx, curve.Path, curve.Component, curve.Property, time, *value);
                } else {
                    const AnimationFloatCurve curve = m_Clip.Curves[static_cast<size_t>(track.Index)];
                    SetFloatKey(ctx, curve.Path, curve.Component, curve.Property, time, AnimationClipLoader::Sample(curve, time, 0.0f));
                }
            }
        }
        // Delete selected keys.
        if (ImGui::IsWindowFocused() && !m_SelectedKeys.empty() && !io.WantTextInput &&
            (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) && !IsReadOnly()) {
            // Erase highest indices first so earlier indices stay valid.
            std::vector<std::pair<int, int>> keys(m_SelectedKeys.begin(), m_SelectedKeys.end());
            std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
            for (const auto& [t, k] : keys) {
                const Track& track = m_Tracks[static_cast<size_t>(t)];
                if (track.IsChannel) continue;
                if (track.IsObject) m_Clip.ObjectCurves[static_cast<size_t>(track.Index)].Keys.erase(m_Clip.ObjectCurves[static_cast<size_t>(track.Index)].Keys.begin() + k);
                else m_Clip.Curves[static_cast<size_t>(track.Index)].Keys.erase(m_Clip.Curves[static_cast<size_t>(track.Index)].Keys.begin() + k);
            }
            m_SelectedKeys.clear();
            CommitClip(ctx, "Delete Keyframes");
        }
        // Context menu.
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !IsReadOnly()) {
            if (hitTrack >= 0 && !m_SelectedKeys.count({ hitTrack, hitKey })) {
                m_SelectedKeys.clear();
                m_SelectedKeys.insert({ hitTrack, hitKey });
            }
            ImGui::OpenPopup("DopesheetContext");
        }
        if (ImGui::BeginPopup("DopesheetContext")) {
            if (ImGui::MenuItem("Add Key (all properties at the playhead)")) {
                for (const Track& track : std::vector<Track>(m_Tracks)) {
                    if (track.IsChannel) continue;
                    if (track.IsObject) {
                        const AnimationObjectCurve curve = m_Clip.ObjectCurves[static_cast<size_t>(track.Index)];
                        if (const std::string* value = AnimationClipLoader::Sample(curve, m_Time))
                            SetObjectKey(ctx, curve.Path, curve.Component, curve.Property, m_Time, *value);
                    } else {
                        const AnimationFloatCurve curve = m_Clip.Curves[static_cast<size_t>(track.Index)];
                        SetFloatKey(ctx, curve.Path, curve.Component, curve.Property, m_Time, AnimationClipLoader::Sample(curve, m_Time, 0.0f));
                    }
                }
            }
            if (ImGui::MenuItem("Delete Keys", "Del", false, !m_SelectedKeys.empty())) {
                std::vector<std::pair<int, int>> keys(m_SelectedKeys.begin(), m_SelectedKeys.end());
                std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
                for (const auto& [t, k] : keys) {
                    const Track& track = m_Tracks[static_cast<size_t>(t)];
                    if (track.IsChannel) continue;
                    if (track.IsObject) m_Clip.ObjectCurves[static_cast<size_t>(track.Index)].Keys.erase(m_Clip.ObjectCurves[static_cast<size_t>(track.Index)].Keys.begin() + k);
                    else m_Clip.Curves[static_cast<size_t>(track.Index)].Keys.erase(m_Clip.Curves[static_cast<size_t>(track.Index)].Keys.begin() + k);
                }
                m_SelectedKeys.clear();
                CommitClip(ctx, "Delete Keyframes");
            }
            ImGui::EndPopup();
        }

        // Sprites dropped on the timeline -> sprite keyframes from the frame under the cursor.
        // A whole Multiple-mode texture adds all of its sprites in order (a flipbook).
        if (!IsReadOnly() && ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                const std::string reference(static_cast<const char*>(payload->Data));
                if (SpriteAssetUI::IsSpriteReference(reference)) {
                    std::vector<std::string> sprites;
                    if (!SpriteRef::IsSubSprite(reference)) {
                        for (const auto& entry : SpriteAssetUI::ListSprites(AssetDatabase::ResolvePath(reference), reference))
                            sprites.push_back(entry.Reference);
                    }
                    if (sprites.empty())
                        sprites.push_back(reference);
                    const int frameAtCursor = std::max(0, static_cast<int>(std::lround(XToTime(mouse.x) / frameDuration)));
                    AddSpriteFrames(ctx, sprites, frameAtCursor);
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    void AnimationWindow::DrawCurves(EditorContext& ctx, float headerHeight) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        m_AreaX = origin.x + 40.0f;
        m_AreaWidth = std::max(50.0f, avail.x - 50.0f);
        ImGuiIO& io = ImGui::GetIO();
        ImGui::InvisibleButton("##Curves", ImVec2(std::max(1.0f, avail.x), std::max(1.0f, avail.y)));
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 mouse = io.MousePos;
        if (hovered && io.MouseWheel != 0.0f) {
            const float t = XToTime(mouse.x);
            m_PixelsPerSecond = std::clamp(m_PixelsPerSecond * std::pow(1.15f, io.MouseWheel), 20.0f, 4000.0f);
            m_ViewStart = std::max(0.0f, t - (mouse.x - m_AreaX) / m_PixelsPerSecond);
        }

        // Which curves: the selected float track, else every float curve.
        std::vector<int> curves;
        if (m_SelectedTrack >= 0 && m_SelectedTrack < static_cast<int>(m_Tracks.size()) &&
            !m_Tracks[static_cast<size_t>(m_SelectedTrack)].IsObject && !m_Tracks[static_cast<size_t>(m_SelectedTrack)].IsChannel)
            curves.push_back(m_Tracks[static_cast<size_t>(m_SelectedTrack)].Index);
        else
            for (int i = 0; i < static_cast<int>(m_Clip.Curves.size()); ++i)
                curves.push_back(i);

        // Vertical range (frozen while dragging so the key does not run away).
        if (m_CurveDragCurve < 0) {
            float lo = 0.0f, hi = 0.0f;
            bool any = false;
            for (int c : curves)
                for (const AnimationFloatKey& key : m_Clip.Curves[static_cast<size_t>(c)].Keys) {
                    lo = any ? std::min(lo, key.Value) : key.Value;
                    hi = any ? std::max(hi, key.Value) : key.Value;
                    any = true;
                }
            if (!any || hi - lo < 0.001f) { lo -= 1.0f; hi += 1.0f; }
            const float pad = (hi - lo) * 0.1f;
            m_CurveMin = lo - pad;
            m_CurveMax = hi + pad;
        }
        const float top = origin.y + headerHeight, bottom = origin.y + avail.y - 6.0f;
        auto valueToY = [&](float v) { return bottom - (v - m_CurveMin) / (m_CurveMax - m_CurveMin) * (bottom - top); };
        auto yToValue = [&](float y) { return m_CurveMin + (bottom - y) / (bottom - top) * (m_CurveMax - m_CurveMin); };

        draw->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(35, 35, 35, 255));
        for (int i = 0; i <= 4; ++i) {
            const float v = m_CurveMin + (m_CurveMax - m_CurveMin) * i / 4.0f;
            const float y = valueToY(v);
            draw->AddLine(ImVec2(m_AreaX, y), ImVec2(m_AreaX + m_AreaWidth, y), IM_COL32(60, 60, 60, 255));
            char text[32];
            std::snprintf(text, sizeof(text), "%.2f", v);
            draw->AddText(ImVec2(origin.x + 2.0f, y - 7.0f), IM_COL32(150, 150, 150, 255), text);
        }
        const float frameDuration = FrameDuration();

        int hitCurve = -1, hitKey = -1;
        for (size_t n = 0; n < curves.size(); ++n) {
            const AnimationFloatCurve& curve = m_Clip.Curves[static_cast<size_t>(curves[n])];
            const ImU32 color = kCurveColors[n % 6];
            // Sampled polyline.
            ImVec2 previous;
            const int steps = static_cast<int>(m_AreaWidth / 3.0f);
            for (int s = 0; s <= steps; ++s) {
                const float x = m_AreaX + m_AreaWidth * s / steps;
                const ImVec2 p(x, valueToY(AnimationClipLoader::Sample(curve, std::max(0.0f, XToTime(x)), 0.0f)));
                if (s > 0)
                    draw->AddLine(previous, p, color, 1.5f);
                previous = p;
            }
            for (size_t k = 0; k < curve.Keys.size(); ++k) {
                const ImVec2 p(TimeToX(curve.Keys[k].Time), valueToY(curve.Keys[k].Value));
                const bool active = m_CurveDragCurve == curves[n] && m_CurveDragKey == static_cast<int>(k);
                draw->AddCircleFilled(p, 4.5f, active ? kKeySelected : color);
                if (std::fabs(mouse.x - p.x) <= 6.0f && std::fabs(mouse.y - p.y) <= 6.0f) {
                    hitCurve = curves[n];
                    hitKey = static_cast<int>(k);
                }
            }
            draw->AddText(ImVec2(m_AreaX + 6.0f, top + 4.0f + 16.0f * n), color, (curve.Component + "." + curve.Property).c_str());
        }
        draw->AddLine(ImVec2(TimeToX(m_Time), origin.y), ImVec2(TimeToX(m_Time), origin.y + avail.y), kPlayheadColor, 1.5f);

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hitCurve >= 0 && !IsReadOnly()) {
                m_CurveDragCurve = hitCurve;
                m_CurveDragKey = hitKey;
            } else if (mouse.y < top) {
                m_Scrubbing = true;
                SetPreview(ctx, true);
            }
        }
        if (m_Scrubbing) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                m_Time = std::max(0.0f, std::round(XToTime(mouse.x) / frameDuration) * frameDuration);
            else
                m_Scrubbing = false;
        }
        if (m_CurveDragCurve >= 0) {
            AnimationFloatCurve& curve = m_Clip.Curves[static_cast<size_t>(m_CurveDragCurve)];
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (m_CurveDragKey >= 0 && m_CurveDragKey < static_cast<int>(curve.Keys.size())) {
                    AnimationFloatKey& key = curve.Keys[static_cast<size_t>(m_CurveDragKey)];
                    key.Time = std::max(0.0f, std::round(XToTime(mouse.x) / frameDuration) * frameDuration);
                    key.Value = io.KeyCtrl ? std::round(yToValue(mouse.y) * 10.0f) / 10.0f : yToValue(mouse.y);
                }
            } else {
                // Keep the dragged key's identity through the re-sort, then save.
                std::stable_sort(curve.Keys.begin(), curve.Keys.end(), [](const auto& a, const auto& b) { return a.Time < b.Time; });
                m_CurveDragCurve = -1;
                m_CurveDragKey = -1;
                CommitClip(ctx, "Edit Curve");
            }
        }
    }

    // ---- Automation ----------------------------------------------------------------------

    namespace {
        bool AnimationAutomation(EditorContext& ctx, const std::vector<std::string>& args, AutomationApi& api) {
            const std::string& command = args[0];
            if (command.rfind("animation-", 0) != 0 && command.rfind("expect-anim", 0) != 0)
                return false;
            auto arg = [&](size_t i) { return i < args.size() ? args[i] : std::string(); };
            auto argi = [&](size_t i) { return std::atoi(arg(i).c_str()); };
            auto argf = [&](size_t i) { return static_cast<float>(std::atof(arg(i).c_str())); };
            auto path = [&](size_t i) { return arg(i) == "." ? std::string() : arg(i); };
            AnimationWindow* window = AnimationWindow::Instance();
            if (!window) {
                api.Expect(false, command + ": no Animation window");
                return true;
            }
            const float frameTime = 1.0f / std::max(1.0f, window->Clip().SampleRate);
            if (command == "animation-open") {
                AnimationWindow::IsOpen() = true;
                api.Wait(2);
            } else if (command == "animation-close") {
                AnimationWindow::IsOpen() = false;
            } else if (command == "animation-create-clip") {
                api.Expect(window->CreateClip(ctx, arg(1)), "create animation clip " + arg(1));
            } else if (command == "animation-frame") {
                window->SetFrame(argi(1));
            } else if (command == "animation-preview") {
                window->SetPreview(ctx, arg(1) == "1");
            } else if (command == "animation-record") {
                window->SetRecording(ctx, arg(1) == "1");
            } else if (command == "animation-play") {
                window->SetPreview(ctx, true);
                window->SetPlaying(true);
            } else if (command == "animation-pause") {
                window->SetPlaying(false);
            } else if (command == "animation-view") {
                window->SetCurvesView(arg(1) == "curves");
            } else if (command == "animation-add-property") {
                window->AddProperty(ctx, path(1), arg(2), arg(3));
            } else if (command == "animation-key") {
                // animation-key <path|.> <component> <property> <frame> <value>
                window->SetFloatKey(ctx, path(1), arg(2), arg(3), argi(4) * frameTime, argf(5));
            } else if (command == "animation-delete-key") {
                window->DeleteKey(ctx, argi(1), argi(2));
            } else if (command == "animation-move-key") {
                window->MoveKey(ctx, argi(1), argi(2), argi(3) * frameTime);
            } else if (command == "animation-add-sprites") {
                // animation-add-sprites <frame> <ref|texture-asset-path>... (a path adds all its sprites)
                std::vector<std::string> sprites;
                for (size_t i = 2; i < args.size(); ++i) {
                    std::string reference = args[i];
                    const size_t colon = reference.rfind(':');
                    if (colon != std::string::npos && colon > 1 && reference.find('.') != std::string::npos) {
                        // "Sprites/Sheet.png:3" = one sprite of a texture
                        const std::string one = SpriteAssetUI::ReferenceForAsset(reference.substr(0, colon), std::atoi(reference.c_str() + colon + 1));
                        if (!one.empty())
                            sprites.push_back(one);
                        continue;
                    }
                    if (reference.find('.') != std::string::npos) { // an asset path
                        std::string full = reference;
                        if (auto project = Project::GetActive(); project && !std::filesystem::exists(full))
                            full = project->GetAssetsDirectory() + "/" + reference;
                        const std::string guid = AssetMeta::EnsureMetaFile(full);
                        AssetDatabase::Register(guid, full);
                        for (const auto& entry : SpriteAssetUI::ListSprites(full, guid))
                            sprites.push_back(entry.Reference);
                    } else {
                        sprites.push_back(reference);
                    }
                }
                window->AddSpriteFrames(ctx, sprites, argi(1));
            } else if (command == "expect-anim-tracks") {
                api.Expect(static_cast<int>(window->Tracks().size()) == argi(1), "animation tracks == " + arg(1) + " (actual " + std::to_string(window->Tracks().size()) + ")");
            } else if (command == "expect-anim-keys") {
                api.Expect(window->KeyCount(argi(1)) == argi(2), "track " + arg(1) + " keys == " + arg(2) + " (actual " + std::to_string(window->KeyCount(argi(1))) + ")");
            } else if (command == "expect-anim-key-frame") {
                // expect-anim-key-frame <track> <key> <frame>
                const int actual = window->KeyCount(argi(1)) > argi(2) ? static_cast<int>(std::lround(window->KeyTime(argi(1), argi(2)) / frameTime)) : -1;
                api.Expect(actual == argi(3), "track " + arg(1) + " key " + arg(2) + " at frame " + arg(3) + " (actual " + std::to_string(actual) + ")");
            } else if (command == "expect-anim-value") {
                // expect-anim-value <path|.> <component> <property> <value> : the live scene value
                float value = 0.0f;
                Entity target = AnimationPlayback::FindTarget(ctx.SceneRef, window->Root(), path(1));
                const bool ok = target && AnimationPlayback::ReadFloat(target, arg(2), arg(3), value) && std::fabs(value - argf(4)) < 0.01f;
                api.Expect(ok, "animated " + arg(2) + "." + arg(3) + " == " + arg(4) + " (actual " + std::to_string(value) + ")");
            } else if (command == "expect-anim-sprite") {
                // expect-anim-sprite <sprite name> : the root's Sprite Renderer shows that sprite
                std::string reference;
                Entity root = window->Root();
                const bool read = root && AnimationPlayback::ReadObject(root, "Sprite Renderer", "Texture", reference);
                const std::string name = read ? SpriteAssetUI::DisplayName(reference) : std::string();
                api.Expect(name == arg(1), "animated sprite == " + arg(1) + " (actual " + name + ")");
            } else if (command == "expect-anim-preview") {
                api.Expect(window->IsPreviewing() == (arg(1) == "1"), "animation preview == " + arg(1));
            } else if (command == "expect-anim-clip-file-keys") {
                // Reads the .anim from disk (verifies the window saved it).
                AnimationClipData disk;
                std::ifstream file(window->ClipPath());
                const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                AnimationClipLoader::ParseJson(text, disk);
                size_t keys = 0;
                for (const auto& curve : disk.Curves) keys += curve.Keys.size();
                for (const auto& curve : disk.ObjectCurves) keys += curve.Keys.size();
                api.Expect(static_cast<int>(keys) == argi(1), "keys in .anim file == " + arg(1) + " (actual " + std::to_string(keys) + ")");
            } else {
                api.Expect(false, "unknown animation command '" + command + "'");
            }
            return true;
        }

        const bool s_AutomationRegistered = (EditorAutomation::RegisterExtension(&AnimationAutomation), true);
    }

}
