#pragma once

#include <vector>

#include "DualityEngine/Reflection/Field.h"

// DUALITY_PROPERTIES(ClassName, field1, field2, ...) generates a
// `static std::vector<FieldHandle> Fields()` method -- the same convention
// ScriptableObject/component types already hand-write (see Reflection.cpp,
// ScriptableObjectRegistration.h) -- by listing already-declared field
// names once instead of a MakeField() call per field:
//
//   class PlayerController : public Duality::Behaviour {
//   public:
//       float Speed = 5.0f;
//       Duality::EntityRef Target;
//
//       DUALITY_PROPERTIES(PlayerController, Speed, Target)
//   };
//
// A bare per-field marker macro (`DUALITY_PROPERTY() float speed;`,
// mirroring Unreal's UPROPERTY()) isn't achievable in plain C++ without a
// code-generation pass scanning the header for that marker -- this project
// has none -- since a macro attached to one field declaration has no way to
// see its own name/type or reach into a class-wide field list. This single
// class-level macro reaches the same "no hand-written MakeField() calls"
// goal instead, at the cost of listing each field's name once here too.
// REGISTER_BEHAVIOUR (GameScripts/Include/ScriptRegistration.h) picks up
// the resulting Fields() automatically if present; it's entirely optional,
// existing scripts with no DUALITY_PROPERTIES line keep compiling unchanged
// with zero Inspector-editable fields.
//
// Supports up to 12 fields per class -- comfortably more than any script
// needs; exceeding it is a compile error (unmatched macro), not a silent
// truncation.

#define DUALITY_PP_EXPAND(x) x

#define DUALITY_PP_ARG_N(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, N, ...) N
#define DUALITY_PP_NARG(...) DUALITY_PP_EXPAND(DUALITY_PP_ARG_N(__VA_ARGS__, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0))

#define DUALITY_PP_CONCAT_(a, b) a##b
#define DUALITY_PP_CONCAT(a, b) DUALITY_PP_CONCAT_(a, b)

#define DUALITY_FIELD_1(Class, x) ::Duality::MakeField(#x, &Class::x)
#define DUALITY_FIELD_2(Class, x, ...) ::Duality::MakeField(#x, &Class::x), DUALITY_PP_EXPAND(DUALITY_FIELD_1(Class, __VA_ARGS__))
#define DUALITY_FIELD_3(Class, x, ...) ::Duality::MakeField(#x, &Class::x), DUALITY_PP_EXPAND(DUALITY_FIELD_2(Class, __VA_ARGS__))
#define DUALITY_FIELD_4(Class, x, ...) ::Duality::MakeField(#x, &Class::x), DUALITY_PP_EXPAND(DUALITY_FIELD_3(Class, __VA_ARGS__))
#define DUALITY_FIELD_5(Class, x, ...) ::Duality::MakeField(#x, &Class::x), DUALITY_PP_EXPAND(DUALITY_FIELD_4(Class, __VA_ARGS__))
#define DUALITY_FIELD_6(Class, x, ...) ::Duality::MakeField(#x, &Class::x), DUALITY_PP_EXPAND(DUALITY_FIELD_5(Class, __VA_ARGS__))
#define DUALITY_FIELD_7(Class, x, ...) ::Duality::MakeField(#x, &Class::x), DUALITY_PP_EXPAND(DUALITY_FIELD_6(Class, __VA_ARGS__))
#define DUALITY_FIELD_8(Class, x, ...) ::Duality::MakeField(#x, &Class::x), DUALITY_PP_EXPAND(DUALITY_FIELD_7(Class, __VA_ARGS__))
#define DUALITY_FIELD_9(Class, x, ...) ::Duality::MakeField(#x, &Class::x), DUALITY_PP_EXPAND(DUALITY_FIELD_8(Class, __VA_ARGS__))
#define DUALITY_FIELD_10(Class, x, ...) ::Duality::MakeField(#x, &Class::x), DUALITY_PP_EXPAND(DUALITY_FIELD_9(Class, __VA_ARGS__))
#define DUALITY_FIELD_11(Class, x, ...) ::Duality::MakeField(#x, &Class::x), DUALITY_PP_EXPAND(DUALITY_FIELD_10(Class, __VA_ARGS__))
#define DUALITY_FIELD_12(Class, x, ...) ::Duality::MakeField(#x, &Class::x), DUALITY_PP_EXPAND(DUALITY_FIELD_11(Class, __VA_ARGS__))

#define DUALITY_FOR_EACH_FIELD_(Class, N, ...) DUALITY_PP_EXPAND(DUALITY_PP_CONCAT(DUALITY_FIELD_, N)(Class, __VA_ARGS__))
#define DUALITY_FOR_EACH_FIELD(Class, ...) DUALITY_FOR_EACH_FIELD_(Class, DUALITY_PP_NARG(__VA_ARGS__), __VA_ARGS__)

#define DUALITY_PROPERTIES(ClassName, ...) \
    static std::vector<::Duality::FieldHandle> Fields() { \
        return { DUALITY_FOR_EACH_FIELD(ClassName, __VA_ARGS__) }; \
    }
