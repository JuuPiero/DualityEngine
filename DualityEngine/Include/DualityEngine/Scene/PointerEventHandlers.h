#pragma once

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Renderer/Screen.h"

namespace Duality {

    // Payload for world-space pointer events dispatched by PhysicsRaycaster2DComponent /
    // PhysicsRaycaster3DComponent (Unity's PointerEventData, trimmed to what this engine needs today).
    struct PointerEventData {
        Screen Screen = Screen::Top;
        // Local pixel coordinates within `Screen` (top-left origin, Y-down) -- same space as
        // Input::GetPointerPosition().
        glm::vec2 Position{ 0.0f };
        // World-space contact point. 2D orthographic hits use z = 0; 3D perspective hits use
        // the actual Raycast3D impact point.
        glm::vec3 WorldPoint{ 0.0f };
        // The entity the raycaster hit this frame (falsy when the pointer missed everything).
        Entity PointerCurrentRaycastTarget;
        // The entity that received the current press (set on pointer-down, cleared on pointer-up).
        Entity PointerPressRaycastTarget;
        float Distance = 0.0f;
    };

    // Unity EventSystem handler interfaces -- implement by overriding the matching virtual on
    // Behaviour (Behaviour virtually inherits all of these with empty defaults, so a script only
    // needs `class Foo : public Behaviour` and can still say it "implements IPointerDownHandler"
    // by overriding OnPointerDown). Multiple inheritance `class Foo : public Behaviour, public
    // IPointerClickHandler` also works when you want the intent spelled out explicitly.
    struct IPointerEnterHandler {
        virtual ~IPointerEnterHandler() = default;
        virtual void OnPointerEnter(PointerEventData& eventData) = 0;
    };

    struct IPointerExitHandler {
        virtual ~IPointerExitHandler() = default;
        virtual void OnPointerExit(PointerEventData& eventData) = 0;
    };

    struct IPointerDownHandler {
        virtual ~IPointerDownHandler() = default;
        virtual void OnPointerDown(PointerEventData& eventData) = 0;
    };

    struct IPointerUpHandler {
        virtual ~IPointerUpHandler() = default;
        virtual void OnPointerUp(PointerEventData& eventData) = 0;
    };

    struct IPointerClickHandler {
        virtual ~IPointerClickHandler() = default;
        virtual void OnPointerClick(PointerEventData& eventData) = 0;
    };

}
