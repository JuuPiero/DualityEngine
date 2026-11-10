// Engine-side logic behind the editor's Unity-style tool windows: sprites sliced by the Sprite
// Editor (import data, references, UVs / pivots / 9-slice), slicing algorithms, tilemap brush
// operations, Animation-window clips (curves, sampling, scene playback), the Profiler, and the
// scene history tracking tool-edited asset files.
#include "TestFramework.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "DualityEditor/EntityIdRegistry.h"
#include "DualityEditor/SceneHistory.h"
#include "DualityEngine/Asset/AnimationClipLoader.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Asset/SpriteDatabase.h"
#include "DualityEngine/Asset/TextureImportSettings.h"
#include "DualityEngine/Asset/TilemapAsset.h"
#include "DualityEngine/Core/Profiler.h"
#include "DualityEngine/Renderer/DrawHelpers2D.h"
#include "DualityEngine/Renderer/IRenderer2D.h"
#include "DualityEngine/Renderer/SceneRenderer.h"
#include "DualityEngine/Scene/AnimationPlayback.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneRuntimeSystems.h"
#include "DualityEngine/Scene/SceneSerializer.h"

using namespace Duality;

namespace {
    std::string TempPath(const std::string& name) {
        return (std::filesystem::temp_directory_path() / name).string();
    }

    bool Near(float a, float b, float epsilon = 0.0001f) { return std::fabs(a - b) <= epsilon; }
    bool Near(const glm::vec4& a, const glm::vec4& b, float epsilon = 0.0001f) {
        return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon) && Near(a.z, b.z, epsilon) && Near(a.w, b.w, epsilon);
    }
    bool Near(const glm::vec2& a, const glm::vec2& b, float epsilon = 0.0001f) {
        return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon);
    }

    std::string ReadAll(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    // Records DrawQuad calls instead of drawing.
    class RecordingRenderer final : public IRenderer2D {
    public:
        struct Quad { glm::vec2 Position, Size; float Rotation; uint32_t Texture; glm::vec4 UV; };
        std::vector<Quad> Quads;
        glm::vec2 TextureSize{ 64.0f, 32.0f };

        void Init() override {}
        void Shutdown() override {}
        void BeginFrame() override {}
        void EndFrame() override {}
        void BeginScene(Screen, const glm::vec4&, bool) override {}
        void EndScene() override {}
        void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4&, float rotationDegrees, uint32_t textureId, const glm::vec4& uvRect) override {
            Quads.push_back({ position, size, rotationDegrees, textureId, uvRect });
        }
        void DrawText(const std::string&, const glm::vec2&, float, const glm::vec4&, uint32_t) override {}
        glm::vec2 MeasureText(const std::string&, float, uint32_t) override { return glm::vec2(0.0f); }
        uint32_t LoadFont(const std::string&) override { return 0; }
        uint32_t LoadTexture(const std::string& path) override { return path.empty() ? 0u : 7u; }
        void UnloadAllTextures() override {}
        void UnloadAllFonts() override {}
        uint32_t GetDrawCallCount() const override { return static_cast<uint32_t>(Quads.size()); }
        glm::vec2 GetTextureSize(uint32_t) const override { return TextureSize; }
    };

    // A 64x32 "sheet" texture registered with the asset database, sliced into two sprites.
    struct SheetFixture {
        std::string TexturePath = TempPath("duality_sprite_sheet_test.png");
        std::string Guid;
        SheetFixture() {
            std::filesystem::remove(TexturePath + ".meta");
            { std::ofstream(TexturePath) << "not really a png"; }
            Guid = AssetMeta::EnsureMetaFile(TexturePath);
            AssetDatabase::Register(Guid, TexturePath);
            TextureImportSettings settings;
            settings.SpriteMode = SpriteImportMode::Multiple;
            settings.Width = 64;
            settings.Height = 32;
            settings.PixelsPerUnit = 16.0f;
            SpriteDefinition a;
            a.Id = "aaaa"; a.Name = "hero_0"; a.Rect = { 0, 0, 16, 32 }; a.Pivot = { 0.5f, 1.0f };
            SpriteDefinition b;
            b.Id = "bbbb"; b.Name = "panel"; b.Rect = { 32, 0, 32, 16 }; b.Pivot = { 0.0f, 0.0f }; b.Border = { 4, 4, 2, 2 };
            settings.Sprites = { a, b };
            TextureImportSettings::Save(TexturePath, settings);
            SpriteDatabase::Invalidate();
        }
        ~SheetFixture() {
            std::filesystem::remove(TexturePath);
            std::filesystem::remove(TexturePath + ".meta");
            SpriteDatabase::Invalidate();
        }
    };

    // RGBA test image: transparent with opaque rectangles.
    std::vector<uint8_t> MakeImage(int w, int h, const std::vector<glm::ivec4>& opaque) {
        std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4, 0);
        for (const glm::ivec4& r : opaque)
            for (int y = r.y; y < r.y + r.w; ++y)
                for (int x = r.x; x < r.x + r.z; ++x)
                    pixels[(static_cast<size_t>(y) * w + x) * 4 + 3] = 255;
        return pixels;
    }
}

// ---- Sprite import data ------------------------------------------------------------------------

