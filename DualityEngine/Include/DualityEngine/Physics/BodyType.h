#pragma once

namespace Duality {

    // Lives in its own header (not Components.h, where Rigidbody2D/3DComponent actually use it)
    // for the same reason Screen.h/ProjectionType.h do: Reflection/Field.h needs this type in
    // its FieldValue variant, but Field.h can't include Components.h (which itself includes
    // Field.h).
    //
    // Unity's own Rigidbody/Rigidbody2D BodyType split, replacing the old plain
    // `bool IsStatic` -- Kinematic fills a real gap that boolean couldn't express: a body moved
    // directly (by a script setting Transform, or an animation) that should still generate
    // collisions and push Dynamic bodies around, but never be affected by gravity/forces itself.
    // Maps directly onto both backends' own native concepts: Box2D's b2_staticBody/
    // b2_kinematicBody/b2_dynamicBody, and Bullet's mass==0 (Static/Kinematic both) +
    // btCollisionObject::CF_KINEMATIC_OBJECT (Kinematic only, plus DISABLE_DEACTIVATION so it
    // never gets put to sleep) -- see Scene.cpp's body-creation code for both.
    enum class BodyType {
        Static,
        Kinematic,
        Dynamic
    };

}
