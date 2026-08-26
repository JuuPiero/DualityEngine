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
# Real per-field DUALITY_PROPERTY() attributes (see Reflection/PropertyMacros.h) need a real
# code-generation pass -- a bare per-field marker macro can't see its own name/type via the
# preprocessor alone, the same reason Unreal Header Tool exists. generate_fields.py scans every
# header under Include/ for that marker and emits Generated/ScriptFields.generated.cpp, which
# both build modes below compile as a normal source file. Searches PATH first (works for the
# desktop build.bat's plain cmd.exe environment, where a standard python.org install already
# registers itself), falling back to this machine's known-good install location -- devkitPro's
# own bundled MSYS2 (which build-3ds.bat's Makefile-generator configure uses) has no Python of
# its own on its PATH, the same kind of environment-specific gotcha this project's other
# devkitPro-path notes already document (see build-3ds.bat's own comments).
#
# Deliberately NOT `find_program(... PATHS "E:/App/Python/Python312")` -- find_program caches
# its result as a FILEPATH-typed variable, which hits the exact same MSYS-hosted-cmake path-
# normalization corruption documented in detail below for DUALITY_PROJECT_SCRIPTS_DIR (confirmed
# empirically: `DUALITY_PYTHON_EXECUTABLE` came out as
# "/f/Workspace/.../DualityEngine/E:/App/Python/Python312/python.exe", the same
# repo-source-dir-prepended-onto-a-drive-letter-path corruption). Searching PATH only, then
# falling back to a plain EXISTS check + non-CACHE `set()` (never stored as FILEPATH/PATH),
# sidesteps that normalization entirely for this variable too.
find_program(DUALITY_PYTHON_EXECUTABLE NAMES python python3)
if(NOT DUALITY_PYTHON_EXECUTABLE)
    set(DUALITY_PYTHON_FALLBACK "E:/App/Python/Python312/python.exe")
    if(EXISTS ${DUALITY_PYTHON_FALLBACK})
        set(DUALITY_PYTHON_EXECUTABLE ${DUALITY_PYTHON_FALLBACK})
    endif()
endif()
if(NOT DUALITY_PYTHON_EXECUTABLE)
    message(FATAL_ERROR "GameScripts: python not found (needed to generate ScriptFields.generated.cpp from DUALITY_PROPERTY() markers) -- checked PATH and the E:/App/Python/Python312 fallback")
endif()

# The active Project's own Assets/Scripts directory, if any -- set (via -D) by
# ScriptEngine::Reload/BuildPipeline::BuildFor3DS whenever the Editor rebuilds, so a project's
# own scripts compile into this SAME GameScripts module alongside the engine's shared/demo
# scripts (one ScriptRegistry, one flat class-name namespace -- see this project's own comment
# above on why GameScripts is never linked/duplicated per-consumer). Empty by default (e.g. a
# raw `cmake --build` with no Editor involved) -- both GLOBs below are guarded no-ops then.
# Deliberately CACHE STRING, not CACHE PATH -- a PATH-typed cache variable gets normalized/
# validated by CMake as a path relative to the *current* CMake process, and the MSYS-hosted
# cmake.exe the 3DS configure uses (see build-3ds.bat's own comments on why it must be that one,
# not a native-Windows cmake) uses Cygwin/MSYS path semantics internally -- it doesn't recognize
# a Windows-style "F:/..." path as already-absolute, so a PATH-typed value here silently got the
# repo source dir prepended onto it (confirmed empirically: "/f/.../DualityEngine/F:/..."),
# exactly the same class of gotcha this project's other devkitPro-path notes already document.
# STRING sidesteps CMake's path normalization entirely -- both consumers below (file(GLOB...)
# and generate_fields.py) accept a plain Windows-style path directly, no normalization needed.
set(DUALITY_PROJECT_SCRIPTS_DIR "" CACHE STRING "Active project's Assets/Scripts directory, compiled into GameScripts alongside the engine's own scripts")

# file(GLOB)/EXISTS under the MSYS-hosted cmake.exe the 3DS configure uses (see build-3ds.bat's
# own comments on why it must be that one) use Cygwin/MSYS path semantics internally -- a
# Windows-style drive-letter path ("F:/Workspace/...") isn't recognized as already-absolute
# there, so file(GLOB_RECURSE "F:/Workspace/.../*.cpp") silently returns NOTHING (confirmed
# empirically: a `cmake -P` one-liner glob against a real existing directory found zero files
# with the drive-letter form, but found it correctly once converted to MSYS mount notation,
# "/f/Workspace/..."). Deliberately only derived/used for the GLOB/EXISTS calls below, gated to
# the 3DS build specifically -- the desktop build's own (mingw64-native) cmake.exe handles
# drive-letter paths correctly already, and generate_fields.py's own invocation further below
# needs the ORIGINAL Windows-style path unchanged (it runs through a native Windows python.exe,
# which doesn't understand MSYS mount notation any more than mingw64's cmake needs it).
set(DUALITY_PROJECT_SCRIPTS_GLOB_DIR "${DUALITY_PROJECT_SCRIPTS_DIR}")
if(CMAKE_SYSTEM_NAME STREQUAL "Nintendo3DS" AND DUALITY_PROJECT_SCRIPTS_GLOB_DIR MATCHES "^([A-Za-z]):(.*)$")
    set(DUALITY_PROJECT_SCRIPTS_GLOB_DIR "/${CMAKE_MATCH_1}${CMAKE_MATCH_2}")