TEST_CASE("Sprite import data round-trips through the texture .meta and keeps old metas compact") {
    const std::string path = TempPath("duality_sprite_meta_roundtrip.png");
    std::filesystem::remove(path + ".meta");
    const std::string guid = AssetMeta::EnsureMetaFile(path);

    // An ordinary texture edit (pre-sprite settings) writes no sprite keys.
    TextureImportSettings plain;
    plain.FilterMode = TextureFilterMode::Point;
    CHECK(TextureImportSettings::Save(path, plain));
    nlohmann::json root = nlohmann::json::parse(ReadAll(path + ".meta"));
    CHECK_SOFT(root.value("guid", std::string()) == guid, "guid preserved");
    CHECK_SOFT(!root["Importer"].contains("SpriteMode") && !root["Importer"].contains("Sprites") && !root["Importer"].contains("TextureSize"),
        "no sprite keys for a plain texture");

    TextureImportSettings settings;
    settings.SpriteMode = SpriteImportMode::Multiple;
    settings.PixelsPerUnit = 32.0f;
    settings.Width = 128;
    settings.Height = 64;
    settings.HasSpritePivot = true;
    settings.SpritePivot = { 0.25f, 0.75f };
    settings.SpriteBorder = { 1, 2, 3, 4 };
    SpriteDefinition sprite;
    sprite.Id = "0123456789abcdef";
    sprite.Name = "walk_0";
    sprite.Rect = { 16, 8, 32, 24 };
    sprite.Pivot = { 0.5f, 1.0f };
    sprite.Border = { 2, 2, 1, 1 };
    settings.Sprites.push_back(sprite);
    CHECK(TextureImportSettings::Save(path, settings));

    const TextureImportSettings loaded = TextureImportSettings::Load(path);
    CHECK_SOFT(loaded.SpriteDataEquals(settings), "sprite data identical after reload");
    CHECK_SOFT(loaded.FindSprite("0123456789abcdef") && loaded.FindSprite("0123456789abcdef")->Name == "walk_0", "FindSprite by id");
    CHECK_SOFT(loaded.FindSpriteIndex("nope") == -1, "unknown id -> -1");
    root = nlohmann::json::parse(ReadAll(path + ".meta"));
    CHECK_SOFT(root.value("guid", std::string()) == guid, "guid still preserved");

    // A legacy meta (only the original three keys) loads as Single mode with defaults.
    { std::ofstream(path + ".meta") << R"({"guid":"x","Importer":{"FilterMode":"Point","WrapMode":"Clamp","GenerateMipmaps":false}})"; }
    const TextureImportSettings legacy = TextureImportSettings::Load(path);
    CHECK_SOFT(legacy.SpriteMode == SpriteImportMode::Single && legacy.Sprites.empty() && !legacy.HasSpritePivot && legacy.Width == 0,
        "legacy meta = Single mode, no sprites");
    std::filesystem::remove(path + ".meta");
}

TEST_CASE("Sprite references: guid#id parsing and AssetDatabase resolves the texture part") {
    CHECK_SOFT(SpriteRef::Make("abc", "") == "abc", "no id = whole texture");
    CHECK_SOFT(SpriteRef::Make("abc", "12") == "abc#12", "guid#id");
    CHECK_SOFT(SpriteRef::TextureGuid("abc#12") == "abc" && SpriteRef::SpriteId("abc#12") == "12", "split");
    CHECK_SOFT(SpriteRef::TextureGuid("abc") == "abc" && SpriteRef::SpriteId("abc").empty(), "plain guid");
    CHECK_SOFT(SpriteRef::IsSubSprite("abc#1") && !SpriteRef::IsSubSprite("abc"), "IsSubSprite");
    AssetDatabase::Register("feedfacefeedfacefeedfacefeedface", "Textures/sheet.png");
    CHECK_SOFT(AssetDatabase::ResolvePath("feedfacefeedfacefeedfacefeedface#0011") == "Textures/sheet.png", "sub-sprite resolves to its texture");
}

TEST_CASE("SpriteDatabase resolves sub-sprite UVs, pivots, borders and falls back to the whole texture") {
    SheetFixture sheet;
    ResolvedSprite hero;
    CHECK(SpriteDatabase::Resolve(SpriteRef::Make(sheet.Guid, "aaaa"), hero));
    CHECK_SOFT(hero.IsSubSprite() && hero.Name == "hero_0", "named sub-sprite");
    CHECK_SOFT(Near(hero.UV, { 0.0f, 0.0f, 0.25f, 1.0f }), "UV = rect / texture size");
    CHECK_SOFT(hero.HasPivot && Near(hero.Pivot, { 0.5f, 1.0f }), "sprite pivot");
    CHECK_SOFT(Near(hero.SizePixels, { 16.0f, 32.0f }) && Near(hero.PixelsPerUnit, 16.0f), "size + PPU");

    ResolvedSprite panel;
    CHECK(SpriteDatabase::Resolve(SpriteRef::Make(sheet.Guid, "bbbb"), panel));
    CHECK_SOFT(Near(panel.UV, { 0.5f, 0.0f, 1.0f, 0.5f }), "second sprite UV");
    CHECK_SOFT(Near(panel.BorderUV(), { 4.0f / 64.0f, 4.0f / 64.0f, 2.0f / 32.0f, 2.0f / 32.0f }), "border as texture fraction");

    ResolvedSprite whole;
    CHECK(SpriteDatabase::Resolve(sheet.Guid, whole));
    CHECK_SOFT(!whole.IsSubSprite() && Near(whole.UV, { 0, 0, 1, 1 }) && !whole.HasPivot, "plain guid = whole texture, renderer pivot");

    ResolvedSprite missing;
    CHECK(SpriteDatabase::Resolve(SpriteRef::Make(sheet.Guid, "deleted"), missing));
    CHECK_SOFT(!missing.IsSubSprite() && Near(missing.UV, { 0, 0, 1, 1 }), "unknown sprite id falls back to the whole texture");

    ResolvedSprite byIndex;
    CHECK_SOFT(SpriteDatabase::ResolveIndex(sheet.Guid, 1, byIndex) && byIndex.Name == "panel", "ResolveIndex (tilemaps)");
    CHECK_SOFT(!SpriteDatabase::ResolveIndex(sheet.Guid, 5, byIndex), "index out of range");
    CHECK_SOFT(SpriteDatabase::SpriteCount(sheet.Guid) == 2, "sprite count");
    CHECK_SOFT(!SpriteDatabase::Resolve("", whole) && !SpriteDatabase::Resolve("00000000000000000000000000000000", whole), "empty / unknown guid");
}

