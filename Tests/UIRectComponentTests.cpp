// UIRectComponent RectTransform-style resolution + legacy Anchor/Offset/Size migration
// coverage -- see UIAnchor.h's LegacyUIAnchorToRectTransform/UIAnchorPresetToMinMaxPivot and
// UIRenderer.cpp's ResolveUIRect for the code under test.
#include "TestFramework.h"

#include <cmath>
#include <string>

#include <nlohmann/json.hpp>

#include "DualityEngine/Renderer/UIAnchor.h"
#include "DualityEngine/Renderer/UIRenderer.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneSerializer.h"

using namespace Duality;
using json = nlohmann::json;

namespace {
    // Independent oracle: a reimplementation of the pre-RectTransform ResolveUIRect switch
    // (quoted verbatim from the engine before this migration was written), kept ONLY here to
    // verify LegacyUIAnchorToRectTransform's conversion is exact. The real engine no longer
    // has this code path -- UIRectComponent has no Anchor/Offset/Size fields any more.
    glm::vec2 OldResolveTopLeft(UIAnchor anchor, glm::vec2 offset, glm::vec2 size, glm::vec2 parentSize) {
        float x, y;
        switch (anchor) {
            case UIAnchor::TopCenter:    x = parentSize.x * 0.5f + offset.x - size.x * 0.5f; y = offset.y; break;
            case UIAnchor::TopRight:     x = parentSize.x - offset.x - size.x; y = offset.y; break;
            case UIAnchor::MiddleLeft:   x = offset.x; y = parentSize.y * 0.5f + offset.y - size.y * 0.5f; break;
            case UIAnchor::MiddleCenter: x = parentSize.x * 0.5f + offset.x - size.x * 0.5f; y = parentSize.y * 0.5f + offset.y - size.y * 0.5f; break;
            case UIAnchor::MiddleRight:  x = parentSize.x - offset.x - size.x; y = parentSize.y * 0.5f + offset.y - size.y * 0.5f; break;
            case UIAnchor::BottomLeft:   x = offset.x; y = parentSize.y - offset.y - size.y; break;
            case UIAnchor::BottomCenter: x = parentSize.x * 0.5f + offset.x - size.x * 0.5f; y = parentSize.y - offset.y - size.y; break;
            case UIAnchor::BottomRight:  x = parentSize.x - offset.x - size.x; y = parentSize.y - offset.y - size.y; break;
            case UIAnchor::TopLeft:
            default:                     x = offset.x; y = offset.y; break;
        }
        return { x, y };
    }

    const UIAnchor kAllAnchors[9] = {
        UIAnchor::TopLeft, UIAnchor::TopCenter, UIAnchor::TopRight,
        UIAnchor::MiddleLeft, UIAnchor::MiddleCenter, UIAnchor::MiddleRight,
        UIAnchor::BottomLeft, UIAnchor::BottomCenter, UIAnchor::BottomRight,
    };
    const char* kAnchorNames[9] = {
        "TopLeft", "TopCenter", "TopRight", "MiddleLeft", "MiddleCenter", "MiddleRight",
        "BottomLeft", "BottomCenter", "BottomRight",
    };
}

TEST_CASE("LegacyUIAnchorToRectTransform reproduces the exact old pixel position for all 9 anchor presets") {
    glm::vec2 offset{ 12.0f, 7.0f };
    glm::vec2 size{ 80.0f, 32.0f };
    glm::vec2 parentSize{ 400.0f, 240.0f }; // Top screen dimensions

    Scene scene;
    for (int i = 0; i < 9; i++) {
        Entity e = scene.CreateEntity("UIRectMigrationTest");
        auto& rect = e.AddComponent<UIRectComponent>();
        rect.Screen = Screen::Top;
        LegacyUIAnchorToRectTransform(kAllAnchors[i], offset, size,
            rect.AnchorMin, rect.AnchorMax, rect.Pivot, rect.AnchoredPosition, rect.SizeDelta);

        glm::vec2 expected = OldResolveTopLeft(kAllAnchors[i], offset, size, parentSize);
        glm::vec2 topLeft, resolvedSize;
        ResolveUIRect(scene, e, topLeft, resolvedSize);

        std::string posWhat = std::string(kAnchorNames[i]) + ": resolved pixel position matches the old formula exactly";
        CHECK_SOFT(std::fabs(topLeft.x - expected.x) < 0.001f && std::fabs(topLeft.y - expected.y) < 0.001f, posWhat.c_str());
        std::string sizeWhat = std::string(kAnchorNames[i]) + ": SizeDelta preserves the old Size exactly";
        CHECK_SOFT(resolvedSize.x == size.x && resolvedSize.y == size.y, sizeWhat.c_str());
    }
}