endif()

set(DUALITY_PROJECT_SCRIPT_SOURCES "")
if(DUALITY_PROJECT_SCRIPTS_GLOB_DIR AND EXISTS ${DUALITY_PROJECT_SCRIPTS_GLOB_DIR})
    file(GLOB_RECURSE DUALITY_PROJECT_SCRIPT_SOURCES ${DUALITY_PROJECT_SCRIPTS_GLOB_DIR}/*.cpp)
endif()

# Run at CONFIGURE time (execute_process), NOT as a build-time add_custom_command(OUTPUT ...) --
# deliberately, after hitting a real bug the other way: Make/Ninja's OUTPUT-vs-DEPENDS staleness
# check only looks at file mtimes, not at whether DUALITY_PROJECT_SCRIPTS_DIR's VALUE changed --
# e.g. reconfigure with an empty project dir (regenerating the file WITHOUT a project script),
# then reconfigure again with that project dir set: the project's header file itself hasn't
# changed mtime, so the build system considered the (now-wrong, stale) output already "up to
# date" and never re-ran the generator -- confirmed empirically via a real link error
# ("undefined reference to ProjectDemoBehaviour::Fields()"). execute_process side-steps this
# entirely by running unconditionally on every CMake configure instead of being a build-graph
# node -- correct because every real entry point in this project (ScriptEngine::Reload,
# BuildPipeline::BuildFor3DS, build.bat, build-3ds.bat) already does a fresh reconfigure before
# every build anyway, so "regenerate on every configure" is exactly the right cadence, not
# excessive -- the script itself is a handful of small file reads, cheap regardless.
set(DUALITY_GENERATED_FIELDS_CPP ${CMAKE_CURRENT_SOURCE_DIR}/Generated/ScriptFields.generated.cpp)
execute_process(
    COMMAND ${DUALITY_PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/CodeGen/generate_fields.py
            ${DUALITY_GENERATED_FIELDS_CPP} ${CMAKE_CURRENT_SOURCE_DIR}/Include ${DUALITY_PROJECT_SCRIPTS_DIR}
    RESULT_VARIABLE DUALITY_GENERATE_FIELDS_RESULT
    OUTPUT_VARIABLE DUALITY_GENERATE_FIELDS_OUTPUT
    ERROR_VARIABLE DUALITY_GENERATE_FIELDS_ERROR
)
if(NOT DUALITY_GENERATE_FIELDS_RESULT EQUAL 0)
    message(FATAL_ERROR "GameScripts: generate_fields.py failed:\n${DUALITY_GENERATE_FIELDS_OUTPUT}\n${DUALITY_GENERATE_FIELDS_ERROR}")
endif()
message(STATUS "${DUALITY_GENERATE_FIELDS_OUTPUT}")

if(CMAKE_SYSTEM_NAME STREQUAL "Nintendo3DS")
    add_library(GameScripts STATIC
        Source/BounceBehaviour.cpp
        Source/ApiShowcaseBehaviour.cpp
        Source/CollisionLogBehaviour.cpp
        Source/RaycastDemoBehaviour.cpp
        Source/UIDocumentDemoBehaviour.cpp
        Source/UIButtonClickBehaviour.cpp
        Source/GameSettingsData.cpp
        Source/PlayerController.cpp
        Source/BallTest.cpp
        Source/ScriptModuleExports.cpp
        ${DUALITY_GENERATED_FIELDS_CPP}
        ${DUALITY_PROJECT_SCRIPT_SOURCES}
    )
else()
    add_library(GameScripts SHARED
        Source/BounceBehaviour.cpp
        Source/ApiShowcaseBehaviour.cpp
        Source/CollisionLogBehaviour.cpp
        Source/RaycastDemoBehaviour.cpp
        Source/UIDocumentDemoBehaviour.cpp
        Source/UIButtonClickBehaviour.cpp
        Source/GameSettingsData.cpp
        Source/PlayerController.cpp
        Source/BallTest.cpp
        Source/ScriptModuleExports.cpp
        ${DUALITY_GENERATED_FIELDS_CPP}
        ${DUALITY_PROJECT_SCRIPT_SOURCES}
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
