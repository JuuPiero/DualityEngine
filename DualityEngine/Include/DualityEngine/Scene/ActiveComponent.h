#pragma once

namespace Duality {

    // Unity's GameObject.activeSelf -- mandatory (added by Scene::CreateEntity alongside
    // Name/Tag/Transform/Hierarchy) so an old scene file saved before this component existed
    // still gets one with its default (Active) value on load, no migration code needed. A
    // single bool is deliberately NOT enough on its own to answer "does this entity actually
    // render/simulate/tick right now" -- see Scene::IsEffectivelyActive, which also walks up
    // HierarchyComponent::Parent (Unity's activeInHierarchy) since a child of a disabled
    // parent is implicitly disabled too, even if its own Active is still true.
    //
    // Defined in its own header (same reason MeshPrimitive.h/ProjectionType.h/UIAnchor.h
    // are standalone) rather than in Components.h: Behaviour.h's SetActive/IsActive need
    // ActiveComponent's complete definition inline, but Components.h includes Behaviour.h,
    // not the other way around -- a standalone header lets both include it without a cycle.
    struct ActiveComponent {
        bool Active = true;
    };

}
