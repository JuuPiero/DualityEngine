#include "DualityEditor/Windows/TilePaletteWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

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
#include "DualityEngine/Asset/TilemapAsset.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Project/Project.h"
#include "DualityEngine/Renderer/SceneRenderer.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    namespace {
        TilePaletteWindow* s_Instance = nullptr;

        constexpr const char* kIconBrush = "\xEF\x87\xBC";      // paintbrush (f1fc)
        constexpr const char* kIconBox = "\xEF\x83\x88";        // square (f0c8)
        constexpr const char* kIconPicker = "\xEF\x87\xBB";     // eyedropper (f1fb)
        constexpr const char* kIconEraser = "\xEF\x84\xAD";     // eraser (f12d)
        constexpr const char* kIconFill = "\xEF\x95\xB6";       // fill-drip (f576)

        struct PaletteTile {
            int Index = 0;
            glm::ivec2 Grid{ 0 };
            std::string Reference;
        };

        // The tileset's sprites laid out as in the sheet (grid position from the sprite
        // rects), so a rectangle picked in the palette is a coherent multi-tile brush.
        std::vector<PaletteTile> BuildPalette(const std::string& tilesetGuid, glm::ivec2& gridSize) {
            std::vector<PaletteTile> tiles;
            gridSize = { 0, 0 };
            const std::string path = AssetDatabase::ResolvePath(tilesetGuid);
            if (path.empty())
                return tiles;
            const TextureImportSettings& settings = SpriteDatabase::Settings(path);
            const std::string guid = SpriteRef::TextureGuid(tilesetGuid);
            if (settings.SpriteMode == SpriteImportMode::Multiple && !settings.Sprites.empty()) {
                glm::vec2 cell{ settings.Sprites.front().Rect.z, settings.Sprites.front().Rect.w };
                cell = glm::max(cell, glm::vec2(1.0f));
                for (int i = 0; i < static_cast<int>(settings.Sprites.size()); ++i) {
                    const SpriteDefinition& sprite = settings.Sprites[static_cast<size_t>(i)];
                    PaletteTile tile;
                    tile.Index = i;
                    tile.Grid = { static_cast<int>(std::lround(sprite.Rect.x / cell.x)), static_cast<int>(std::lround(sprite.Rect.y / cell.y)) };
                    tile.Reference = SpriteRef::Make(guid, sprite.Id);
                    gridSize = glm::max(gridSize, tile.Grid + glm::ivec2(1));
                    tiles.push_back(tile);
                }
            } else {
                tiles.push_back({ 0, { 0, 0 }, guid });
                gridSize = { 1, 1 };
            }
            return tiles;
        }
    }

    TilePaletteWindow::TilePaletteWindow() : m_Thumbnails(std::make_unique<ThumbnailCache>()) { s_Instance = this; }
    TilePaletteWindow::~TilePaletteWindow() { if (s_Instance == this) s_Instance = nullptr; }

    bool& TilePaletteWindow::IsOpen() {
        static bool open = false;
        return open;
    }

    TilePaletteWindow* TilePaletteWindow::Instance() { return s_Instance; }

    Entity TilePaletteWindow::Target() const {
        if (!m_Scene || !m_Ids || m_TargetId == 0)
            return {};
        Entity entity = m_Ids->Find(*m_Scene, m_TargetId);
        return entity && entity.HasComponent<TilemapComponent>() ? entity : Entity{};
    }

    void TilePaletteWindow::SetTarget(EditorContext& ctx, Entity tilemap) {
        m_Scene = &ctx.SceneRef;
        m_Ids = &ctx.Session.Ids;
        m_TargetId = tilemap ? ctx.Session.Ids.Get(tilemap) : 0;
    }

    glm::ivec2 TilePaletteWindow::WorldToCell(const glm::vec2& world) const {
        Entity target = Target();
        if (!target)
            return { 0, 0 };
        const TilemapComponent& tilemap = target.GetComponent<TilemapComponent>();
        const TransformComponent transform = m_Scene->GetWorldTransform(target);
        const glm::vec2 cell = glm::max(tilemap.CellSize, glm::vec2(0.0001f));
        // Tile (x, y) is centered on Translation + (x, y) * CellSize (SceneRenderer).
        return { static_cast<int>(std::floor((world.x - transform.Translation.x) / cell.x + 0.5f)),
                 static_cast<int>(std::floor((world.y - transform.Translation.y) / cell.y + 0.5f)) };
    }

    const TilemapData& TilePaletteWindow::Data() const {
        static const TilemapData s_Empty;
        if (m_Stroking)
            return m_StrokeData;
        Entity target = Target();
        if (!target || target.GetComponent<TilemapComponent>().TileData.Guid.empty())
            return s_Empty;
        return TilemapAsset::Get(AssetDatabase::ResolvePath(target.GetComponent<TilemapComponent>().TileData.Guid));
    }

    bool TilePaletteWindow::EnsureTileData(EditorContext& ctx) {
        Entity target = Target();
        if (!target)
            return false;
        TilemapComponent& tilemap = target.GetComponent<TilemapComponent>();
        if (!tilemap.TileData.Guid.empty() && !AssetDatabase::ResolvePath(tilemap.TileData.Guid).empty())
            return true;
        // Unity keeps tiles in the scene; here they live in a .tilemap asset -- create one.
        auto project = Project::GetActive();
        if (!project)
            return false;
        std::string name = target.GetComponent<NameComponent>().Name;
        for (char& c : name)
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                c = '_';
        const std::filesystem::path directory = std::filesystem::path(project->GetAssetsDirectory()) / "Tilemaps";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        std::filesystem::path path = directory / (name + ".tilemap");
        for (int i = 1; std::filesystem::exists(path); ++i)
            path = directory / (name + " " + std::to_string(i) + ".tilemap");
        TilemapData data;
        data.Width = std::max(1, tilemap.GridWidth);
        data.Height = std::max(1, tilemap.GridHeight);
        TilemapEdit::Normalize(data);
        if (!TilemapAsset::Save(path.string(), data))
            return false;
        const std::string guid = AssetMeta::EnsureMetaFile(path);
        AssetDatabase::Register(guid, path.string());
        tilemap.TileData.Guid = guid;
        MarkSceneDirty(ctx, "Create Tile Data");
        Log::Info("Tile Palette: created " + path.filename().string());
        return true;
    }

    bool TilePaletteWindow::BeginStroke(EditorContext& ctx) {
        if (m_Stroking)
            return true;
        if (!EnsureTileData(ctx))
            return false;
        Entity target = Target();
        const TilemapComponent& tilemap = target.GetComponent<TilemapComponent>();
        m_StrokePath = AssetDatabase::ResolvePath(tilemap.TileData.Guid);
        // Before the first write: the history snapshots the file as it is now.
        if (!ctx.History.IsTrackingFile(m_StrokePath))
            ctx.History.TrackFile(m_StrokePath);
        m_StrokeData = TilemapAsset::Get(m_StrokePath);
        if (m_StrokeData.Width <= 0 || m_StrokeData.Height <= 0) {
            m_StrokeData.Width = std::max(1, tilemap.GridWidth);
            m_StrokeData.Height = std::max(1, tilemap.GridHeight);
        }
        TilemapEdit::Normalize(m_StrokeData);
        m_StrokeChanges = 0;
        m_Stroking = true;
        return true;
    }

    void TilePaletteWindow::StampAt(glm::ivec2 cell, bool erase) {
        if (erase) {
            for (int y = 0; y < m_BrushSize.y; ++y)
                for (int x = 0; x < m_BrushSize.x; ++x)
                    m_StrokeChanges += TilemapEdit::Set(m_StrokeData, cell.x + x, cell.y + y, -1) ? 1 : 0;
        } else {
            m_StrokeChanges += TilemapEdit::Stamp(m_StrokeData, cell, m_BrushSize, m_Brush);
        }
        TilemapAsset::Store(m_StrokePath, m_StrokeData); // live in Scene / Game views
    }

    void TilePaletteWindow::EndStroke(EditorContext& ctx, const char* label) {
        if (!m_Stroking)
            return;
        m_Stroking = false;
        if (m_StrokeChanges > 0) {
            TilemapAsset::Save(m_StrokePath, m_StrokeData);
            MarkSceneDirty(ctx, label); // the history captures the .tilemap content (TrackFile)
        }
    }

    int TilePaletteWindow::Paint(EditorContext& ctx, const std::vector<glm::ivec2>& cells, bool erase) {
        if (!BeginStroke(ctx))
            return 0;
        for (const glm::ivec2& cell : cells)
            StampAt(cell, erase);
        const int changes = m_StrokeChanges;
        EndStroke(ctx, erase ? "Erase Tiles" : "Paint Tiles");
        return changes;
    }

    int TilePaletteWindow::Box(EditorContext& ctx, glm::ivec2 a, glm::ivec2 b, bool erase) {
        if (!BeginStroke(ctx))
            return 0;
        const glm::ivec2 lo = glm::min(a, b), hi = glm::max(a, b);
        // The brush pattern repeats over the box (Unity's Box Fill with a multi-tile brush).
        for (int y = lo.y; y <= hi.y; ++y)
            for (int x = lo.x; x <= hi.x; ++x) {
                const int bx = ((x - lo.x) % m_BrushSize.x), by = ((y - lo.y) % m_BrushSize.y);
                const size_t index = static_cast<size_t>(by) * m_BrushSize.x + bx;
                const int tile = erase ? -1 : (index < m_Brush.size() ? m_Brush[index] : -1);
                if (!erase && tile < 0)
                    continue;
                m_StrokeChanges += TilemapEdit::Set(m_StrokeData, x, y, tile) ? 1 : 0;
            }
        TilemapAsset::Store(m_StrokePath, m_StrokeData);
        const int changes = m_StrokeChanges;
        EndStroke(ctx, erase ? "Erase Tiles" : "Box Fill Tiles");
        return changes;
    }

    int TilePaletteWindow::Fill(EditorContext& ctx, glm::ivec2 cell) {
        if (!BeginStroke(ctx))
            return 0;
        m_StrokeChanges += TilemapEdit::FloodFill(m_StrokeData, cell.x, cell.y, BrushTile());
        TilemapAsset::Store(m_StrokePath, m_StrokeData);
        const int changes = m_StrokeChanges;
        EndStroke(ctx, "Fill Tiles");
        return changes;
    }

    int TilePaletteWindow::Pick(glm::ivec2 cell) {
        const int tile = TilemapEdit::Get(Data(), cell.x, cell.y);
        if (tile >= 0)
            SetBrush(tile);
        return tile;
    }

    bool TilePaletteWindow::CellToScreen(glm::ivec2 cell, glm::vec2& out) const {
        Entity target = Target();
        const int pane = m_LastPaneScreen == Screen::Top ? 0 : 1;
        if (!target || !m_PaneKnown[pane])
            return false;
        const TilemapComponent& tilemap = target.GetComponent<TilemapComponent>();
        const TransformComponent transform = m_Scene->GetWorldTransform(target);
        const glm::vec2 world{ transform.Translation.x + cell.x * tilemap.CellSize.x, transform.Translation.y + cell.y * tilemap.CellSize.y };
        out = m_PaneOrigin[pane] + world * m_PaneScale[pane];
        return true;
    }

    // ---- Scene view input ------------------------------------------------------------------

    bool TilePaletteWindow::HandleSceneInput(EditorContext& ctx, const SceneInput& input) {
        return s_Instance && IsOpen() && s_Instance->HandleInput(ctx, input);
    }

    bool TilePaletteWindow::HandleInput(EditorContext& ctx, const SceneInput& input) {
        m_Scene = &ctx.SceneRef;
        m_Ids = &ctx.Session.Ids;
        Entity target = Target();
        if (m_Tool == Tool::None || !target || ctx.IsPlaying || !ShouldRenderOnScreen(ctx.SceneRef, target.Handle(), input.TargetScreen))
            return false;

        const int pane = input.TargetScreen == Screen::Top ? 0 : 1;
        if (input.WorldToScreen) {
            m_PaneOrigin[pane] = input.WorldToScreen({ 0.0f, 0.0f });
            m_PaneScale[pane] = input.WorldToScreen({ 1.0f, 1.0f }) - m_PaneOrigin[pane];
            m_PaneKnown[pane] = true;
            if (input.Hovered || !m_PaneKnown[1 - pane])
                m_LastPaneScreen = input.TargetScreen;
        }
        // A stroke belongs to the pane it started in (a Default-layer tilemap shows in both).
        if ((m_Stroking || m_Boxing) && input.TargetScreen != m_ActivePane)
            return false;
        if (input.Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_ActivePane = input.TargetScreen;

        const TilemapComponent& tilemap = target.GetComponent<TilemapComponent>();
        const TransformComponent transform = ctx.SceneRef.GetWorldTransform(target);
        const glm::ivec2 cell = WorldToCell(input.MouseWorld);
        auto cellRect = [&](glm::ivec2 a, glm::ivec2 b, ImVec2& outMin, ImVec2& outMax) {
            const glm::ivec2 lo = glm::min(a, b), hi = glm::max(a, b);
            const glm::vec2 worldMin{ transform.Translation.x + (lo.x - 0.5f) * tilemap.CellSize.x, transform.Translation.y + (lo.y - 0.5f) * tilemap.CellSize.y };
            const glm::vec2 worldMax{ transform.Translation.x + (hi.x + 0.5f) * tilemap.CellSize.x, transform.Translation.y + (hi.y + 0.5f) * tilemap.CellSize.y };
            const glm::vec2 a2 = input.WorldToScreen(worldMin), b2 = input.WorldToScreen(worldMax);
            outMin = ImVec2(std::min(a2.x, b2.x), std::min(a2.y, b2.y));
            outMax = ImVec2(std::max(a2.x, b2.x), std::max(a2.y, b2.y));
        };

        // Tilemap bounds (Unity shows the grid while painting).
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const TilemapData& data = Data();
        const glm::ivec2 size{ data.Width > 0 ? data.Width : tilemap.GridWidth, data.Height > 0 ? data.Height : tilemap.GridHeight };
        if (input.WorldToScreen && size.x > 0 && size.y > 0) {
            ImVec2 a, b;
            cellRect({ 0, 0 }, size - glm::ivec2(1), a, b);
            draw->AddRect(a, b, IM_COL32(255, 255, 255, 70));
        }

        const bool leftClicked = input.Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        switch (m_Tool) {
            case Tool::Paint:
            case Tool::Erase: {
                const bool erase = m_Tool == Tool::Erase;
                if (leftClicked && BeginStroke(ctx)) {
                    StampAt(cell, erase);
                    m_LastCell = cell;
                } else if (m_Stroking && leftDown && cell != m_LastCell) {
                    for (const glm::ivec2& c : TilemapEdit::Line(m_LastCell, cell))
                        StampAt(c, erase);
                    m_LastCell = cell;
                }
                if (m_Stroking && !leftDown)
                    EndStroke(ctx, erase ? "Erase Tiles" : "Paint Tiles");
                if (input.Hovered && input.WorldToScreen) {
                    ImVec2 a, b;
                    cellRect(cell, cell + m_BrushSize - glm::ivec2(1), a, b);
                    if (!erase) {
                        // Ghost of the brush tiles.
                        const std::string tileset = tilemap.Tileset.Guid;
                        const glm::vec2 cellPx{ (b.x - a.x) / m_BrushSize.x, (b.y - a.y) / m_BrushSize.y };
                        for (int y = 0; y < m_BrushSize.y; ++y)
                            for (int x = 0; x < m_BrushSize.x; ++x) {
                                const size_t index = static_cast<size_t>(y) * m_BrushSize.x + x;
                                if (index >= m_Brush.size() || m_Brush[index] < 0)
                                    continue;
                                ResolvedSprite sprite;
                                const bool sliced = SpriteDatabase::SpriteCount(tileset) > 0;
                                std::string reference = tileset;
                                if (sliced && SpriteDatabase::ResolveIndex(tileset, m_Brush[index], sprite)) {
                                    const TextureImportSettings& settings = SpriteDatabase::Settings(sprite.TexturePath);
                                    reference = SpriteRef::Make(SpriteRef::TextureGuid(tileset), settings.Sprites[static_cast<size_t>(m_Brush[index])].Id);
                                }
                                const ImVec2 cmin(a.x + cellPx.x * x, a.y + cellPx.y * y);
                                SpriteAssetUI::DrawThumbnail(draw, *m_Thumbnails, reference, cmin, ImVec2(cmin.x + cellPx.x, cmin.y + cellPx.y), IM_COL32(255, 255, 255, 150));
                            }
                    }
                    draw->AddRect(a, b, erase ? IM_COL32(255, 120, 120, 255) : IM_COL32(255, 255, 255, 230), 0.0f, 0, 2.0f);
                }
                break;
            }
            case Tool::Box: {
                if (leftClicked) {
                    m_BoxStart = cell;
                    m_LastCell = cell;
                    m_Boxing = true;
                }
                const bool boxing = m_Boxing;
                if (boxing && leftDown) {
                    m_LastCell = cell;
                } else if (boxing) {
                    m_Boxing = false;
                    Box(ctx, m_BoxStart, m_LastCell, false);
                }
                if (input.WorldToScreen && (input.Hovered || boxing)) {
                    ImVec2 a, b;
                    cellRect(boxing ? m_BoxStart : cell, boxing ? m_LastCell : cell, a, b);
                    draw->AddRectFilled(a, b, IM_COL32(64, 160, 255, 50));
                    draw->AddRect(a, b, IM_COL32(64, 160, 255, 255), 0.0f, 0, 2.0f);
                }
                break;
            }
            case Tool::Fill:
                if (leftClicked)
                    Fill(ctx, cell);
                if (input.Hovered && input.WorldToScreen) {
                    ImVec2 a, b;
                    cellRect(cell, cell, a, b);
                    draw->AddRect(a, b, IM_COL32(120, 255, 160, 255), 0.0f, 0, 2.0f);
                }
                break;
            case Tool::Pick:
                if (leftClicked && Pick(cell) >= 0)
                    m_Tool = Tool::Paint; // Unity switches back to the brush after picking
                if (input.Hovered && input.WorldToScreen) {
                    ImVec2 a, b;
                    cellRect(cell, cell, a, b);
                    draw->AddRect(a, b, IM_COL32(255, 220, 90, 255), 0.0f, 0, 2.0f);
                }
                break;
            default:
                break;
        }
        return input.Hovered || m_Stroking || m_Boxing;
    }

    // ---- Window ----------------------------------------------------------------------------

    void TilePaletteWindow::OnImGuiRender(EditorContext& ctx) {
        m_Scene = &ctx.SceneRef;
        m_Ids = &ctx.Session.Ids;
        bool& open = IsOpen();
        if (!open) {
            m_Tool = Tool::None;
            return;
        }
        // Follow the selection when it is a Tilemap (Unity's Active Tilemap).
        if (ctx.Selected && ctx.Selected.HasComponent<TilemapComponent>() && ctx.Session.Ids.Get(ctx.Selected) != m_TargetId)
            SetTarget(ctx, ctx.Selected);

        ImGui::SetNextWindowSize(ImVec2(360.0f, 480.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Tile Palette", &open)) {
            ImGui::End();
            return;
        }
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (focused) {
            ctx.Session.FocusedPanel = EditorPanel::Other;
            ctx.Session.FocusedWindowUndo = nullptr;
        }

        // Tools (Unity shortcuts B U I D G; Esc = none) while this window or the Scene view has focus.
        const ImGuiIO& io = ImGui::GetIO();
        // Not while RMB-flying the 3D Scene view (W A S D Q E move the camera there).
        if ((focused || ctx.Session.FocusedPanel == EditorPanel::Scene) && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            if (ImGui::IsKeyPressed(ImGuiKey_B, false)) m_Tool = Tool::Paint;
            if (ImGui::IsKeyPressed(ImGuiKey_U, false)) m_Tool = Tool::Box;
            if (ImGui::IsKeyPressed(ImGuiKey_I, false)) m_Tool = Tool::Pick;
            if (ImGui::IsKeyPressed(ImGuiKey_D, false)) m_Tool = Tool::Erase;
            if (ImGui::IsKeyPressed(ImGuiKey_G, false)) m_Tool = Tool::Fill;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) m_Tool = Tool::None;
        }
        const float square = ImGui::GetFrameHeight() * 1.3f;
        struct ToolButton { Tool Value; const char* Icon; const char* Tip; };
        const ToolButton tools[] = {
            { Tool::Paint, kIconBrush, "Paint with active brush (B)" }, { Tool::Box, kIconBox, "Box Fill (U)" },
            { Tool::Pick, kIconPicker, "Picker (I)" }, { Tool::Erase, kIconEraser, "Erase (D)" }, { Tool::Fill, kIconFill, "Flood Fill (G)" },
        };
        for (const ToolButton& tool : tools) {
            const bool active = m_Tool == tool.Value;
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(tool.Icon, ImVec2(square, square)))
                m_Tool = active ? Tool::None : tool.Value;
            if (active)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tool.Tip);
            ImGui::SameLine(0.0f, 2.0f);
        }
        ImGui::NewLine();

        // Active Tilemap (every Tilemap in the scene).
        Entity target = Target();
        const std::string targetName = target ? target.GetComponent<NameComponent>().Name : std::string("None");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##ActiveTilemap", ("Active Tilemap: " + targetName).c_str())) {
            for (Entity entity : ctx.SceneRef.GetHierarchyTraversalOrder()) {
                if (!entity.HasComponent<TilemapComponent>())
                    continue;
                ImGui::PushID(static_cast<int>(static_cast<uint32_t>(entity.Handle())));
                if (ImGui::Selectable(entity.GetComponent<NameComponent>().Name.c_str(), entity == target))
                    SetTarget(ctx, entity);
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (!target) {
            ImGui::TextWrapped("Select (or create, GameObject > 2D Object > Tilemap) a Tilemap to paint on.");
            ImGui::End();
            return;
        }
        TilemapComponent& tilemap = target.GetComponent<TilemapComponent>();
        if (tilemap.Tileset.Guid.empty()) {
            ImGui::TextWrapped("The Tilemap has no Tileset. Assign a texture (slice it with the Sprite Editor) to its Tileset field.");
        }
        const std::string dataPath = tilemap.TileData.Guid.empty() ? std::string() : AssetDatabase::ResolvePath(tilemap.TileData.Guid);
        ImGui::TextDisabled("Tile Data: %s  |  %dx%d cells", dataPath.empty() ? "(created on first stroke)" : std::filesystem::path(dataPath).filename().string().c_str(),
            Data().Width > 0 ? Data().Width : tilemap.GridWidth, Data().Height > 0 ? Data().Height : tilemap.GridHeight);
        ImGui::Separator();
        DrawPalette(ctx);
        ImGui::End();
    }

    void TilePaletteWindow::DrawPalette(EditorContext& ctx) {
        (void)ctx;
        Entity target = Target();
        const TilemapComponent& tilemap = target.GetComponent<TilemapComponent>();
        glm::ivec2 gridSize;
        const std::vector<PaletteTile> palette = BuildPalette(tilemap.Tileset.Guid, gridSize);
        if (palette.empty())
            return;
        const float cell = 40.0f;
        ImGui::BeginChild("##Palette", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##PaletteGrid", ImVec2(std::max(1.0f, gridSize.x * cell), std::max(1.0f, gridSize.y * cell)));
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, ImVec2(origin.x + gridSize.x * cell, origin.y + gridSize.y * cell), IM_COL32(30, 30, 30, 255));
        for (const PaletteTile& tile : palette) {
            const ImVec2 min(origin.x + tile.Grid.x * cell + 1.0f, origin.y + tile.Grid.y * cell + 1.0f);
            const ImVec2 max(min.x + cell - 2.0f, min.y + cell - 2.0f);
            SpriteAssetUI::DrawThumbnail(draw, *m_Thumbnails, tile.Reference, min, max);
            draw->AddRect(min, max, IM_COL32(70, 70, 70, 255));
        }
        // Mouse: click = one tile, drag = rectangle brush (Unity).
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const glm::ivec2 gridCell{ static_cast<int>(std::floor((mouse.x - origin.x) / cell)), static_cast<int>(std::floor((mouse.y - origin.y) / cell)) };
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_PaletteDragging = true;
            m_PaletteStart = m_PaletteEnd = glm::clamp(gridCell, glm::ivec2(0), gridSize - glm::ivec2(1));
        }
        if (m_PaletteDragging) {
            m_PaletteEnd = glm::clamp(gridCell, glm::ivec2(0), gridSize - glm::ivec2(1));
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                m_PaletteDragging = false;
                const glm::ivec2 lo = glm::min(m_PaletteStart, m_PaletteEnd), hi = glm::max(m_PaletteStart, m_PaletteEnd);
                m_BrushSize = hi - lo + glm::ivec2(1);
                m_Brush.assign(static_cast<size_t>(m_BrushSize.x) * m_BrushSize.y, -1);
                for (const PaletteTile& tile : palette)
                    if (tile.Grid.x >= lo.x && tile.Grid.x <= hi.x && tile.Grid.y >= lo.y && tile.Grid.y <= hi.y)
                        m_Brush[static_cast<size_t>(tile.Grid.y - lo.y) * m_BrushSize.x + (tile.Grid.x - lo.x)] = tile.Index;
                if (m_Tool == Tool::None || m_Tool == Tool::Pick || m_Tool == Tool::Erase)
                    m_Tool = Tool::Paint;
            }
        }
        // Brush highlight.
        glm::ivec2 lo, hi;
        if (m_PaletteDragging) {
            lo = glm::min(m_PaletteStart, m_PaletteEnd);
            hi = glm::max(m_PaletteStart, m_PaletteEnd);
        } else {
            // Where the current brush's first tile sits in the palette.
            lo = hi = glm::ivec2(-1);
            const int first = BrushTile();
            for (const PaletteTile& tile : palette)
                if (tile.Index == first) { lo = tile.Grid; hi = tile.Grid + m_BrushSize - glm::ivec2(1); }
        }
        if (lo.x >= 0)
            draw->AddRect(ImVec2(origin.x + lo.x * cell, origin.y + lo.y * cell), ImVec2(origin.x + (hi.x + 1) * cell, origin.y + (hi.y + 1) * cell),
                IM_COL32(64, 160, 255, 255), 0.0f, 0, 2.5f);
        if (hovered) {
            for (const PaletteTile& tile : palette)
                if (tile.Grid == gridCell)
                    ImGui::SetTooltip("%s (tile %d)", SpriteAssetUI::DisplayName(tile.Reference).c_str(), tile.Index);
        }
        ImGui::EndChild();
    }

    // ---- Automation ----------------------------------------------------------------------

    namespace {
        bool TileAutomation(EditorContext& ctx, const std::vector<std::string>& args, AutomationApi& api) {
            const std::string& command = args[0];
            if (command.rfind("tile-", 0) != 0 && command.rfind("expect-tile", 0) != 0)
                return false;
            auto arg = [&](size_t i) { return i < args.size() ? args[i] : std::string(); };
            auto argi = [&](size_t i) { return std::atoi(arg(i).c_str()); };
            TilePaletteWindow* window = TilePaletteWindow::Instance();
            if (!window) {
                api.Expect(false, command + ": no Tile Palette");
                return true;
            }
            if (command == "tile-palette-open") {
                TilePaletteWindow::IsOpen() = true;
                api.Wait(2);
            } else if (command == "tile-palette-close") {
                TilePaletteWindow::IsOpen() = false;
            } else if (command == "tile-target") {
                Entity found;
                for (Entity entity : ctx.SceneRef.GetHierarchyTraversalOrder())
                    if (entity.HasComponent<TilemapComponent>() && entity.GetComponent<NameComponent>().Name == arg(1))
                        found = entity;
                api.Expect(static_cast<bool>(found), "tilemap '" + arg(1) + "' found");
                window->SetTarget(ctx, found);
            } else if (command == "tile-tool") {
                const std::string tool = arg(1);
                window->SetTool(tool == "paint" ? TilePaletteWindow::Tool::Paint : tool == "box" ? TilePaletteWindow::Tool::Box :
                    tool == "pick" ? TilePaletteWindow::Tool::Pick : tool == "erase" ? TilePaletteWindow::Tool::Erase :
                    tool == "fill" ? TilePaletteWindow::Tool::Fill : TilePaletteWindow::Tool::None);
            } else if (command == "tile-brush") {
                window->SetBrush(argi(1));
            } else if (command == "tile-scene-view") {
                // tile-scene-view <top|bottom> <zoom> <world x> <world y> : position the 2D Scene pane camera
                SceneViewCamera& camera = arg(1) == "bottom" ? ctx.BottomSceneView : ctx.TopSceneView;
                camera.Zoom = static_cast<float>(std::atof(arg(2).c_str()));
                camera.Position = { static_cast<float>(std::atof(arg(3).c_str())), static_cast<float>(std::atof(arg(4).c_str())) };
                api.Wait(2);
            } else if (command == "tile-paint" || command == "tile-erase") {
                window->Paint(ctx, { { argi(1), argi(2) } }, command == "tile-erase");
            } else if (command == "tile-box") {
                window->Box(ctx, { argi(1), argi(2) }, { argi(3), argi(4) }, false);
            } else if (command == "tile-fill") {
                window->Fill(ctx, { argi(1), argi(2) });
            } else if (command == "tile-pick") {
                window->Pick({ argi(1), argi(2) });
            } else if (command == "tile-scene-drag" || command == "tile-scene-click") {
                // Cell coordinates -> a real mouse drag in the Scene view pane.
                glm::vec2 a, b;
                if (!window->CellToScreen({ argi(1), argi(2) }, a)) {
                    api.Expect(false, command + ": Scene pane mapping unknown (select a brush tool and hover the Scene first)");
                    return true;
                }
                const ImVec2 origin = ImGui::GetMainViewport()->Pos;
                char line[160];
                if (command == "tile-scene-click") {
                    std::snprintf(line, sizeof(line), "click %.1f %.1f", a.x - origin.x, a.y - origin.y);
                } else {
                    window->CellToScreen({ argi(3), argi(4) }, b);
                    std::snprintf(line, sizeof(line), "drag %.1f %.1f %.1f %.1f left 12", a.x - origin.x, a.y - origin.y, b.x - origin.x, b.y - origin.y);
                }
                api.RunNext(line);
            } else if (command == "expect-tile") {
                const int actual = TilemapEdit::Get(window->Data(), argi(1), argi(2));
                api.Expect(actual == argi(3), "tile (" + arg(1) + "," + arg(2) + ") == " + arg(3) + " (actual " + std::to_string(actual) + ")");
            } else if (command == "expect-tile-count") {
                // expect-tile-count <tile index> <count>
                int count = 0;
                for (int tile : window->Data().Tiles)
                    count += tile == argi(1) ? 1 : 0;
                api.Expect(count == argi(2), "tiles == " + arg(1) + " count " + arg(2) + " (actual " + std::to_string(count) + ")");
            } else if (command == "expect-tile-brush") {
                api.Expect(window->BrushTile() == argi(1), "brush tile == " + arg(1) + " (actual " + std::to_string(window->BrushTile()) + ")");
            } else {
                api.Expect(false, "unknown tile command '" + command + "'");
            }
            return true;
        }

        const bool s_AutomationRegistered = (EditorAutomation::RegisterExtension(&TileAutomation), true);
    }

}