TEST_CASE("UIAnchorPresetToMinMaxPivot only snaps the anchor triple, matching the legacy fraction table") {
    glm::vec2 anchorMin, anchorMax, pivot;
    UIAnchorPresetToMinMaxPivot(UIAnchor::BottomRight, anchorMin, anchorMax, pivot);
    CHECK_SOFT(anchorMin.x == 1.0f && anchorMin.y == 1.0f, "BottomRight preset -> AnchorMin (1,1)");
    CHECK_SOFT(anchorMax == anchorMin && pivot == anchorMin, "preset always produces a point anchor (Min == Max == Pivot)");

    UIAnchorPresetToMinMaxPivot(UIAnchor::TopCenter, anchorMin, anchorMax, pivot);
    CHECK_SOFT(anchorMin.x == 0.5f && anchorMin.y == 0.0f, "TopCenter preset -> AnchorMin (0.5, 0)");
}

TEST_CASE("Old-format scene JSON (Anchor/Offset/Size) auto-migrates on load to the same resolved pixel rect") {
    json entityJson;
    entityJson["Name"] = { { "Name", "LegacyUIEntity" } };
    entityJson["UI Rect"] = {
        { "Enabled", true },
        { "Screen", "Top" },
        { "Anchor", "BottomRight" },
        { "Offset", { 10.0, 10.0 } },
        { "Size", { 80.0, 32.0 } },
        { "Sort Order", 0 },
    };
    entityJson["Parent"] = -1;

    json root;
    root["Entities"] = json::array({ entityJson });

    Scene scene;
    CHECK(SceneSerializer(scene).DeserializeFromJson(root));

    Entity loaded;
    for (auto handle : scene.Registry().view<UIRectComponent>())
        loaded = Entity(handle, &scene);
    CHECK(loaded);

    glm::vec2 topLeft, size;
    ResolveUIRect(scene, loaded, topLeft, size);
    glm::vec2 expected = OldResolveTopLeft(UIAnchor::BottomRight, { 10.0f, 10.0f }, { 80.0f, 32.0f }, { 400.0f, 240.0f });
    CHECK_SOFT(std::fabs(topLeft.x - expected.x) < 0.001f && std::fabs(topLeft.y - expected.y) < 0.001f,
        "legacy scene JSON resolves to the exact same pixel position as the old renderer would have produced");
    CHECK_SOFT(size.x == 80.0f && size.y == 32.0f, "legacy Size survives migration as SizeDelta");

    auto& rect = loaded.GetComponent<UIRectComponent>();
    CHECK_SOFT(rect.Enabled, "Enabled still applied via the generic field loop after the legacy-migration branch");
    CHECK_SOFT(rect.Screen == Screen::Top, "Screen still applied via the generic field loop after the legacy-migration branch");
}

TEST_CASE("ResolveUIRect stretch mode: a horizontally-stretched rect fills the parent minus SizeDelta margins") {
    Scene scene;
    Entity e = scene.CreateEntity("StretchedUIRect");
    auto& rect = e.AddComponent<UIRectComponent>();
    rect.Screen = Screen::Top; // 400x240
    rect.AnchorMin = { 0.0f, 0.5f };
    rect.AnchorMax = { 1.0f, 0.5f }; // stretch X, point-anchor Y at vertical middle
    rect.Pivot = { 0.5f, 0.5f };
    rect.AnchoredPosition = { 0.0f, 0.0f };
    rect.SizeDelta = { -20.0f, 40.0f }; // 10px margin each side on X; Y is a plain 40px point size

    glm::vec2 topLeft, size;
    ResolveUIRect(scene, e, topLeft, size);

    CHECK_SOFT(size.x == 380.0f, "stretched X axis: (400 - 0) + (-20) == 380");
    CHECK_SOFT(topLeft.x == 10.0f, "stretched X axis centers the 380-wide rect in the 400-wide parent -> 10px margin each side");
    CHECK_SOFT(size.y == 40.0f, "point-anchored Y axis: size == SizeDelta.y exactly, unaffected by the X stretch");
    CHECK_SOFT(topLeft.y == 100.0f, "point-anchored Y axis at the vertical middle: 240*0.5 - 40*0.5 == 100");
}
