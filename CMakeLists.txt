cmake_minimum_required(VERSION 3.14)

project(DualityEngine CXX)

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
endif()