// ---- Slicing ----------------------------------------------------------------------------------

TEST_CASE("Sprite slicing: grid by cell size / cell count with offset, padding and empty cells") {
    // 4x2 cells of 8x8; cell (1,0) and (3,1) are transparent.
    std::vector<glm::ivec4> opaque;
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 4; ++col)
            if (!(col == 1 && row == 0) && !(col == 3 && row == 1))
                opaque.push_back({ col * 8 + 2, row * 8 + 2, 3, 3 });
    const std::vector<uint8_t> image = MakeImage(32, 16, opaque);

    const auto bySize = SpriteSlicer::GridByCellSize(image.data(), 32, 16, { 8, 8 });
    CHECK_SOFT(bySize.size() == 6, "empty cells dropped");
    CHECK_SOFT(bySize.front().X == 0 && bySize.front().Y == 0 && bySize.front().Width == 8, "first cell top-left");
    CHECK_SOFT(bySize[1].X == 16 && bySize[1].Y == 0, "row-major, skipping the empty cell");
    CHECK_SOFT(SpriteSlicer::GridByCellSize(image.data(), 32, 16, { 8, 8 }, {}, {}, true).size() == 8, "Keep Empty Rects");
    CHECK_SOFT(SpriteSlicer::GridByCellSize(nullptr, 32, 16, { 8, 8 }).size() == 8, "no pixels = keep all");

    const auto withPadding = SpriteSlicer::GridByCellSize(nullptr, 34, 16, { 8, 8 }, { 1, 0 }, { 3, 0 }, true);
    CHECK_SOFT(withPadding.size() == 6 && withPadding[1].X == 12, "offset 1 + padding 3 -> x = 1, 12, 23");

    const auto byCount = SpriteSlicer::GridByCellCount(image.data(), 32, 16, 4, 2);
    CHECK_SOFT(byCount.size() == 6 && byCount.front().Width == 8 && byCount.front().Height == 8, "4x2 count = 8px cells");
    CHECK_SOFT(SpriteSlicer::GridByCellCount(nullptr, 32, 16, 0, 2).empty(), "zero columns -> nothing");
}

TEST_CASE("Sprite slicing: automatic islands, merged overlaps, reading order, id reuse on re-slice") {
    // Two islands on row 0, one on row 1; a dot inside the first island's box merges into it.
    std::vector<uint8_t> image = MakeImage(40, 30, { { 2, 2, 6, 6 }, { 20, 3, 5, 4 }, { 4, 18, 10, 8 } });
    image[(static_cast<size_t>(4) * 40 + 4) * 4 + 3] = 0; // a hole does not split an island
    image[(static_cast<size_t>(12) * 40 + 30) * 4 + 3] = 255; // a lone 1px dot (dropped by minSize 2)
    const auto rects = SpriteSlicer::Automatic(image.data(), 40, 30, 2);
    CHECK(rects.size() == 3);
    CHECK_SOFT(rects[0].X == 2 && rects[0].Y == 2 && rects[0].Width == 6 && rects[0].Height == 6, "first island bounds");
    CHECK_SOFT(rects[1].X == 20, "same row, left to right");
    CHECK_SOFT(rects[2].Y == 18, "next row last");
    CHECK_SOFT(SpriteSlicer::Automatic(image.data(), 40, 30, 1).size() == 4, "minSize 1 keeps the dot");

    // Diagonal pixels are 8-connected.
    std::vector<uint8_t> diagonal = MakeImage(4, 4, {});
    for (int i = 0; i < 4; ++i)
        diagonal[(static_cast<size_t>(i) * 4 + i) * 4 + 3] = 255;
    CHECK_SOFT(SpriteSlicer::Automatic(diagonal.data(), 4, 4, 1).size() == 1, "8-connectivity");

    std::vector<SpriteDefinition> existing(1);
    existing[0].Id = "keepme";
    existing[0].Name = "sheet_1";
    existing[0].Border = { 1, 1, 1, 1 };
    const auto sprites = SpriteSlicer::MakeSprites(rects, "sheet", { 0.5f, 0.0f }, existing);
    CHECK(sprites.size() == 3);
    CHECK_SOFT(sprites[0].Name == "sheet_0" && sprites[2].Name == "sheet_2", "names <base>_<i>");
    CHECK_SOFT(sprites[1].Id == "keepme" && sprites[1].Border == glm::vec4(1.0f), "same name keeps id + border (references survive re-slicing)");
    CHECK_SOFT(sprites[0].Id != sprites[2].Id && sprites[0].Id.size() == 16 && sprites[0].Id != "keepme", "fresh unique ids");
    CHECK_SOFT(sprites[0].Pivot == glm::vec2(0.5f, 0.0f), "slice pivot applied");
}

