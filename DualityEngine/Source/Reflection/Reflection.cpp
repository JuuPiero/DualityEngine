#include "DualityEngine/Reflection/Reflection.h"

#include "DualityEngine/Reflection/TypeRegistry.h"
#include "../Scene/Components/ComponentRegistration.h"

namespace Duality {

    void RegisterBuiltinComponents() {
        if (!TypeRegistry::All().empty())
            return;

        // One registration file per domain (Source/Scene/Components/<Domain>Components.cpp).
        // The order here is the Inspector card order and the Add Component list order.
        using namespace ComponentRegistration;
        RegisterCoreComponents();
        RegisterRendering2DComponents();
        RegisterAnimationComponents();
        RegisterRendering3DComponents();
        RegisterAudioComponents();
        RegisterUIComponents();
        RegisterPhysics2DComponents();
        RegisterPhysics3DComponents();
        RegisterConstraintComponents();
        RegisterScriptingComponents(); // "Scripts" last: script cards follow the built-in components
    }

}
