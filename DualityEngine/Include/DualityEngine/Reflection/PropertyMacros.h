#pragma once

#include <vector>

#include "DualityEngine/Reflection/Field.h"

// Real per-field Inspector attributes, Unreal-UPROPERTY()-style:
//
//   class PlayerController : public Duality::Behaviour {
//   public:
//       DUALITY_PROPERTY() float Speed = 5.0f;
//       DUALITY_PROPERTY() Duality::EntityRef Target;
//
//       DUALITY_PROPERTIES_AUTO()
//   };
//
// DUALITY_PROPERTY() is a no-op at real compile time -- the actual reflection data comes from
// a real code-generation pass (GameScripts/CodeGen/generate_fields.py, wired into
// GameScripts/CMakeLists.txt as a pre-build step) that scans every header under
// GameScripts/Include/ for this exact marker, exactly like Unreal Header Tool scans for
// UPROPERTY() before the real compiler runs (a bare per-field macro genuinely can't see its own
// attached declaration's name/type on its own -- this isn't achievable via the preprocessor
// alone). DUALITY_PROPERTIES_AUTO() just declares the `static std::vector<FieldHandle>
// Fields()` method the generated .cpp then defines out-of-class, one definition per class that
// had at least one DUALITY_PROPERTY() field. REGISTER_BEHAVIOUR
// (GameScripts/Include/ScriptRegistration.h) picks up the resulting Fields() automatically if
// present; both macros are entirely optional -- existing scripts with neither line keep
// compiling unchanged with zero Inspector-editable fields.
#define DUALITY_PROPERTY()
#define DUALITY_PROPERTIES_AUTO() static std::vector<::Duality::FieldHandle> Fields();