// ---- Drawing ----------------------------------------------------------------------------------

TEST_CASE("DrawSprite2D: pivot, flip around the pivot, rotation and 9-slice patches") {
    RecordingRenderer renderer;
    SpriteDrawData sprite;
    sprite.PivotPosition = { 100.0f, 50.0f };
    sprite.Size = { 20.0f, 10.0f };
    sprite.Pivot = { 0.0f, 0.0f };
    sprite.TextureId = 7;
    sprite.UV = { 0.25f, 0.0f, 0.5f, 0.5f };
    CHECK(DrawSprite2D(renderer, sprite) == 1);
    CHECK_SOFT(Near(renderer.Quads[0].Position, { 100.0f, 50.0f }) && Near(renderer.Quads[0].Size, { 20.0f, 10.0f }), "top-left pivot");
    CHECK_SOFT(Near(renderer.Quads[0].UV, sprite.UV), "sub-sprite UV passed through");

    renderer.Quads.clear();
    sprite.FlipX = true;
    DrawSprite2D(renderer, sprite);
    // Mirrored around the pivot: covers x = 80..100, drawn with a negative width.
    const auto& flipped = renderer.Quads[0];
    CHECK_SOFT(flipped.Size.x < 0.0f && Near(flipped.Position.x + flipped.Size.x * 0.5f, 90.0f), "flip X mirrors around the pivot");

    renderer.Quads.clear();
    sprite.FlipX = false;
    sprite.Pivot = { 0.5f, 0.5f };
    sprite.RotationDegrees = 90.0f;
    DrawSprite2D(renderer, sprite);
    CHECK_SOFT(Near(renderer.Quads[0].Position + renderer.Quads[0].Size * 0.5f, { 100.0f, 50.0f }, 0.001f), "center pivot: rotation keeps the center");

    renderer.Quads.clear();
    sprite.Pivot = { 0.0f, 0.0f };
    DrawSprite2D(renderer, sprite);
    // Rect center (10, 5) relative to the pivot rotated by 90 degrees -> (-5, 10).
    CHECK_SOFT(Near(renderer.Quads[0].Position + renderer.Quads[0].Size * 0.5f, { 95.0f, 60.0f }, 0.001f), "rotation is around the pivot (Unity)");

    renderer.Quads.clear();
    sprite.RotationDegrees = 0.0f;
    sprite.Size = { 40.0f, 20.0f };
    sprite.Sliced = true;
    sprite.BorderDraw = { 4.0f, 4.0f, 2.0f, 2.0f };
    sprite.BorderUV = { 0.05f, 0.05f, 0.1f, 0.1f };
    CHECK(DrawSprite2D(renderer, sprite) == 9);
    CHECK_SOFT(Near(renderer.Quads[0].Size, { 4.0f, 2.0f }) && Near(renderer.Quads[0].UV, { 0.25f, 0.0f, 0.30f, 0.1f }), "top-left corner patch");
    CHECK_SOFT(Near(renderer.Quads[4].Size, { 32.0f, 16.0f }) && Near(renderer.Quads[4].UV, { 0.30f, 0.1f, 0.45f, 0.4f }), "stretched center patch");

    // Borders larger than the rect shrink proportionally instead of overlapping.
    renderer.Quads.clear();
    sprite.Size = { 4.0f, 20.0f };
    DrawSprite2D(renderer, sprite);
    CHECK_SOFT(Near(renderer.Quads[0].Size.x, 2.0f), "oversized borders fit the rect");

    CHECK_SOFT(Near(ComposeUV({ 0.5f, 0.0f, 1.0f, 0.5f }, { 0.5f, 0.5f, 1.0f, 1.0f }), { 0.75f, 0.25f, 1.0f, 0.5f }), "ComposeUV");
}

