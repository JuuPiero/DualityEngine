#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Scene/SceneRuntimeSystems.h"

namespace Duality {

    struct EditorContext;
    class ThumbnailCache;
    class EntityIdRegistry;

    // Unity's Tile Palette (Window > 2D > Tile Palette): pick tiles from the active Tilemap's
    // tileset (its sliced sprites, Sprite Editor) and paint them in the Scene view with the
    // Paint (B), Box Fill (U), Picker (I), Eraser (D) and Fill (G) tools. A stroke is one
    // Undo step (the .tilemap file is tracked by the scene history).
    class TilePaletteWindow {
    public:
        enum class Tool { None, Paint, Box, Pick, Erase, Fill };

        TilePaletteWindow();
        ~TilePaletteWindow();

        void OnImGuiRender(EditorContext& ctx);
        static bool& IsOpen();
        static TilePaletteWindow* Instance();

        // Called by the Scene view (2D panes) every frame. Returns true when a brush tool owns
        // the mouse (the Scene view then skips selection / gizmos).
        struct SceneInput {
            bool Hovered = false;
            Screen TargetScreen = Screen::Top;
            glm::vec2 MouseWorld{ 0.0f };
            std::function<glm::vec2(const glm::vec2&)> WorldToScreen;
        };
        static bool HandleSceneInput(EditorContext& ctx, const SceneInput& input);

        // ---- Model (UI, automation) ----------------------------------------------------------
        Tool ActiveTool() const { return m_Tool; }
        void SetTool(Tool tool) { m_Tool = tool; }
        Entity Target() const;
        void SetTarget(EditorContext& ctx, Entity tilemap);
        void SetBrush(int tile) { m_Brush = { tile }; m_BrushSize = { 1, 1 }; }
        int BrushTile() const { return m_Brush.empty() ? -1 : m_Brush.front(); }
        glm::ivec2 WorldToCell(const glm::vec2& world) const;
        // Edits of the target's tile data -- each call is one Undo step.
        int Paint(EditorContext& ctx, const std::vector<glm::ivec2>& cells, bool erase);
        int Box(EditorContext& ctx, glm::ivec2 a, glm::ivec2 b, bool erase);
        int Fill(EditorContext& ctx, glm::ivec2 cell);
        int Pick(glm::ivec2 cell);
        const TilemapData& Data() const;
        // Screen position of a cell center in the last drawn Scene pane (automation drags).
        bool CellToScreen(glm::ivec2 cell, glm::vec2& out) const;

    private:
        bool EnsureTileData(EditorContext& ctx);
        bool BeginStroke(EditorContext& ctx);
        void StampAt(glm::ivec2 cell, bool erase);
        void EndStroke(EditorContext& ctx, const char* label);
        bool HandleInput(EditorContext& ctx, const SceneInput& input);
        void DrawPalette(EditorContext& ctx);

        uint64_t m_TargetId = 0;
        Scene* m_Scene = nullptr;
        EntityIdRegistry* m_Ids = nullptr;
        Tool m_Tool = Tool::None;
        std::vector<int> m_Brush{ 0 };
        glm::ivec2 m_BrushSize{ 1, 1 };
        // Stroke state.
        bool m_Stroking = false;
        std::string m_StrokePath;
        TilemapData m_StrokeData;
        glm::ivec2 m_LastCell{ 0 };
        glm::ivec2 m_BoxStart{ 0 };
        int m_StrokeChanges = 0;
        // Palette selection (grid coordinates) while dragging a rectangle.
        bool m_PaletteDragging = false;
        glm::ivec2 m_PaletteStart{ 0 }, m_PaletteEnd{ 0 };
        // Last Scene-pane mapping per screen (screen = origin + world * scale).
        glm::vec2 m_PaneOrigin[2]{};
        glm::vec2 m_PaneScale[2]{};
        bool m_PaneKnown[2]{ false, false };
        Screen m_LastPaneScreen = Screen::Top;
        Screen m_ActivePane = Screen::Top; // the pane a stroke / box drag started in
        bool m_Boxing = false;
        std::unique_ptr<ThumbnailCache> m_Thumbnails;
    };

}
