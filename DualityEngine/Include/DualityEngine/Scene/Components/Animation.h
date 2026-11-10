#pragma once

// Animation components: the transform-clip Animation player, Sprite Flipbook and Sprite Sheet
// Animator. Part of Scene/Components.h.

#include <glm/glm.hpp>

#include "DualityEngine/Reflection/Field.h"

namespace Duality {

    // Minimal transform-animation player. Clip refers to a cooked .anim child from a source
    // model. The player applies the channel whose node name matches this entity name (or the
    // sole channel in a one-node clip); full skeleton-to-hierarchy binding is a later layer.
    struct AnimationComponent {
        bool Enabled = true;
        AssetRef Clip;
        bool PlayOnAwake = true;
        bool Loop = true;
        float Speed = 1.0f;
        bool Playing = true;

        // Runtime state: reset when Play starts and never serialized into a scene.
        float Time = 0.0f;
    };

    // Flipbook-style 2D animation: an ordered, fixed-size list of frame
    // textures played back at a constant rate. A fixed 8 slots (not an
    // unbounded list) since the reflection system (Field.h) only models
    // scalar fields, one ImGui widget each -- a real variable-length list
    // editor would need its own, larger extension to TypeRegistry/
    // SceneSerializer/PropertiesPanel. Only advances during Play
    // (Scene::OnRuntimeUpdate), same as Behaviour/physics -- no Edit-mode
    // preview-scrubbing.
    struct SpriteFlipbookComponent {
        bool Enabled = true;
        AssetRef Frame0, Frame1, Frame2, Frame3, Frame4, Frame5, Frame6, Frame7;
        float FrameDuration = 0.1f;
        bool Loop = true;
        bool Playing = true;

        // Runtime-only, like Rigidbody2DComponent::RuntimeBody -- not
        // reflected/serialized (TypeRegistry only registers the fields
        // above).
        float ElapsedTime = 0.0f;
        int CurrentFrame = 0;
    };

    // Frame0..Frame7 accessed by index -- shared by Scene.cpp (advancing
    // CurrentFrame) and the renderer (picking which frame to draw), so the
    // switch lives in exactly one place.
    inline AssetRef& GetFlipbookFrame(SpriteFlipbookComponent& flipbook, int index) {
        switch (index) {
            case 0: return flipbook.Frame0;
            case 1: return flipbook.Frame1;
            case 2: return flipbook.Frame2;
            case 3: return flipbook.Frame3;
            case 4: return flipbook.Frame4;
            case 5: return flipbook.Frame5;
            case 6: return flipbook.Frame6;
            default: return flipbook.Frame7;
        }
    }

    // Atlas-based sprite animation (UV sub-rects) -- alternative to fixed-frame flipbook.
    struct SpriteSheetAnimatorComponent {
        bool Enabled = true;
        AssetRef Texture;
        int Columns = 4;
        int Rows = 4;
        float FrameRate = 8.0f;
        bool Loop = true;
        bool Playing = true;
        float ElapsedTime = 0.0f;
        int CurrentFrame = 0;
    };

}