TEST_CASE("SpriteRenderer with a sliced sprite: sub-rect UVs, sprite pivot, Draw Mode Sliced border") {
    SheetFixture sheet;
    Scene scene;
    Entity entity = scene.CreateEntity("Hero");
    SpriteRendererComponent& sprite = entity.AddComponent<SpriteRendererComponent>();
    sprite.Texture.Guid = SpriteRef::Make(sheet.Guid, "aaaa");
    sprite.Size = { 1.0f, 2.0f };
    sprite.Pivot = { 0.5f, 0.5f }; // ignored: the sprite's own pivot wins

    RecordingRenderer renderer;
    SpriteDrawData data;
    CHECK(BuildSpriteRendererDrawData(renderer, scene, entity.Handle(), { 0.0f, 0.0f }, 16.0f, data));
    CHECK_SOFT(data.TextureId == 7 && Near(data.UV, { 0.0f, 0.0f, 0.25f, 1.0f }), "sub-rect UVs");
    CHECK_SOFT(Near(data.Pivot, { 0.5f, 1.0f }), "sprite pivot overrides SpriteRenderer::Pivot");
    CHECK_SOFT(Near(data.Size, { 16.0f, 32.0f }) && !data.Sliced, "Simple draw mode");
    CHECK_SOFT(Near(EffectiveSpritePivot(scene, entity.Handle()), { 0.5f, 1.0f }), "editor outlines use the same pivot");

    sprite.Texture.Guid = SpriteRef::Make(sheet.Guid, "bbbb");
    sprite.DrawMode = SpriteDrawMode::Sliced;
    CHECK(BuildSpriteRendererDrawData(renderer, scene, entity.Handle(), { 0.0f, 0.0f }, 16.0f, data));
    // Border px / texture PPU (16) * 16 draw units per world unit = 1 draw unit per source px.
    CHECK_SOFT(data.Sliced && Near(data.BorderDraw, { 4.0f, 4.0f, 2.0f, 2.0f }), "sprite border used by Draw Mode Sliced");
    CHECK_SOFT(Near(data.BorderUV, { 4.0f / 64.0f, 4.0f / 64.0f, 2.0f / 32.0f, 2.0f / 32.0f }), "border UVs from the texture size");

    // Legacy: a non-zero SliceBorder slices even in Simple mode and overrides the sprite's.
    sprite.DrawMode = SpriteDrawMode::Simple;
    sprite.SliceBorder = { 8.0f, 8.0f, 8.0f, 8.0f };
    CHECK(BuildSpriteRendererDrawData(renderer, scene, entity.Handle(), { 0.0f, 0.0f }, 16.0f, data));
    CHECK_SOFT(data.Sliced && Near(data.BorderDraw, { 8.0f, 8.0f, 8.0f, 8.0f }), "legacy SliceBorder");

    // Negative scale mirrors like Unity.
    sprite.SliceBorder = glm::vec4(0.0f);
    entity.GetComponent<TransformComponent>().Scale = { -1.0f, 1.0f, 1.0f };
    CHECK(BuildSpriteRendererDrawData(renderer, scene, entity.Handle(), { 0.0f, 0.0f }, 16.0f, data));
    CHECK_SOFT(data.FlipX && data.Size.x > 0.0f, "negative scale = flip");
}

TEST_CASE("SpriteRenderer Draw Mode and sprite references survive scene save/load") {
    SheetFixture sheet;
    Scene scene;
    Entity entity = scene.CreateEntity("Panel");
    SpriteRendererComponent& sprite = entity.AddComponent<SpriteRendererComponent>();
    sprite.Texture.Guid = SpriteRef::Make(sheet.Guid, "bbbb");
    sprite.DrawMode = SpriteDrawMode::Sliced;
    const nlohmann::json json = SceneSerializer(scene).SerializeToJson();
    Scene loaded;
    CHECK(SceneSerializer(loaded).DeserializeFromJson(json));
    Entity copy;
    for (auto handle : loaded.Registry().view<SpriteRendererComponent>())
        copy = Entity(handle, &loaded);
    CHECK(static_cast<bool>(copy));
    CHECK_SOFT(copy.GetComponent<SpriteRendererComponent>().Texture.Guid == sprite.Texture.Guid, "guid#id kept verbatim");
    CHECK_SOFT(copy.GetComponent<SpriteRendererComponent>().DrawMode == SpriteDrawMode::Sliced, "Draw Mode saved");

    // A scene saved before Draw Mode existed loads as Simple.
    nlohmann::json legacy = json;
    for (auto& e : legacy["Entities"])
        if (e.contains("Sprite Renderer"))
            e["Sprite Renderer"].erase("Draw Mode");
    Scene legacyScene;
    CHECK(SceneSerializer(legacyScene).DeserializeFromJson(legacy));
    for (auto handle : legacyScene.Registry().view<SpriteRendererComponent>())
        CHECK_SOFT(legacyScene.Registry().get<SpriteRendererComponent>(handle).DrawMode == SpriteDrawMode::Simple, "missing key = Simple");
}

TEST_CASE("Sprite Sheet Animator on a sliced texture plays its sprites in order") {
    SheetFixture sheet;
    Scene scene;
    Entity entity = scene.CreateEntity("Anim");
    entity.AddComponent<SpriteRendererComponent>();
    SpriteSheetAnimatorComponent& anim = entity.AddComponent<SpriteSheetAnimatorComponent>();
    anim.Texture.Guid = sheet.Guid;
    anim.Columns = 8; // ignored for a sliced sheet
    anim.Rows = 8;
    anim.FrameRate = 10.0f;
    UpdateSceneRuntimeSystems(scene, 0.15f);
    CHECK_SOFT(anim.CurrentFrame == 1, "advanced one frame");
    UpdateSceneRuntimeSystems(scene, 0.1f);
    CHECK_SOFT(anim.CurrentFrame == 0, "wraps after the 2 sliced sprites, not 64 grid cells");
    RecordingRenderer renderer;
    SpriteDrawData data;
    anim.CurrentFrame = 1;
    CHECK(BuildSpriteRendererDrawData(renderer, scene, entity.Handle(), {}, 1.0f, data));
    CHECK_SOFT(Near(data.UV, { 0.5f, 0.0f, 1.0f, 0.5f }), "frame 1 = second sprite");
}

