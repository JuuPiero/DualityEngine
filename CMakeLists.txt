cmake_minimum_required(VERSION 3.14)


# C is needed alongside CXX for the 3DS build's embedded-binary shader object
# library (dkp_add_embedded_binary_library falls back to compiling its
# generated .S file as C when the ASM language isn't enabled -- see
# DualityEngine/CMakeLists.txt's shader-embedding block) -- must be enabled
# here, at the top of the whole build, not lazily via a subdirectory (e.g.
# box2d's own project() call already enables it, but by then it's too late
# for the "Unix Makefiles" generator to wire up C's per-directory compile
# rules for targets declared afterwards, which manifested as a real "CMake
# Error: Error required internal CMake variable not set... CMAKE_C_COMPILE_OBJECT"
# Generate-step failure).
project(DualityEngine CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Nintendo3DS")
    message(STATUS "DualityEngine: configuring for Nintendo 3DS (devkitARM)")
else()
    message(STATUS "DualityEngine: configuring for desktop")
endif()

add_subdirectory(Vendor)
add_subdirectory(DualityEngine)

# GameScripts builds SHARED (a hot-reload DLL) for desktop or STATIC for 3DS
# (see GameScripts/CMakeLists.txt) -- same source files either way.
add_subdirectory(GameScripts)

if(CMAKE_SYSTEM_NAME STREQUAL "Nintendo3DS")
    add_subdirectory(DualityPlayer)
else()
    # The Editor is a desktop-only tool (Dear ImGui + native window) and is
    # never cross-compiled for the device.
    add_subdirectory(DualityEditor)
    # Standalone desktop player -- the desktop equivalent of DualityPlayer (real GLFW window +
    # OpenGLRenderer2D/3D instead of Citro2D/3DRenderer, no ImGui/Editor UI at all), so a scene
    # built in the Editor can also run as a real .exe on desktop, not just on-device.
    add_subdirectory(DualityPlayerDesktop)
endif()
