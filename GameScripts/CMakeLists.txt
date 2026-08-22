# Gameplay scripts, built as a hot-reloadable DLL for desktop Play-in-Editor.
#
# Deliberately does NOT link DualityEngine (or any other engine library) --
# only its headers, via the EnTT::EnTT / glm::glm INTERFACE targets (which
# add include paths only, no compiled code to duplicate). Behaviour/Entity/
# Components are all header-only templates, so no linkage is needed to use
# them here. If GameScripts linked DualityEngine's compiled .lib, this DLL
# would carry its own separate copy of every static (ScriptRegistry's map,
# TypeRegistry's vector, ...), silently desyncing from the host process's
# copy -- exactly the failure mode this split avoids.
add_library(GameScripts SHARED
    Source/BounceBehaviour.cpp
    Source/ScriptModuleExports.cpp
)

set_target_properties(GameScripts PROPERTIES PREFIX "")

target_include_directories(GameScripts PRIVATE
    Include
    ${CMAKE_SOURCE_DIR}/DualityEngine/Include
)
target_link_libraries(GameScripts PRIVATE EnTT::EnTT glm::glm)