// ---- Tilemaps ---------------------------------------------------------------------------------

TEST_CASE("Tilemap brush operations: set, box fill, flood fill, line, stamp, resize, save") {
    TilemapData data;
    data.Width = 5;
    data.Height = 4;
    TilemapEdit::Normalize(data);
    CHECK_SOFT(data.Tiles.size() == 20 && TilemapEdit::Get(data, 0, 0) == -1, "normalized empty grid");
    CHECK_SOFT(TilemapEdit::Set(data, 1, 1, 3) && !TilemapEdit::Set(data, 1, 1, 3), "set reports changes only");
    CHECK_SOFT(!TilemapEdit::Set(data, 7, 1, 3) && TilemapEdit::Get(data, -1, 0) == -1, "outside = no-op");
    CHECK_SOFT(TilemapEdit::BoxFill(data, { 3, 2 }, { 0, 0 }, 2) == 12 && TilemapEdit::Get(data, 3, 2) == 2, "box fill any corner order");
    // Flood fill the empty region (cells with -1): the 5x4 grid minus the 4x3 box = 8 cells.
    CHECK_SOFT(TilemapEdit::FloodFill(data, 4, 3, 5) == 8, "flood fill 4-connected region");
    CHECK_SOFT(TilemapEdit::FloodFill(data, 4, 3, 5) == 0, "same value = nothing to do");
    const auto line = TilemapEdit::Line({ 0, 0 }, { 3, 1 });
    CHECK_SOFT(line.size() == 4 && line.front() == glm::ivec2(0, 0) && line.back() == glm::ivec2(3, 1), "line without gaps");
    CHECK_SOFT(TilemapEdit::Stamp(data, { 3, 2 }, { 2, 2 }, { 9, -1, 9, 9 }) == 3, "stamp skips -1 and clips at the edge");
    CHECK_SOFT(TilemapEdit::Get(data, 4, 2) == 5, "skipped cell untouched");
    TilemapEdit::Resize(data, 3, 2);
    CHECK_SOFT(data.Width == 3 && data.Height == 2 && data.Tiles.size() == 6 && TilemapEdit::Get(data, 1, 1) == 2, "resize keeps tiles");

    const std::string path = TempPath("duality_tilemap_test.tilemap");
    CHECK(TilemapAsset::Save(path, data));
    TilemapAsset::Invalidate(path);
    const TilemapData& loaded = TilemapAsset::Get(path);
    CHECK_SOFT(loaded.Width == 3 && loaded.Tiles == data.Tiles, "saved file loads back (same format as before)");
    std::filesystem::remove(path);
    TilemapAsset::Invalidate(path);
}

TEST_CASE("Tilemap tiles draw the tileset's sliced sprites by index") {
    SheetFixture sheet;
    const std::string path = TempPath("duality_tilemap_draw.tilemap");
    TilemapData data;
    data.Width = 2;
    data.Height = 1;
    data.Tiles = { 1, 0 };
    TilemapAsset::Save(path, data);
    const std::string guid = AssetMeta::EnsureMetaFile(path);
    AssetDatabase::Register(guid, path);

    Scene scene;
    Entity entity = scene.CreateEntity("Ground");
    TilemapComponent& tilemap = entity.AddComponent<TilemapComponent>();
    tilemap.Tileset.Guid = sheet.Guid;
    tilemap.TileData.Guid = guid;
    tilemap.CellSize = { 1.0f, 1.0f };
    RecordingRenderer renderer;
    CHECK(DrawTilemap2D(renderer, scene, entity.Handle(), { 0.0f, 0.0f }, 10.0f, { 0.0f, 0.0f }) == 2);
    CHECK_SOFT(Near(renderer.Quads[0].UV, { 0.5f, 0.0f, 1.0f, 0.5f }) && Near(renderer.Quads[1].UV, { 0.0f, 0.0f, 0.25f, 1.0f }), "tile index -> sprite UV");
    CHECK_SOFT(Near(renderer.Quads[1].Position, { 5.0f, -5.0f }), "cell centered on Translation + x * CellSize");
    std::filesystem::remove(path);
    std::filesystem::remove(path + ".meta");
    TilemapAsset::Invalidate(path);
}

// ---- Animation clips --------------------------------------------------------------------------

