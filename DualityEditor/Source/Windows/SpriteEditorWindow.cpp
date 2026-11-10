#include "DualityEditor/Windows/SpriteEditorWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include <GL/glew.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <stb_image.h>

#include "DualityEditor/BuildPipeline.h"
#include "DualityEditor/EditorAutomation.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/EditorIcons.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEditor/ThumbnailCache.h"
#include "DualityEditor/Windows/SpriteAssetUI.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Project/Project.h"
#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    namespace {
        SpriteEditorWindow* s_Instance = nullptr;
        std::string s_RequestedPath;
        std::string s_ChangedOnDisk;

        struct PivotPreset { const char* Name; glm::vec2 Value; };
        // Y-down: "Top" is y = 0 (SpriteRenderer::Pivot convention).
        const PivotPreset kPivotPresets[] = {
            { "Center", { 0.5f, 0.5f } }, { "Top Left", { 0.0f, 0.0f } }, { "Top", { 0.5f, 0.0f } },
            { "Top Right", { 1.0f, 0.0f } }, { "Left", { 0.0f, 0.5f } }, { "Right", { 1.0f, 0.5f } },
            { "Bottom Left", { 0.0f, 1.0f } }, { "Bottom", { 0.5f, 1.0f } }, { "Bottom Right", { 1.0f, 1.0f } },
        };
        constexpr int kCustomPivot = 9;

        int PivotPresetIndex(const glm::vec2& pivot) {
            for (int i = 0; i < 9; ++i)
                if (glm::all(glm::lessThan(glm::abs(kPivotPresets[i].Value - pivot), glm::vec2(0.0001f))))
                    return i;
            return kCustomPivot;
        }

        bool IsImagePath(const std::string& path) {
            return !path.empty() && ThumbnailCache::IsImageFile(std::filesystem::path(path).extension().string());
        }

        std::string ResolveProjectPath(const std::string& path) {
            if (path.empty() || std::filesystem::path(path).is_absolute() || std::filesystem::exists(path))
                return path;
            if (auto project = Project::GetActive()) {
                const std::string candidate = project->GetAssetsDirectory() + "/" + path;
                if (std::filesystem::exists(candidate))
                    return candidate;
            }
            return path;
        }

        // Unity's Sprite Editor colours: unselected rects white-ish, selected blue, border green.
        const ImU32 kRectColor = IM_COL32(255, 255, 255, 150);
        const ImU32 kSelectedColor = IM_COL32(64, 160, 255, 255);
        const ImU32 kBorderColor = IM_COL32(80, 230, 90, 255);
        const ImU32 kPivotColor = IM_COL32(64, 160, 255, 255);
    }

    // ---- Lifetime / statics ------------------------------------------------------------------

    SpriteEditorWindow::SpriteEditorWindow() {
        s_Instance = this;
    }

    SpriteEditorWindow::~SpriteEditorWindow() {
        ReleaseTexture();
        if (s_Instance == this)
            s_Instance = nullptr;
    }

    bool& SpriteEditorWindow::IsOpen() {
        static bool open = false;
        return open;
    }

    void SpriteEditorWindow::Open(const std::string& texturePath) {
        IsOpen() = true;
        s_RequestedPath = texturePath;
    }

    void SpriteEditorWindow::NotifyImportSettingsChanged(const std::string& texturePath) {
        s_ChangedOnDisk = texturePath;
    }

    SpriteEditorWindow* SpriteEditorWindow::Instance() {
        return s_Instance;
    }

    // ---- Data ----------------------------------------------------------------------------------

    bool SpriteEditorWindow::IsDirty() const {
        return !m_TexturePath.empty() && !m_Working.SpriteDataEquals(m_Original);
    }

    void SpriteEditorWindow::ReleaseTexture() {
        if (m_GLTexture) {
            GLuint id = m_GLTexture;
            glDeleteTextures(1, &id);
            m_GLTexture = 0;
        }
        m_Pixels.clear();
        m_Width = m_Height = 0;
    }

    bool SpriteEditorWindow::LoadTexture(const std::string& path) {
        ReleaseTexture();
        m_TexturePath.clear();
        m_Selected = -1;
        m_UndoStack.clear();
        m_RedoStack.clear();
        int width = 0, height = 0, channels = 0;
        stbi_set_flip_vertically_on_load(0);
        unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
        if (!data) {
            Log::Warn("Sprite Editor: could not load '" + path + "'");
            return false;
        }
        m_Width = width;
        m_Height = height;
        m_Pixels.assign(data, data + static_cast<size_t>(width) * height * 4);
        stbi_image_free(data);

        // Nearest filtering: the editor zooms pixel art a lot, rects must line up with texels.
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_Pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        m_GLTexture = texture;

        m_TexturePath = path;
        m_DisplayName = std::filesystem::path(path).filename().string();
        AssetMeta::EnsureMetaFile(path);
        m_Original = TextureImportSettings::Load(path);
        m_Working = m_Original;
        m_Committed = m_Working;
        m_NeedsFrame = true;
        if (m_Working.SpriteMode == SpriteImportMode::Single || !m_Working.Sprites.empty())
            m_Selected = m_Working.SpriteMode == SpriteImportMode::Single ? 0 : -1;
        return true;
    }

    int SpriteEditorWindow::SpriteCount() const {
        if (m_TexturePath.empty())
            return 0;
        return m_Working.SpriteMode == SpriteImportMode::Single ? 1 : static_cast<int>(m_Working.Sprites.size());
    }

    SpriteDefinition SpriteEditorWindow::GetSprite(int index) const {
        if (m_Working.SpriteMode == SpriteImportMode::Single) {
            SpriteDefinition whole;
            whole.Id = "";
            whole.Name = std::filesystem::path(m_TexturePath).stem().string();
            whole.Rect = { 0.0f, 0.0f, static_cast<float>(m_Width), static_cast<float>(m_Height) };
            whole.Pivot = m_Working.SpritePivot;
            whole.Border = m_Working.SpriteBorder;
            return whole;
        }
        if (index < 0 || index >= static_cast<int>(m_Working.Sprites.size()))
            return {};
        return m_Working.Sprites[static_cast<size_t>(index)];
    }

    void SpriteEditorWindow::SetSprite(int index, const SpriteDefinition& sprite) {
        if (m_Working.SpriteMode == SpriteImportMode::Single) {
            if (sprite.Pivot != m_Working.SpritePivot) {
                m_Working.SpritePivot = sprite.Pivot;
                m_Working.HasSpritePivot = true;
            }
            m_Working.SpriteBorder = sprite.Border;
            return;
        }
        if (index < 0 || index >= static_cast<int>(m_Working.Sprites.size()))
            return;
        SpriteDefinition clamped = sprite;
        clamped.Rect.z = std::max(1.0f, clamped.Rect.z);
        clamped.Rect.w = std::max(1.0f, clamped.Rect.w);
        // A border can never exceed its rect.
        clamped.Border = glm::max(clamped.Border, glm::vec4(0.0f));
        clamped.Border.x = std::min(clamped.Border.x, clamped.Rect.z);
        clamped.Border.y = std::min(clamped.Border.y, clamped.Rect.z - clamped.Border.x);
        clamped.Border.z = std::min(clamped.Border.z, clamped.Rect.w);
        clamped.Border.w = std::min(clamped.Border.w, clamped.Rect.w - clamped.Border.z);
        m_Working.Sprites[static_cast<size_t>(index)] = clamped;
    }

    int SpriteEditorWindow::AddSprite(const glm::ivec4& rect) {
        if (m_TexturePath.empty())
            return -1;
        m_Working.SpriteMode = SpriteImportMode::Multiple;
        SpriteDefinition sprite;
        sprite.Id = SpriteSlicer::NewSpriteId(m_Working.Sprites);
        const std::string stem = std::filesystem::path(m_TexturePath).stem().string();
        int suffix = static_cast<int>(m_Working.Sprites.size());
        auto nameUsed = [&](const std::string& name) {
            return std::any_of(m_Working.Sprites.begin(), m_Working.Sprites.end(), [&](const SpriteDefinition& s) { return s.Name == name; });
        };
        while (nameUsed(stem + "_" + std::to_string(suffix)))
            ++suffix;
        sprite.Name = stem + "_" + std::to_string(suffix);
        sprite.Rect = { static_cast<float>(rect.x), static_cast<float>(rect.y), static_cast<float>(std::max(1, rect.z)), static_cast<float>(std::max(1, rect.w)) };
        m_Working.Sprites.push_back(sprite);
        m_Selected = static_cast<int>(m_Working.Sprites.size()) - 1;
        return m_Selected;
    }

    void SpriteEditorWindow::Select(int index) {
        m_Selected = (index >= 0 && index < SpriteCount()) ? index : -1;
    }

    void SpriteEditorWindow::DeleteSelected() {
        if (m_Working.SpriteMode != SpriteImportMode::Multiple || m_Selected < 0 || m_Selected >= static_cast<int>(m_Working.Sprites.size()))
            return;
        m_Working.Sprites.erase(m_Working.Sprites.begin() + m_Selected);
        m_Selected = -1;
    }

    void SpriteEditorWindow::TrimSelected() {
        if (m_Working.SpriteMode != SpriteImportMode::Multiple || m_Selected < 0 || m_Selected >= static_cast<int>(m_Working.Sprites.size()))
            return;
        SpriteDefinition sprite = m_Working.Sprites[static_cast<size_t>(m_Selected)];
        const int x0 = static_cast<int>(sprite.Rect.x), y0 = static_cast<int>(sprite.Rect.y);
        const int x1 = std::min(m_Width, x0 + static_cast<int>(sprite.Rect.z)), y1 = std::min(m_Height, y0 + static_cast<int>(sprite.Rect.w));
        int minX = x1, minY = y1, maxX = x0 - 1, maxY = y0 - 1;
        for (int y = std::max(0, y0); y < y1; ++y)
            for (int x = std::max(0, x0); x < x1; ++x)
                if (m_Pixels[(static_cast<size_t>(y) * m_Width + x) * 4 + 3] > 0) {
                    minX = std::min(minX, x); maxX = std::max(maxX, x);
                    minY = std::min(minY, y); maxY = std::max(maxY, y);
                }
        if (maxX < minX)
            return; // fully transparent: Unity leaves it untouched
        sprite.Rect = { static_cast<float>(minX), static_cast<float>(minY), static_cast<float>(maxX - minX + 1), static_cast<float>(maxY - minY + 1) };
        SetSprite(m_Selected, sprite);
    }

    int SpriteEditorWindow::Slice(const SliceOptions& options) {
        if (m_TexturePath.empty())
            return 0;
        const uint8_t* pixels = m_Pixels.empty() ? nullptr : m_Pixels.data();
        std::vector<SpriteSliceRect> rects;
        switch (options.Type) {
            case SliceType::Automatic:
                rects = SpriteSlicer::Automatic(pixels, m_Width, m_Height, options.MinimumSize);
                break;
            case SliceType::GridByCellSize:
                rects = SpriteSlicer::GridByCellSize(pixels, m_Width, m_Height, options.CellSize, options.Offset, options.Padding, options.KeepEmptyRects);
                break;
            case SliceType::GridByCellCount:
                rects = SpriteSlicer::GridByCellCount(pixels, m_Width, m_Height, options.CellCount.x, options.CellCount.y, options.Offset, options.Padding, options.KeepEmptyRects);
                break;
        }
        const std::string stem = std::filesystem::path(m_TexturePath).stem().string();
        m_Working.SpriteMode = SpriteImportMode::Multiple;
        if (options.Method == SliceMethod::Safe) {
            // Unity "Safe": keep every existing sprite, add only rects that overlap none of them.
            for (const SpriteSliceRect& rect : rects) {
                const bool overlaps = std::any_of(m_Working.Sprites.begin(), m_Working.Sprites.end(), [&](const SpriteDefinition& s) {
                    return rect.X < s.Rect.x + s.Rect.z && s.Rect.x < rect.X + rect.Width && rect.Y < s.Rect.y + s.Rect.w && s.Rect.y < rect.Y + rect.Height;
                });
                if (!overlaps) {
                    const int index = AddSprite({ rect.X, rect.Y, rect.Width, rect.Height });
                    m_Working.Sprites[static_cast<size_t>(index)].Pivot = options.Pivot;
                }
            }
        } else {
            // "Delete Existing" -- but a sprite whose generated name already existed keeps its
            // id, so references to "<tex>_3" survive a re-slice (Unity matches by name too).
            m_Working.Sprites = SpriteSlicer::MakeSprites(rects, stem, options.Pivot, m_Working.Sprites);
        }
        m_Selected = -1;
        CommitIfChanged();
        return static_cast<int>(rects.size());
    }

    void SpriteEditorWindow::CommitIfChanged() {
        if (m_Working.SpriteDataEquals(m_Committed))
            return;
        m_UndoStack.push_back(m_Committed);
        if (m_UndoStack.size() > 100)
            m_UndoStack.erase(m_UndoStack.begin());
        m_RedoStack.clear();
        m_Committed = m_Working;
    }

    void SpriteEditorWindow::Undo() {
        CommitIfChanged();
        if (m_UndoStack.empty())
            return;
        m_RedoStack.push_back(m_Working);
        m_Working = m_UndoStack.back();
        m_UndoStack.pop_back();
        m_Committed = m_Working;
        if (m_Selected >= SpriteCount())
            m_Selected = -1;
    }

    void SpriteEditorWindow::Redo() {
        if (m_RedoStack.empty())
            return;
        m_UndoStack.push_back(m_Working);
        m_Working = m_RedoStack.back();
        m_RedoStack.pop_back();
        m_Committed = m_Working;
        if (m_Selected >= SpriteCount())
            m_Selected = -1;
    }

    bool SpriteEditorWindow::Apply() {
        if (m_TexturePath.empty())
            return false;
        // Only the sprite part is ours: filter/wrap/mipmaps on disk may have been changed by the
        // Inspector meanwhile.
        TextureImportSettings disk = TextureImportSettings::Load(m_TexturePath);
        const bool hasSpriteData = m_Working.SpriteMode == SpriteImportMode::Multiple || !m_Working.Sprites.empty() ||
            m_Working.HasSpritePivot || m_Working.SpriteBorder != glm::vec4(0.0f);
        m_Working.Width = hasSpriteData ? m_Width : 0;
        m_Working.Height = hasSpriteData ? m_Height : 0;
        disk.SpriteMode = m_Working.SpriteMode;
        disk.PixelsPerUnit = m_Working.PixelsPerUnit;
        disk.Width = m_Working.Width;
        disk.Height = m_Working.Height;
        disk.HasSpritePivot = m_Working.HasSpritePivot;
        disk.SpritePivot = m_Working.SpritePivot;
        disk.SpriteBorder = m_Working.SpriteBorder;
        disk.Sprites = m_Working.Sprites;
        if (!TextureImportSettings::Save(m_TexturePath, disk)) {
            Log::Error("Sprite Editor: could not write '" + m_TexturePath + ".meta'");
            return false;
        }
        m_Original = m_Working;
        m_Committed = m_Working;
        SpriteDatabase::Invalidate();
        Log::Info("Sprite Editor: applied " + std::to_string(SpriteCount()) + " sprite(s) to " + m_DisplayName);
        return true;
    }

    void SpriteEditorWindow::Revert() {
        m_Working = m_Original;
        CommitIfChanged();
        m_Committed = m_Working;
        if (m_Selected >= SpriteCount())
            m_Selected = -1;
    }

    // ---- View ----------------------------------------------------------------------------------

    glm::vec2 SpriteEditorWindow::TextureToScreen(const glm::vec2& texel) const {
        return m_CanvasMin + m_CanvasSize * 0.5f + (texel - m_ViewCenter) * m_Zoom;
    }

    glm::vec2 SpriteEditorWindow::ScreenToTexture(const glm::vec2& screen) const {
        return (screen - m_CanvasMin - m_CanvasSize * 0.5f) / m_Zoom + m_ViewCenter;
    }

    void SpriteEditorWindow::FrameTexture() {
        if (m_Width <= 0 || m_Height <= 0 || m_CanvasSize.x <= 0.0f)
            return;
        m_ViewCenter = { m_Width * 0.5f, m_Height * 0.5f };
        const float fit = std::min((m_CanvasSize.x - 40.0f) / m_Width, (m_CanvasSize.y - 40.0f) / m_Height);
        m_Zoom = std::clamp(fit, 0.05f, 32.0f);
        m_NeedsFrame = false;
    }

    // ---- UI ------------------------------------------------------------------------------------

    void SpriteEditorWindow::OnImGuiRender(EditorContext& ctx) {
        bool& open = IsOpen();

        // Requests from the Inspector button / Window menu / automation.
        if (!s_RequestedPath.empty()) {
            const std::string requested = ResolveProjectPath(s_RequestedPath);
            s_RequestedPath.clear();
            if (requested != m_TexturePath) {
                if (IsDirty()) {
                    m_PendingOpenPath = requested;
                    m_OpenPendingPrompt = true;
                } else {
                    LoadTexture(requested);
                }
            }
        }
        if (!s_ChangedOnDisk.empty()) {
            if (s_ChangedOnDisk == m_TexturePath && !IsDirty()) {
                const int selected = m_Selected;
                m_Original = TextureImportSettings::Load(m_TexturePath);
                m_Working = m_Original;
                m_Committed = m_Working;
                m_Selected = selected < SpriteCount() ? selected : -1;
            }
            s_ChangedOnDisk.clear();
        }

        if (!open) {
            if (m_Focused && ctx.Session.FocusedPanel == EditorPanel::Other) {
                ctx.Session.FocusedPanel = EditorPanel::None;
                ctx.Session.FocusedWindowUndo = nullptr;
            }
            m_Focused = false;
            return;
        }

        // Unity: the Sprite Editor follows the Project selection (asking first when changes
        // are unapplied).
        static std::string s_LastSelection;
        if (ctx.SelectedAssetPath != s_LastSelection) {
            s_LastSelection = ctx.SelectedAssetPath;
            if (IsImagePath(s_LastSelection) && s_LastSelection != m_TexturePath)
                Open(s_LastSelection);
        }

        ImGui::SetNextWindowSize(ImVec2(900.0f, 640.0f), ImGuiCond_FirstUseEver);
        const std::string title = std::string("Sprite Editor") + (IsDirty() ? " *" : "") + "###Sprite Editor";
        if (!ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            ImGui::End();
            return;
        }
        if (!open && IsDirty()) {
            // Closing with unapplied changes: Unity asks Apply / Revert.
            open = true;
            m_PendingOpenPath.clear();
            m_OpenPendingPrompt = true;
        }

        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (focused) {
            ctx.Session.FocusedPanel = EditorPanel::Other;
            ctx.Session.FocusedWindowUndo = [this](bool redo) { if (redo) Redo(); else Undo(); };
        } else if (m_Focused && ctx.Session.FocusedPanel == EditorPanel::Other) {
            ctx.Session.FocusedWindowUndo = nullptr;
        }
        m_Focused = focused;

        if (m_TexturePath.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("No texture selected.");
            ImGui::TextWrapped("Select a texture in the Project window (or press \"Sprite Editor\" in its Inspector) to slice it into sprites.");
            if (IsImagePath(ctx.SelectedAssetPath) && ImGui::Button(("Edit " + std::filesystem::path(ctx.SelectedAssetPath).filename().string()).c_str()))
                LoadTexture(ctx.SelectedAssetPath);
            DrawPendingSwitchPrompt();
            ImGui::End();
            return;
        }

        DrawToolbar(ctx);
        DrawCanvas();
        DrawSpriteProperties();

        // Keyboard (Unity: Delete removes the selected sprite).
        if (focused && !ImGui::GetIO().WantTextInput) {
            if ((ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) && m_Drag == DragMode::None)
                DeleteSelected();
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                m_Selected = -1;
            if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !ImGui::GetIO().KeyCtrl)
                m_NeedsFrame = true;
        }

        // One undo step per finished interaction (drag / typed value / slice).
        if (!ImGui::IsAnyItemActive() && m_Drag == DragMode::None)
            CommitIfChanged();

        DrawPendingSwitchPrompt();
        ImGui::End();
    }

    void SpriteEditorWindow::DrawToolbar(EditorContext& ctx) {
        (void)ctx;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(m_DisplayName.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%dx%d)", m_Width, m_Height);

        ImGui::SameLine(0.0f, 16.0f);
        int mode = m_Working.SpriteMode == SpriteImportMode::Multiple ? 1 : 0;
        const char* modes[] = { "Single", "Multiple" };
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Combo("##SpriteMode", &mode, modes, 2)) {
            m_Working.SpriteMode = mode == 1 ? SpriteImportMode::Multiple : SpriteImportMode::Single;
            m_Selected = mode == 0 ? 0 : -1;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Sprite Mode (also in the texture's Inspector)");

        ImGui::SameLine();
        const bool multiple = m_Working.SpriteMode == SpriteImportMode::Multiple;
        ImGui::BeginDisabled(!multiple);
        if (ImGui::Button("Slice " "\xEF\x83\x97"))
            ImGui::OpenPopup("SlicePopup");
        ImGui::SameLine();
        ImGui::BeginDisabled(m_Selected < 0);
        if (ImGui::Button("Trim"))
            TrimSelected();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Shrink the selected sprite to its opaque pixels");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        DrawSlicePopup();

        ImGui::SameLine(0.0f, 16.0f);
        ImGui::TextDisabled("%d sprite(s)", multiple ? static_cast<int>(m_Working.Sprites.size()) : 1);

        // Right side: zoom, Revert, Apply (Unity's order).
        const float applyWidth = 70.0f, revertWidth = 70.0f, zoomWidth = 120.0f;
        const float right = ImGui::GetWindowContentRegionMax().x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, right - applyWidth - revertWidth - zoomWidth - ImGui::GetStyle().ItemSpacing.x * 2.0f));
        ImGui::SetNextItemWidth(zoomWidth);
        float zoomPercent = m_Zoom * 100.0f;
        if (ImGui::SliderFloat("##Zoom", &zoomPercent, 5.0f, 3200.0f, "%.0f%%", ImGuiSliderFlags_Logarithmic))
            m_Zoom = zoomPercent / 100.0f;
        ImGui::SameLine();
        ImGui::BeginDisabled(!IsDirty());
        if (ImGui::Button("Revert", ImVec2(revertWidth, 0.0f)))
            Revert();
        ImGui::SameLine();
        if (ImGui::Button("Apply", ImVec2(applyWidth, 0.0f)))
            Apply();
        ImGui::EndDisabled();

        if (m_Original.Width > 0 && (m_Original.Width != m_Width || m_Original.Height != m_Height)) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s Image is %dx%d but sprites were authored for %dx%d -- Apply to update.",
                EditorIcons::Warning, m_Width, m_Height, m_Original.Width, m_Original.Height);
        }
    }

    void SpriteEditorWindow::DrawSlicePopup() {
        if (!ImGui::BeginPopup("SlicePopup"))
            return;
        SliceOptions& o = m_SliceOptions;
        ImGui::TextUnformatted("Slice");
        ImGui::Separator();
        int type = static_cast<int>(o.Type);
        const char* types[] = { "Automatic", "Grid By Cell Size", "Grid By Cell Count" };
        ImGui::SetNextItemWidth(180.0f);
        ImGui::Combo("Type", &type, types, 3);
        o.Type = static_cast<SliceType>(type);
        if (o.Type == SliceType::Automatic) {
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputInt("Minimum Size", &o.MinimumSize);
            o.MinimumSize = std::max(1, o.MinimumSize);
        } else {
            ImGui::SetNextItemWidth(180.0f);
            if (o.Type == SliceType::GridByCellSize) {
                ImGui::InputInt2("Pixel Size", &o.CellSize.x);
                o.CellSize = glm::max(o.CellSize, glm::ivec2(1));
            } else {
                ImGui::InputInt2("Column & Row", &o.CellCount.x);
                o.CellCount = glm::max(o.CellCount, glm::ivec2(1));
            }
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputInt2("Offset", &o.Offset.x);
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputInt2("Padding", &o.Padding.x);
            o.Offset = glm::max(o.Offset, glm::ivec2(0));
            o.Padding = glm::max(o.Padding, glm::ivec2(0));
            ImGui::Checkbox("Keep Empty Rects", &o.KeepEmptyRects);
        }
        int pivot = PivotPresetIndex(o.Pivot);
        const char* pivotNames[] = { "Center", "Top Left", "Top", "Top Right", "Left", "Right", "Bottom Left", "Bottom", "Bottom Right", "Custom" };
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::Combo("Pivot", &pivot, pivotNames, 10) && pivot < kCustomPivot)
            o.Pivot = kPivotPresets[pivot].Value;
        if (pivot == kCustomPivot) {
            ImGui::SetNextItemWidth(180.0f);
            ImGui::DragFloat2("Custom Pivot", &o.Pivot.x, 0.01f);
        }
        int method = static_cast<int>(o.Method);
        const char* methods[] = { "Delete Existing", "Safe" };
        ImGui::SetNextItemWidth(180.0f);
        ImGui::Combo("Method", &method, methods, 2);
        o.Method = static_cast<SliceMethod>(method);
        ImGui::Spacing();
        if (ImGui::Button("Slice", ImVec2(-1.0f, 0.0f))) {
            Slice(o);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void SpriteEditorWindow::DrawCanvas() {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        m_CanvasMin = { canvasPos.x, canvasPos.y };
        m_CanvasSize = { std::max(available.x, 50.0f), std::max(available.y, 50.0f) };
        if (m_NeedsFrame)
            FrameTexture();

        ImGui::InvisibleButton("##SpriteCanvas", ImVec2(m_CanvasSize.x, m_CanvasSize.y),
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
        ImGuiIO& io = ImGui::GetIO();
        const glm::vec2 mouse{ io.MousePos.x, io.MousePos.y };
        const glm::vec2 mouseTexel = ScreenToTexture(mouse);

        // ---- Zoom (wheel, around the cursor) / pan (MMB, or Alt+LMB) -------------------------
        if (hovered && io.MouseWheel != 0.0f) {
            const float factor = std::pow(1.15f, io.MouseWheel);
            const float newZoom = std::clamp(m_Zoom * factor, 0.05f, 32.0f);
            // Keep the texel under the cursor fixed.
            m_ViewCenter = mouseTexel - (mouse - m_CanvasMin - m_CanvasSize * 0.5f) / newZoom;
            m_Zoom = newZoom;
        }
        if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || (io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)))) {
            m_Drag = DragMode::Pan;
            m_DragStartMouse = mouse;
        }
        if (m_Drag == DragMode::Pan) {
            const bool held = ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Left);
            if (held) {
                m_ViewCenter -= (mouse - m_DragStartMouse) / m_Zoom;
                m_DragStartMouse = mouse;
            } else {
                m_Drag = DragMode::None;
            }
        }

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(canvasPos, ImVec2(canvasPos.x + m_CanvasSize.x, canvasPos.y + m_CanvasSize.y), true);
        draw->AddRectFilled(canvasPos, ImVec2(canvasPos.x + m_CanvasSize.x, canvasPos.y + m_CanvasSize.y), IM_COL32(40, 40, 40, 255));

        // Checkerboard behind the texture so transparency reads like Unity.
        const glm::vec2 texMin = TextureToScreen({ 0.0f, 0.0f });
        const glm::vec2 texMax = TextureToScreen({ static_cast<float>(m_Width), static_cast<float>(m_Height) });
        {
            const float cell = 10.0f;
            const glm::vec2 clipMin = glm::max(texMin, m_CanvasMin);
            const glm::vec2 clipMax = glm::min(texMax, m_CanvasMin + m_CanvasSize);
            for (float y = texMin.y; y < clipMax.y; y += cell) {
                if (y + cell < clipMin.y)
                    continue;
                for (float x = texMin.x; x < clipMax.x; x += cell) {
                    if (x + cell < clipMin.x)
                        continue;
                    const bool dark = (static_cast<int>((x - texMin.x) / cell) + static_cast<int>((y - texMin.y) / cell)) % 2 == 0;
                    draw->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + cell, texMax.x), std::min(y + cell, texMax.y)),
                        dark ? IM_COL32(100, 100, 100, 255) : IM_COL32(130, 130, 130, 255));
                }
            }
        }
        draw->AddImage(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(m_GLTexture)), ImVec2(texMin.x, texMin.y), ImVec2(texMax.x, texMax.y));
        draw->AddRect(ImVec2(texMin.x - 1.0f, texMin.y - 1.0f), ImVec2(texMax.x + 1.0f, texMax.y + 1.0f), IM_COL32(0, 0, 0, 160));

        const bool multiple = m_Working.SpriteMode == SpriteImportMode::Multiple;
        const int count = SpriteCount();

        // ---- Hit testing ---------------------------------------------------------------------
        auto rectScreen = [&](const SpriteDefinition& s, glm::vec2& a, glm::vec2& b) {
            a = TextureToScreen({ s.Rect.x, s.Rect.y });
            b = TextureToScreen({ s.Rect.x + s.Rect.z, s.Rect.y + s.Rect.w });
        };
        const float handleRadius = 6.0f;
        auto isNear = [&](const glm::vec2& p) { return glm::length(mouse - p) <= handleRadius + 1.0f; };

        if (hovered && m_Drag == DragMode::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt) {
            bool handled = false;
            if (m_Selected >= 0 && m_Selected < count) {
                const SpriteDefinition s = GetSprite(m_Selected);
                glm::vec2 a, b;
                rectScreen(s, a, b);
                const glm::vec2 pivot = a + (b - a) * s.Pivot;
                const float bx0 = a.x + s.Border.x * m_Zoom, bx1 = b.x - s.Border.y * m_Zoom;
                const float by0 = a.y + s.Border.z * m_Zoom, by1 = b.y - s.Border.w * m_Zoom;
                const float midX = (a.x + b.x) * 0.5f, midY = (a.y + b.y) * 0.5f;
                if (isNear(pivot)) {
                    m_Drag = DragMode::Pivot;
                } else if (isNear({ bx0, midY }) && s.Border.x > 0.0f) { m_Drag = DragMode::Border; m_DragHandle = 0; }
                else if (isNear({ bx1, midY }) && s.Border.y > 0.0f) { m_Drag = DragMode::Border; m_DragHandle = 1; }
                else if (isNear({ midX, by0 }) && s.Border.z > 0.0f) { m_Drag = DragMode::Border; m_DragHandle = 2; }
                else if (isNear({ midX, by1 }) && s.Border.w > 0.0f) { m_Drag = DragMode::Border; m_DragHandle = 3; }
                if (m_Drag == DragMode::None) {
                    // Resize handles: corners + edge midpoints (Multiple only); border handles
                    // sit on the rect edge when a border is 0 -- Ctrl+drag an edge to set one.
                    struct Handle { glm::vec2 P; int Mask; };
                    const Handle handles[] = {
                        { a, 1 | 4 }, { { b.x, a.y }, 2 | 4 }, { { a.x, b.y }, 1 | 8 }, { b, 2 | 8 },
                        { { a.x, midY }, 1 }, { { b.x, midY }, 2 }, { { midX, a.y }, 4 }, { { midX, b.y }, 8 },
                    };
                    for (const Handle& h : handles) {
                        if (!isNear(h.P))
                            continue;
                        if (io.KeyCtrl || !multiple) {
                            // Border from an edge (Unity: drag the green dot that sits on the edge).
                            const int edge = (h.Mask == 1) ? 0 : (h.Mask == 2) ? 1 : (h.Mask == 4) ? 2 : (h.Mask == 8) ? 3 : -1;
                            if (edge >= 0) { m_Drag = DragMode::Border; m_DragHandle = edge; }
                        } else {
                            m_Drag = DragMode::Resize;
                            m_DragHandle = h.Mask;
                        }
                        break;
                    }
                }
                if (m_Drag == DragMode::None && multiple && mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y)
                    m_Drag = DragMode::Move;
                if (m_Drag != DragMode::None) {
                    handled = true;
                    m_DragStartSprite = s;
                    m_DragStartTexel = mouseTexel;
                }
            }
            if (!handled) {
                // Select the smallest rect under the cursor (nested rects stay pickable).
                int best = -1;
                float bestArea = 0.0f;
                for (int i = 0; i < count; ++i) {
                    const SpriteDefinition s = GetSprite(i);
                    if (mouseTexel.x >= s.Rect.x && mouseTexel.x <= s.Rect.x + s.Rect.z && mouseTexel.y >= s.Rect.y && mouseTexel.y <= s.Rect.y + s.Rect.w) {
                        const float area = s.Rect.z * s.Rect.w;
                        if (best < 0 || area < bestArea) { best = i; bestArea = area; }
                    }
                }
                if (best >= 0) {
                    m_Selected = best;
                    if (multiple) {
                        m_Drag = DragMode::Move;
                        m_DragStartSprite = GetSprite(best);
                        m_DragStartTexel = mouseTexel;
                    }
                } else if (multiple) {
                    // Drag on empty texture space draws a new sprite (Unity).
                    m_Selected = -1;
                    m_Drag = DragMode::Create;
                    m_DragStartTexel = glm::clamp(glm::floor(mouseTexel + 0.5f), glm::vec2(0.0f), glm::vec2(static_cast<float>(m_Width), static_cast<float>(m_Height)));
                } else {
                    m_Selected = -1;
                }
            }
        }

        // ---- Drag update -------------------------------------------------------------------
        const bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const glm::vec2 imageSize{ static_cast<float>(m_Width), static_cast<float>(m_Height) };
        if (m_Drag != DragMode::None && m_Drag != DragMode::Pan && m_Selected >= 0 && m_Selected < count) {
            SpriteDefinition s = m_DragStartSprite;
            const glm::vec2 delta = glm::round(mouseTexel - m_DragStartTexel);
            switch (m_Drag) {
                case DragMode::Move: {
                    s.Rect.x = std::clamp(m_DragStartSprite.Rect.x + delta.x, 0.0f, imageSize.x - s.Rect.z);
                    s.Rect.y = std::clamp(m_DragStartSprite.Rect.y + delta.y, 0.0f, imageSize.y - s.Rect.w);
                    break;
                }
                case DragMode::Resize: {
                    float x0 = s.Rect.x, y0 = s.Rect.y, x1 = s.Rect.x + s.Rect.z, y1 = s.Rect.y + s.Rect.w;
                    const glm::vec2 p = glm::clamp(glm::round(mouseTexel), glm::vec2(0.0f), imageSize);
                    if (m_DragHandle & 1) x0 = std::min(p.x, x1 - 1.0f);
                    if (m_DragHandle & 2) x1 = std::max(p.x, x0 + 1.0f);
                    if (m_DragHandle & 4) y0 = std::min(p.y, y1 - 1.0f);
                    if (m_DragHandle & 8) y1 = std::max(p.y, y0 + 1.0f);
                    s.Rect = { x0, y0, x1 - x0, y1 - y0 };
                    break;
                }
                case DragMode::Pivot: {
                    s.Pivot = (mouseTexel - glm::vec2(s.Rect.x, s.Rect.y)) / glm::vec2(s.Rect.z, s.Rect.w);
                    if (io.KeyCtrl) // snap to pixel corners
                        s.Pivot = glm::round(s.Pivot * glm::vec2(s.Rect.z, s.Rect.w)) / glm::vec2(s.Rect.z, s.Rect.w);
                    break;
                }
                case DragMode::Border: {
                    const glm::vec2 p = glm::round(mouseTexel);
                    if (m_DragHandle == 0) s.Border.x = std::clamp(p.x - s.Rect.x, 0.0f, s.Rect.z - s.Border.y);
                    if (m_DragHandle == 1) s.Border.y = std::clamp(s.Rect.x + s.Rect.z - p.x, 0.0f, s.Rect.z - s.Border.x);
                    if (m_DragHandle == 2) s.Border.z = std::clamp(p.y - s.Rect.y, 0.0f, s.Rect.w - s.Border.w);
                    if (m_DragHandle == 3) s.Border.w = std::clamp(s.Rect.y + s.Rect.w - p.y, 0.0f, s.Rect.w - s.Border.z);
                    break;
                }
                default:
                    break;
            }
            SetSprite(m_Selected, s);
            if (!leftDown)
                m_Drag = DragMode::None;
        }
        glm::vec2 createA{ 0.0f }, createB{ 0.0f };
        if (m_Drag == DragMode::Create) {
            const glm::vec2 p = glm::clamp(glm::floor(mouseTexel + 0.5f), glm::vec2(0.0f), imageSize);
            createA = glm::min(m_DragStartTexel, p);
            createB = glm::max(m_DragStartTexel, p);
            if (!leftDown) {
                m_Drag = DragMode::None;
                if (createB.x - createA.x >= 1.0f && createB.y - createA.y >= 1.0f)
                    AddSprite({ static_cast<int>(createA.x), static_cast<int>(createA.y), static_cast<int>(createB.x - createA.x), static_cast<int>(createB.y - createA.y) });
            }
        }
        if (m_Drag != DragMode::None && m_Drag != DragMode::Pan && !leftDown)
            m_Drag = DragMode::None;

        // ---- Drawing ----------------------------------------------------------------------------
        for (int i = 0; i < count; ++i) {
            if (i == m_Selected)
                continue;
            const SpriteDefinition s = GetSprite(i);
            glm::vec2 a, b;
            rectScreen(s, a, b);
            draw->AddRect(ImVec2(a.x, a.y), ImVec2(b.x, b.y), kRectColor);
        }
        if (m_Selected >= 0 && m_Selected < count) {
            const SpriteDefinition s = GetSprite(m_Selected);
            glm::vec2 a, b;
            rectScreen(s, a, b);
            // Border (green) lines + their handles.
            const float bx0 = a.x + s.Border.x * m_Zoom, bx1 = b.x - s.Border.y * m_Zoom;
            const float by0 = a.y + s.Border.z * m_Zoom, by1 = b.y - s.Border.w * m_Zoom;
            const float midX = (a.x + b.x) * 0.5f, midY = (a.y + b.y) * 0.5f;
            if (s.Border.x > 0.0f) draw->AddLine(ImVec2(bx0, a.y), ImVec2(bx0, b.y), kBorderColor);
            if (s.Border.y > 0.0f) draw->AddLine(ImVec2(bx1, a.y), ImVec2(bx1, b.y), kBorderColor);
            if (s.Border.z > 0.0f) draw->AddLine(ImVec2(a.x, by0), ImVec2(b.x, by0), kBorderColor);
            if (s.Border.w > 0.0f) draw->AddLine(ImVec2(a.x, by1), ImVec2(b.x, by1), kBorderColor);
            draw->AddRect(ImVec2(a.x, a.y), ImVec2(b.x, b.y), kSelectedColor, 0.0f, 0, 2.0f);
            // Resize handles (Multiple) -- Single mode's rect is the whole texture.
            if (multiple) {
                const glm::vec2 handles[] = { a, { b.x, a.y }, { a.x, b.y }, b, { a.x, midY }, { b.x, midY }, { midX, a.y }, { midX, b.y } };
                for (const glm::vec2& h : handles) {
                    draw->AddRectFilled(ImVec2(h.x - 3.5f, h.y - 3.5f), ImVec2(h.x + 3.5f, h.y + 3.5f), IM_COL32(255, 255, 255, 255));
                    draw->AddRect(ImVec2(h.x - 3.5f, h.y - 3.5f), ImVec2(h.x + 3.5f, h.y + 3.5f), kSelectedColor);
                }
            }
            const glm::vec2 borderHandles[] = { { bx0, midY }, { bx1, midY }, { midX, by0 }, { midX, by1 } };
            for (int i = 0; i < 4; ++i) {
                const float value = i == 0 ? s.Border.x : i == 1 ? s.Border.y : i == 2 ? s.Border.z : s.Border.w;
                if (value > 0.0f)
                    draw->AddRectFilled(ImVec2(borderHandles[i].x - 3.0f, borderHandles[i].y - 3.0f), ImVec2(borderHandles[i].x + 3.0f, borderHandles[i].y + 3.0f), kBorderColor);
            }
            // Pivot: blue ring like Unity.
            const glm::vec2 pivot = a + (b - a) * s.Pivot;
            draw->AddCircle(ImVec2(pivot.x, pivot.y), 5.0f, kPivotColor, 16, 2.0f);
            draw->AddCircleFilled(ImVec2(pivot.x, pivot.y), 2.0f, kPivotColor);
        }
        if (m_Drag == DragMode::Create) {
            const glm::vec2 a = TextureToScreen(createA), b = TextureToScreen(createB);
            draw->AddRect(ImVec2(a.x, a.y), ImVec2(b.x, b.y), kSelectedColor, 0.0f, 0, 1.5f);
        }
        // Cursor readout (texel under the mouse).
        if (hovered) {
            char text[64];
            std::snprintf(text, sizeof(text), "%d, %d", static_cast<int>(std::floor(mouseTexel.x)), static_cast<int>(std::floor(mouseTexel.y)));
            draw->AddText(ImVec2(canvasPos.x + 6.0f, canvasPos.y + m_CanvasSize.y - 20.0f), IM_COL32(200, 200, 200, 200), text);
        }
        draw->PopClipRect();
    }

    void SpriteEditorWindow::DrawSpriteProperties() {
        if (m_Selected < 0 || m_Selected >= SpriteCount())
            return;
        // Unity's "Sprite" box in the bottom-right corner of the canvas.
        const float width = 300.0f;
        const float height = ImGui::GetFrameHeightWithSpacing() * (m_Working.SpriteMode == SpriteImportMode::Multiple ? 8.0f : 6.0f) + 20.0f;
        ImGui::SetCursorScreenPos(ImVec2(m_CanvasMin.x + m_CanvasSize.x - width - 10.0f, m_CanvasMin.y + m_CanvasSize.y - height - 10.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.16f, 0.16f, 0.94f));
        ImGui::BeginChild("##SpriteProperties", ImVec2(width, height), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted("Sprite");
        ImGui::Separator();
        SpriteDefinition s = GetSprite(m_Selected);
        const SpriteDefinition before = s;
        const bool multiple = m_Working.SpriteMode == SpriteImportMode::Multiple;
        ImGui::PushItemWidth(-90.0f);

        char name[128];
        std::snprintf(name, sizeof(name), "%s", s.Name.c_str());
        ImGui::BeginDisabled(!multiple);
        if (ImGui::InputText("Name", name, sizeof(name)))
            s.Name = name;
        int position[2] = { static_cast<int>(s.Rect.x), static_cast<int>(s.Rect.y) };
        int size[2] = { static_cast<int>(s.Rect.z), static_cast<int>(s.Rect.w) };
        if (multiple) {
            if (ImGui::DragInt2("Position", position, 0.25f, 0, std::max(m_Width, m_Height))) {
                s.Rect.x = static_cast<float>(std::clamp(position[0], 0, std::max(0, m_Width - size[0])));
                s.Rect.y = static_cast<float>(std::clamp(position[1], 0, std::max(0, m_Height - size[1])));
            }
            if (ImGui::DragInt2("Size", size, 0.25f, 1, std::max(m_Width, m_Height))) {
                s.Rect.z = static_cast<float>(std::clamp(size[0], 1, m_Width - static_cast<int>(s.Rect.x)));
                s.Rect.w = static_cast<float>(std::clamp(size[1], 1, m_Height - static_cast<int>(s.Rect.y)));
            }
        }
        ImGui::EndDisabled();

        // Unity order in the box: L T R B. Storage is L R T B.
        int border[4] = { static_cast<int>(s.Border.x), static_cast<int>(s.Border.z), static_cast<int>(s.Border.y), static_cast<int>(s.Border.w) };
        if (ImGui::DragInt4("Border LTRB", border, 0.25f, 0, std::max(m_Width, m_Height)))
            s.Border = { static_cast<float>(border[0]), static_cast<float>(border[2]), static_cast<float>(border[1]), static_cast<float>(border[3]) };

        int preset = PivotPresetIndex(s.Pivot);
        const char* pivotNames[] = { "Center", "Top Left", "Top", "Top Right", "Left", "Right", "Bottom Left", "Bottom", "Bottom Right", "Custom" };
        if (ImGui::Combo("Pivot", &preset, pivotNames, 10) && preset < kCustomPivot)
            s.Pivot = kPivotPresets[preset].Value;
        ImGui::DragFloat2("Custom Pivot", &s.Pivot.x, 0.005f, -2.0f, 3.0f, "%.3f");
        ImGui::TextDisabled("Pivot px: %.1f, %.1f", s.Pivot.x * s.Rect.z, s.Pivot.y * s.Rect.w);
        ImGui::PopItemWidth();

        if (s != before)
            SetSprite(m_Selected, s);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void SpriteEditorWindow::DrawPendingSwitchPrompt() {
        if (m_OpenPendingPrompt) {
            ImGui::OpenPopup("Unapplied import settings");
            m_OpenPendingPrompt = false;
        }
        if (!ImGui::BeginPopupModal("Unapplied import settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::Text("Unapplied import settings for '%s'.", m_DisplayName.c_str());
        ImGui::Spacing();
        auto proceed = [&]() {
            if (!m_PendingOpenPath.empty())
                LoadTexture(m_PendingOpenPath);
            else
                IsOpen() = false; // the prompt came from closing the window
            m_PendingOpenPath.clear();
            ImGui::CloseCurrentPopup();
        };
        if (ImGui::Button("Apply", ImVec2(90.0f, 0.0f))) {
            Apply();
            proceed();
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert", ImVec2(90.0f, 0.0f))) {
            Revert();
            proceed();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
            m_PendingOpenPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ---- Automation (editor-test scripts) -------------------------------------------------

    namespace {
        bool SpriteEditorAutomation(EditorContext& ctx, const std::vector<std::string>& args, AutomationApi& api) {
            const std::string& command = args[0];
            if (command.rfind("sprite-", 0) != 0 && command.rfind("expect-sprite", 0) != 0)
                return false;
            auto arg = [&](size_t i) { return i < args.size() ? args[i] : std::string(); };
            auto argi = [&](size_t i) { return std::atoi(arg(i).c_str()); };
            auto argf = [&](size_t i) { return static_cast<float>(std::atof(arg(i).c_str())); };
            SpriteEditorWindow* window = SpriteEditorWindow::Instance();
            if (!window) {
                api.Expect(false, command + ": no Sprite Editor");
                return true;
            }
            if (command == "sprite-editor-open") {
                SpriteEditorWindow::Open(arg(1));
                api.Wait(2);
            } else if (command == "sprite-editor-close") {
                SpriteEditorWindow::IsOpen() = false;
            } else if (command == "sprite-editor-mode") {
                window->Working().SpriteMode = arg(1) == "multiple" ? SpriteImportMode::Multiple : SpriteImportMode::Single;
                window->Select(arg(1) == "multiple" ? -1 : 0);
            } else if (command == "sprite-editor-slice") {
                // sprite-editor-slice grid-size W H [ox oy px py] | grid-count C R [...] | auto [min] ; trailing "safe"
                SpriteEditorWindow::SliceOptions options = window->SliceSettings();
                const std::string type = arg(1);
                if (type == "auto") {
                    options.Type = SpriteEditorWindow::SliceType::Automatic;
                    if (!arg(2).empty() && arg(2) != "safe")
                        options.MinimumSize = argi(2);
                } else {
                    options.Type = type == "grid-count" ? SpriteEditorWindow::SliceType::GridByCellCount : SpriteEditorWindow::SliceType::GridByCellSize;
                    const glm::ivec2 a{ argi(2), argi(3) };
                    if (options.Type == SpriteEditorWindow::SliceType::GridByCellCount) options.CellCount = a; else options.CellSize = a;
                    if (args.size() > 5 && arg(4) != "safe") options.Offset = { argi(4), argi(5) };
                    if (args.size() > 7 && arg(6) != "safe") options.Padding = { argi(6), argi(7) };
                }
                options.Method = args.back() == "safe" ? SpriteEditorWindow::SliceMethod::Safe : SpriteEditorWindow::SliceMethod::DeleteExisting;
                const int sliced = window->Slice(options);
                std::printf("[editor-test] sliced %d rect(s)\n", sliced);
            } else if (command == "sprite-editor-select") {
                window->Select(argi(1));
            } else if (command == "sprite-editor-ppu") {
                window->Working().PixelsPerUnit = argf(1);
            } else if (command == "sprite-editor-set") {
                // sprite-editor-set <index> name N | rect x y w h | pivot x y | border l r t b
                SpriteDefinition sprite = window->GetSprite(argi(1));
                const std::string what = arg(2);
                if (what == "name") sprite.Name = arg(3);
                else if (what == "rect") sprite.Rect = { argf(3), argf(4), argf(5), argf(6) };
                else if (what == "pivot") sprite.Pivot = { argf(3), argf(4) };
                else if (what == "border") sprite.Border = { argf(3), argf(4), argf(5), argf(6) };
                window->SetSprite(argi(1), sprite);
                window->Select(argi(1));
            } else if (command == "sprite-editor-apply") {
                api.Expect(window->Apply(), "sprite editor apply");
            } else if (command == "sprite-editor-revert") {
                window->Revert();
            } else if (command == "sprite-editor-undo") {
                window->Undo();
            } else if (command == "sprite-editor-redo") {
                window->Redo();
            } else if (command == "sprite-editor-delete") {
                window->DeleteSelected();
            } else if (command == "sprite-editor-trim") {
                window->TrimSelected();
            } else if (command == "sprite-cook-3ds") {
                // Test helper: the cook half of File > Build for 3DS (start scene -> romfs/Scene.scene,
                // CookAssets, BuildSettings.json) WITHOUT running build-3ds.bat, so a separate build
                // tree can package it. Writes into DualityPlayer/romfs (back it up first).
                auto project = Project::GetActive();
                bool ok = project != nullptr;
                if (ok) {
                    const std::string scene = project->GetAssetsDirectory() + "/" + project->GetStartScenePath();
                    const std::filesystem::path romfs = std::filesystem::path(ctx.RepoRoot) / "DualityPlayer" / "romfs";
                    std::error_code error;
                    std::filesystem::copy_file(scene, romfs / "Scene.scene", std::filesystem::copy_options::overwrite_existing, error);
                    ok = !error && BuildPipeline::CookAssets(ctx.RepoRoot, project->GetAssetsDirectory(), scene);
                    std::ofstream settings(romfs / "BuildSettings.json");
                    settings << "{\n  \"Gravity\": " << project->GetConfig().Gravity << ",\n  \"N3DSAntiAliasing\": " << project->GetConfig().N3DSAntiAliasing
                             << ",\n  \"PPU\": " << project->GetConfig().PPU << ",\n  \"ShadowTechnique\": " << static_cast<int>(project->GetConfig().ShadowTechnique) << "\n}\n";
                }
                api.Expect(ok, "cooked the start scene's assets into romfs");
            } else if (command == "sprite-ping") {
                // Pings an asset in the Project window (opens its folder, unfolds a sliced texture).
                ctx.Session.PingAssetPath = ResolveProjectPath(arg(1));
                ctx.Session.PingStartTime = ImGui::GetTime();
                api.Wait(2);
            } else if (command == "sprite-select-asset") {
                // Selects an asset as a Project-window click would (Inspector shows it).
                const std::string path = ResolveProjectPath(arg(1));
                ctx.SelectedAssetPath = path;
                ctx.SelectedAssetPaths = { path };
                ctx.Selected = Entity();
                ctx.SelectedEntities.clear();
            } else if (command == "sprite-assign") {
                // sprite-assign <entity> <Component> <Field> <texture asset path> <sprite index | -1>
                const std::string reference = SpriteAssetUI::ReferenceForAsset(arg(4), argi(5));
                Entity entity;
                for (Entity candidate : ctx.SceneRef.GetHierarchyTraversalOrder())
                    if (candidate.GetComponent<NameComponent>().Name == arg(1))
                        entity = candidate;
                ComponentTypeInfo* type = TypeRegistry::Find(arg(2));
                bool ok = entity && type && type->Has(entity) && !reference.empty();
                if (ok) {
                    ok = false;
                    for (const FieldHandle& field : type->Fields)
                        if (field.Name == arg(3)) {
                            field.Set(type->GetPtr(entity), FieldValue(AssetRef{ reference }));
                            ok = true;
                        }
                    MarkSceneDirty(ctx, "Assign Sprite");
                }
                api.Expect(ok, "sprite-assign " + arg(1) + "." + arg(2) + "." + arg(3) + " = " + arg(4) + "[" + arg(5) + "]");
            } else if (command == "sprite-create-object") {
                // sprite-create-object <texture asset path> <sprite index | -1> <top|bottom> <x> <y>
                // = dropping that sprite into the Scene view (Unity: GameObject + SpriteRenderer,
                // native size).
                const std::string reference = SpriteAssetUI::ReferenceForAsset(arg(1), argi(2));
                if (reference.empty()) {
                    api.Expect(false, "sprite-create-object: no sprite " + arg(1) + "[" + arg(2) + "]");
                    return true;
                }
                const Screen screen = arg(3) == "bottom" ? Screen::Bottom : Screen::Top;
                const std::string name = SpriteAssetUI::DisplayName(reference);
                Entity entity = ctx.SceneRef.CreateEntity(std::filesystem::path(name).stem().string());
                entity.GetComponent<TransformComponent>().Translation = { argf(4), argf(5), 0.0f };
                SpriteRendererComponent& renderer = entity.AddComponent<SpriteRendererComponent>();
                renderer.Texture.Guid = reference;
                glm::vec2 nativeSize;
                if (SpriteAssetUI::NativeWorldSize(reference, nativeSize))
                    renderer.Size = nativeSize;
                ctx.SceneRef.SetParent(entity, ctx.SceneRef.EnsureScreenRoot(screen), {}, true);
                ctx.Selected = entity;
                ctx.SelectedEntities = { entity };
                MarkSceneDirty(ctx, "Create Sprite");
            } else if (command == "sprite-editor-drag" || command == "sprite-editor-click") {
                // Texture-pixel coordinates -> a real mouse drag/click through ImGui's input queue.
                if (!window->HasCanvas()) {
                    api.Expect(false, command + ": canvas not drawn yet");
                    return true;
                }
                const ImVec2 origin = ImGui::GetMainViewport()->Pos;
                const glm::vec2 a = window->TextureToScreen({ argf(1), argf(2) });
                char line[160];
                if (command == "sprite-editor-click") {
                    std::snprintf(line, sizeof(line), "click %.1f %.1f", a.x - origin.x, a.y - origin.y);
                } else {
                    const glm::vec2 b = window->TextureToScreen({ argf(3), argf(4) });
                    std::snprintf(line, sizeof(line), "drag %.1f %.1f %.1f %.1f left 10", a.x - origin.x, a.y - origin.y, b.x - origin.x, b.y - origin.y);
                }
                api.RunNext(line);
            } else if (command == "expect-sprite-count") {
                api.Expect(window->SpriteCount() == argi(1), "sprite count == " + arg(1) + " (actual " + std::to_string(window->SpriteCount()) + ")");
            } else if (command == "expect-sprite-dirty") {
                api.Expect(window->IsDirty() == (arg(1) == "1"), "sprite editor dirty == " + arg(1));
            } else if (command == "expect-sprite-selected") {
                api.Expect(window->SelectedIndex() == argi(1), "selected sprite == " + arg(1) + " (actual " + std::to_string(window->SelectedIndex()) + ")");
            } else if (command == "expect-sprite") {
                // expect-sprite <index> rect x y w h | name N | pivot x y | border l r t b
                const SpriteDefinition s = window->GetSprite(argi(1));
                const std::string what = arg(2);
                bool ok = false;
                char actual[160] = "";
                if (what == "name") { ok = s.Name == arg(3); std::snprintf(actual, sizeof(actual), "%s", s.Name.c_str()); }
                else if (what == "rect") { ok = s.Rect == glm::vec4(argf(3), argf(4), argf(5), argf(6)); std::snprintf(actual, sizeof(actual), "%g %g %g %g", s.Rect.x, s.Rect.y, s.Rect.z, s.Rect.w); }
                else if (what == "pivot") { ok = glm::all(glm::lessThan(glm::abs(s.Pivot - glm::vec2(argf(3), argf(4))), glm::vec2(0.01f))); std::snprintf(actual, sizeof(actual), "%g %g", s.Pivot.x, s.Pivot.y); }
                else if (what == "border") { ok = s.Border == glm::vec4(argf(3), argf(4), argf(5), argf(6)); std::snprintf(actual, sizeof(actual), "%g %g %g %g", s.Border.x, s.Border.y, s.Border.z, s.Border.w); }
                api.Expect(ok, "sprite " + arg(1) + " " + what + " (actual " + actual + ")");
            } else if (command == "expect-sprite-meta-count") {
                // Reads the .meta from disk -- verifies Apply really wrote it.
                const TextureImportSettings disk = TextureImportSettings::Load(window->TexturePath());
                api.Expect(static_cast<int>(disk.Sprites.size()) == argi(1), "sprites in .meta == " + arg(1) + " (actual " + std::to_string(disk.Sprites.size()) + ")");
            } else {
                api.Expect(false, "unknown sprite editor command '" + command + "'");
            }
            return true;
        }

        const bool s_AutomationRegistered = (EditorAutomation::RegisterExtension(&SpriteEditorAutomation), true);
    }

}
