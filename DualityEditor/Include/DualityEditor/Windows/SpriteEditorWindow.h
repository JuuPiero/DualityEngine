#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "DualityEngine/Asset/SpriteDatabase.h"
#include "DualityEngine/Asset/TextureImportSettings.h"

namespace Duality {

    struct EditorContext;

    // Unity's Sprite Editor (Window > 2D > Sprite Editor, or the "Sprite Editor" button of a
    // texture's Inspector). Edits the sprite part of a texture's import settings: in Multiple
    // mode the list of sliced sprites (rects drawn / moved / resized on the texture, Slice menu,
    // Trim, Delete), in Single mode the whole-texture sprite's pivot and border. Changes stay in
    // the window (with their own Undo/Redo) until Apply writes them to the texture's .meta;
    // Revert drops them.
    class SpriteEditorWindow {
    public:
        SpriteEditorWindow();
        ~SpriteEditorWindow();

        void OnImGuiRender(EditorContext& ctx);

        static bool& IsOpen();
        // Opens the window on a texture (asks Apply/Revert first when the current texture has
        // unapplied changes).
        static void Open(const std::string& texturePath);
        // The texture Inspector changed Sprite Mode / PPU on disk: reload if not dirty.
        static void NotifyImportSettingsChanged(const std::string& texturePath);
        static SpriteEditorWindow* Instance();

        // ---- State / commands (UI + automation + tests) ------------------------------------
        const std::string& TexturePath() const { return m_TexturePath; }
        bool IsDirty() const;
        bool Apply();
        void Revert();
        void Undo();
        void Redo();
        int SelectedIndex() const { return m_Selected; }
        void Select(int index);
        TextureImportSettings& Working() { return m_Working; }
        glm::ivec2 ImageSize() const { return { m_Width, m_Height }; }
        int SpriteCount() const;
        // The editable sprite at `index` (Single mode: index 0 = the whole texture).
        SpriteDefinition GetSprite(int index) const;
        void SetSprite(int index, const SpriteDefinition& sprite);
        int AddSprite(const glm::ivec4& rect);
        void DeleteSelected();
        void TrimSelected();
        enum class SliceType { Automatic, GridByCellSize, GridByCellCount };
        enum class SliceMethod { DeleteExisting, Safe };
        struct SliceOptions {
            SliceType Type = SliceType::GridByCellSize;
            glm::ivec2 CellSize{ 16, 16 };
            glm::ivec2 CellCount{ 4, 4 };
            glm::ivec2 Offset{ 0, 0 };
            glm::ivec2 Padding{ 0, 0 };
            bool KeepEmptyRects = false;
            int MinimumSize = 4;
            glm::vec2 Pivot{ 0.5f, 0.5f };
            SliceMethod Method = SliceMethod::DeleteExisting;
        };
        int Slice(const SliceOptions& options);
        SliceOptions& SliceSettings() { return m_SliceOptions; }
        // Screen position (ImGui coordinates) of a texture pixel in the last drawn canvas.
        glm::vec2 TextureToScreen(const glm::vec2& texel) const;
        bool HasCanvas() const { return m_CanvasSize.x > 0.0f; }

    private:
        bool LoadTexture(const std::string& path);
        void ReleaseTexture();
        void CommitIfChanged();
        void DrawToolbar(EditorContext& ctx);
        void DrawSlicePopup();
        void DrawCanvas();
        void DrawSpriteProperties();
        void DrawPendingSwitchPrompt();
        glm::vec2 ScreenToTexture(const glm::vec2& screen) const;
        void FrameTexture();

        std::string m_TexturePath;
        std::string m_DisplayName;
        TextureImportSettings m_Original;  // as on disk
        TextureImportSettings m_Working;   // edited
        TextureImportSettings m_Committed; // last undo checkpoint
        std::vector<TextureImportSettings> m_UndoStack;
        std::vector<TextureImportSettings> m_RedoStack;
        int m_Width = 0;
        int m_Height = 0;
        std::vector<uint8_t> m_Pixels; // RGBA8, row 0 = top
        uint32_t m_GLTexture = 0;
        int m_Selected = -1;

        // View: texture pixel shown at the canvas center, pixels per texel.
        glm::vec2 m_ViewCenter{ 0.0f, 0.0f };
        float m_Zoom = 1.0f;
        bool m_NeedsFrame = true;
        glm::vec2 m_CanvasMin{ 0.0f, 0.0f };
        glm::vec2 m_CanvasSize{ 0.0f, 0.0f };
        bool m_ShowAlpha = false;

        enum class DragMode { None, Create, Move, Resize, Pivot, Border, Pan };
        DragMode m_Drag = DragMode::None;
        int m_DragHandle = 0;          // Resize: bit mask L=1 R=2 T=4 B=8; Border: 0..3 = L R T B
        glm::vec2 m_DragStartTexel{ 0.0f, 0.0f };
        glm::vec2 m_DragStartMouse{ 0.0f, 0.0f };
        SpriteDefinition m_DragStartSprite;

        SliceOptions m_SliceOptions;
        std::string m_PendingOpenPath;
        bool m_OpenPendingPrompt = false;
        bool m_Focused = false;
    };

}