TEST_CASE("Animation clips: legacy files load unchanged, curves round-trip, sampling and key editing") {
    AnimationClipData legacy;
    CHECK(AnimationClipLoader::ParseJson(R"({"Duration":2,"Channels":[{"Node":"Cube","Position":[[0,[0,0,0]],[2,[4,0,0]]]}]})", legacy));
    CHECK_SOFT(legacy.Channels.size() == 1 && legacy.Curves.empty() && legacy.SampleRate == 60.0f, "legacy clip: channels only, default sample rate");
    CHECK_SOFT(Near(AnimationClipLoader::Sample(legacy.Channels[0].PositionKeys, 1.0f, {}).x, 2.0f), "channel sampling unchanged");

    AnimationClipData clip;
    clip.SampleRate = 12.0f;
    AnimationFloatCurve x{ "", "Transform", "Translation.x", AnimationInterpolation::Linear, {} };
    AnimationClipLoader::SetKey(x, 1.0f, 10.0f);
    AnimationClipLoader::SetKey(x, 0.0f, 0.0f);
    AnimationClipLoader::SetKey(x, 1.0f, 20.0f); // replaces the key at t=1
    clip.Curves.push_back(x);
    AnimationFloatCurve enabled{ "Child", "Sprite Renderer", "Flip X", AnimationInterpolation::Constant, { { 0.0f, 0.0f }, { 0.5f, 1.0f } } };
    clip.Curves.push_back(enabled);
    AnimationObjectCurve frames{ "", "Sprite Renderer", "Texture", { { 0.0f, "g#a" }, { 0.25f, "g#b" } } };
    clip.ObjectCurves.push_back(frames);
    clip.Duration = 1.0f;

    CHECK_SOFT(clip.Curves[0].Keys.size() == 2 && clip.Curves[0].Keys[0].Time == 0.0f, "SetKey sorts and replaces");
    CHECK_SOFT(Near(AnimationClipLoader::Sample(clip.Curves[0], 0.5f, -1.0f), 10.0f), "linear interpolation");
    CHECK_SOFT(Near(AnimationClipLoader::Sample(clip.Curves[0], 5.0f, -1.0f), 20.0f), "holds the last key");
    CHECK_SOFT(Near(AnimationClipLoader::Sample(clip.Curves[1], 0.49f, -1.0f), 0.0f), "constant interpolation steps");
    CHECK_SOFT(*AnimationClipLoader::Sample(clip.ObjectCurves[0], 0.3f) == "g#b" && *AnimationClipLoader::Sample(clip.ObjectCurves[0], 0.1f) == "g#a", "sprite keys step");
    CHECK_SOFT(Near(clip.LastKeyTime(), 1.0f), "last key time");

    AnimationClipData reloaded;
    CHECK(AnimationClipLoader::ParseJson(AnimationClipLoader::ToJson(clip), reloaded));
    CHECK_SOFT(reloaded.SampleRate == 12.0f && reloaded.Curves.size() == 2 && reloaded.ObjectCurves.size() == 1, "curves round-trip");
    CHECK_SOFT(reloaded.Curves[1].Interpolation == AnimationInterpolation::Constant && reloaded.Curves[1].Path == "Child", "curve metadata round-trips");
    CHECK_SOFT(reloaded.ObjectCurves[0].Keys[1].Value == "g#b", "object keys round-trip");

    AnimationClipData withChannels;
    AnimationClipLoader::ParseJson(AnimationClipLoader::ToJson(legacy), withChannels);
    CHECK_SOFT(withChannels.Channels.size() == 1 && withChannels.Channels[0].PositionKeys.size() == 2, "channels survive a save");

    const std::string path = TempPath("duality_clip_test.anim");
    CHECK(AnimationClipLoader::Save(path, clip));
    AnimationClipLoader::Invalidate(path);
    CHECK_SOFT(AnimationClipLoader::Load(path).Curves.size() == 2, "Save + Load");
    std::filesystem::remove(path);
    AnimationClipLoader::Invalidate(path);
}

TEST_CASE("AnimationPlayback samples property and sprite curves onto the entity tree") {
    Scene scene;
    Entity root = scene.CreateEntity("Player");
    Entity child = scene.CreateEntity("Sword");
    scene.SetParent(child, root);
    root.AddComponent<SpriteRendererComponent>();
    child.AddComponent<SpriteRendererComponent>();

    CHECK_SOFT(AnimationPlayback::FindTarget(scene, root, "Sword") == child && AnimationPlayback::FindTarget(scene, root, "") == root, "FindTarget");
    CHECK_SOFT(!AnimationPlayback::FindTarget(scene, root, "Nope"), "missing path");
    std::string path;
    CHECK_SOFT(AnimationPlayback::RelativePath(root, child, path) && path == "Sword", "RelativePath");

    float value = 0.0f;
    CHECK_SOFT(AnimationPlayback::WriteFloat(root, "Sprite Renderer", "Color.a", 0.5f) && AnimationPlayback::ReadFloat(root, "Sprite Renderer", "Color.a", value) && Near(value, 0.5f), "color channel");
    CHECK_SOFT(AnimationPlayback::WriteFloat(root, "Sprite Renderer", "Flip X", 1.0f) && root.GetComponent<SpriteRendererComponent>().FlipX, "bool property");
    CHECK_SOFT(!AnimationPlayback::WriteFloat(root, "Sprite Renderer", "Texture", 1.0f), "AssetRef is not a float");
    const auto properties = AnimationPlayback::AnimatableProperties(root, "Transform");
    CHECK_SOFT(properties.size() == 9 && properties.front().Property == "Translation.x", "Transform exposes 9 scalars");

    AnimationClipData clip;
    clip.Curves.push_back({ "", "Transform", "Translation.x", AnimationInterpolation::Linear, { { 0.0f, 0.0f }, { 1.0f, 4.0f } } });
    clip.Curves.push_back({ "Sword", "Transform", "Rotation.z", AnimationInterpolation::Linear, { { 0.0f, 0.0f }, { 1.0f, 90.0f } } });
    clip.ObjectCurves.push_back({ "", "Sprite Renderer", "Texture", { { 0.0f, "tex#a" }, { 0.5f, "tex#b" } } });
    AnimationPlayback::Sample(scene, root, clip, 0.75f);
    CHECK_SOFT(Near(root.GetComponent<TransformComponent>().Translation.x, 3.0f), "root curve applied");
    CHECK_SOFT(Near(child.GetComponent<TransformComponent>().Rotation.z, 67.5f), "child curve applied by path");
    CHECK_SOFT(root.GetComponent<SpriteRendererComponent>().Texture.Guid == "tex#b", "sprite keyframe applied");

    // The Animation component drives the same sampling at runtime.
    const std::string clipPath = TempPath("duality_playback_test.anim");
    clip.Duration = 1.0f;
    AnimationClipLoader::Save(clipPath, clip);
    const std::string guid = AssetMeta::EnsureMetaFile(clipPath);
    AssetDatabase::Register(guid, clipPath);
    AnimationComponent& animation = root.AddComponent<AnimationComponent>();
    animation.Clip.Guid = guid;
    animation.Time = 0.0f;
    UpdateSceneRuntimeSystems(scene, 0.25f);
    CHECK_SOFT(Near(root.GetComponent<TransformComponent>().Translation.x, 1.0f), "runtime Animation component samples curves");
    CHECK_SOFT(root.GetComponent<SpriteRendererComponent>().Texture.Guid == "tex#a", "runtime sprite frame");
    UpdateSceneRuntimeSystems(scene, 1.0f); // loops: 1.25 -> 0.25
    CHECK_SOFT(Near(animation.Time, 0.25f, 0.001f), "loops at the clip length");
    std::filesystem::remove(clipPath);
    std::filesystem::remove(clipPath + ".meta");
    AnimationClipLoader::Invalidate(clipPath);
}

