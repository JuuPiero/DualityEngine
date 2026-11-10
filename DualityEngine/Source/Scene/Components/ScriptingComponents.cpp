// Registration of Scene/Components/Scripting.h.
#include "ComponentRegistration.h"

namespace Duality::ComponentRegistration {

    void RegisterScriptingComponents() {
        // Zero reflected fields: BehaviourComponent holds a vector of script slots, each with its
        // own class name + property overrides, so it can't use the generic one-FieldValue-per-
        // field path. PropertiesPanel and EntitySerialization special-case DisplayName ==
        // "Scripts" for the per-slot cards / JSON array. Registered (last, so script cards come
        // after the built-in components) so generic Has()/Remove() code still finds it; scripts
        // are added through Add Component > Scripts, not a category.
        Hidden<BehaviourComponent>("Scripts", {});
    }

}
