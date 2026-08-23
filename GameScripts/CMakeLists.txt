# Gameplay scripts. Two link modes, same source files unchanged either way
# (the only thing that differs is ScriptModuleExports.cpp's DE_SCRIPT_EXPORT
# macro, which already resolves per-platform via #if defined(_WIN32)):
#
# - Desktop (Editor Play-in-Editor): SHARED (a hot-reloadable DLL). Does NOT
#   link DualityEngine (or any other engine library) -- only its headers,
#   via the EnTT::EnTT / glm::glm INTERFACE targets (include paths only, no
#   compiled code to duplicate). Behaviour/Entity/Components are all
#   header-only templates, so no linkage is needed to use them here. If
#   GameScripts linked DualityEngine's compiled .lib, this DLL would carry
#   its own separate copy of every static (ScriptRegistry's map,
#   TypeRegistry's vector, ...), silently desyncing from the host process's
#   copy -- exactly the failure mode this split avoids.
# - 3DS (no dynamic loading exists on the platform at all): STATIC, linked
#   directly into DualityPlayer. There's only one process/one copy of every
#   static either way here, so the "don't link DualityEngine" concern above
#   doesn't apply -- but the sources don't need DualityEngine's compiled
#   code regardless (same header-only reasoning), so it's left unlinked for
#   both modes rather than special-cased.
if(CMAKE_SYSTEM_NAME STREQUAL "Nintendo3DS")
    add_library(GameScripts STATIC
        Source/BounceBehaviour.cpp
        Source/ApiShowcaseBehaviour.cpp
        Source/ScriptModuleExports.cpp
    )
else()
    add_library(GameScripts SHARED
        Source/BounceBehaviour.cpp
        Source/ApiShowcaseBehaviour.cpp
        Source/ScriptModuleExports.cpp
    )
    set_target_properties(GameScripts PROPERTIES PREFIX "")
endif()

target_include_directories(GameScripts PRIVATE
    Include
    ${CMAKE_SOURCE_DIR}/DualityEngine/Include
)
# nlohmann_json is header-only (no compiled symbols/global state) --
# ApiShowcaseBehaviour.cpp uses DualityEngine/IO/SaveSystem.h, which needs
# it. Same "header-only is safe across the DLL boundary" reasoning as
# Behaviour/Entity/Components already being usable here with no engine lib
# linked -- this doesn't reintroduce the static-duplication problem
# EnTT::EnTT/glm::glm's INTERFACE-only linking already avoids.
target_link_libraries(GameScripts PRIVATE EnTT::EnTT glm::glm nlohmann_json::nlohmann_json)