// ---- Profiler ---------------------------------------------------------------------------------

TEST_CASE("Profiler records nested samples and counters only while recording") {
    Profiler::Clear();
    Profiler::SetRecording(false);
    Profiler::BeginFrame();
    { DUALITY_PROFILE_SCOPE("Ignored"); }
    Profiler::EndFrame();
    CHECK_SOFT(Profiler::Frames().empty(), "nothing recorded while off");

    Profiler::SetRecording(true);
    Profiler::BeginFrame();
    {
        DUALITY_PROFILE_SCOPE("PlayerLoop");
        { DUALITY_PROFILE_SCOPE("Scripts.C++"); }
        { DUALITY_PROFILE_SCOPE("Physics.FixedStep"); }
    }
    Profiler::SetCounter("Draw Calls", 12);
    Profiler::SetCounter("Draw Calls", 13);
    Profiler::BeginSample("Unclosed");
    Profiler::EndFrame();
    Profiler::SetRecording(false);
    CHECK(Profiler::Frames().size() == 1);
    const Profiler::Frame& frame = Profiler::Frames().back();
    CHECK(frame.Samples.size() == 4);
    CHECK_SOFT(frame.Samples[1].Parent == 0 && frame.Samples[1].Depth == 1 && frame.Samples[2].Parent == 0, "children point at PlayerLoop");
    CHECK_SOFT(frame.Samples[0].DurationMs >= frame.Samples[1].DurationMs, "parent covers its children");
    CHECK_SOFT(frame.CounterValue("Draw Calls") == 13.0 && frame.Counters.size() == 1, "last counter write wins");
    CHECK_SOFT(frame.Samples[3].DurationMs >= 0.0 && frame.DurationMs >= frame.Samples[0].DurationMs, "unclosed sample closed at frame end");
    CHECK_SOFT(frame.TotalMs("Scripts.C++") >= 0.0, "TotalMs");
    Profiler::Clear();
}

// ---- Scene history tracking tool-edited files ----------------------------------------------------

TEST_CASE("SceneHistory: a tracked asset file (tilemap / clip) is part of Undo/Redo") {
    const std::string path = TempPath("duality_history_tracked.tilemap");
    { std::ofstream(path) << "v1"; }
    Scene scene;
    EntityIdRegistry ids;
    scene.CreateEntity("A");
    SceneHistory history;
    history.Reset(scene, ids);
    history.TrackFile(path); // before the first modification
    CHECK_SOFT(history.IsTrackingFile(path), "tracking");

    { std::ofstream(path, std::ios::trunc) << "v2"; }
    CHECK(history.Capture(scene, ids, "Paint Tiles"));
    CHECK_SOFT(history.UndoLabel() == "Paint Tiles", "a file-only change is an undo step");
    CHECK_SOFT(!history.Capture(scene, ids, "Nothing"), "unchanged file + scene = no step");
    CHECK_SOFT(history.IsAtSavedState(), "the scene itself is still at its saved state");

    const uint64_t serial = SceneHistory::RestoredFileSerial();
    CHECK(history.Undo(scene, ids));
    CHECK_SOFT(ReadAll(path) == "v1", "undo restores the file");
    CHECK_SOFT(SceneHistory::RestoredFileSerial() == serial + 1 && SceneHistory::LastRestoredFiles().size() == 1, "tools are notified");
    CHECK(history.Redo(scene, ids));
    CHECK_SOFT(ReadAll(path) == "v2", "redo re-applies it");
    std::filesystem::remove(path);
}
