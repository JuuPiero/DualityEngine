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

if(CMAKE_SYSTEM_NAME STREQUAL "Nintendo3DS")
    add_subdirectory(DualityPlayer)
else()
    # DualityPlayer's Main.cpp is 3DS-only for now (gfxInitDefault/hidScanInput/
    # etc.) -- a desktop-standalone variant (statically linking GameScripts,
    # for testing the "same script, no code changes" build path without a
    # devkitARM cross-compile) is a later phase, not needed yet since
    # DualityEditor already exercises the desktop renderer/ECS path directly.

    # The Editor is a desktop-only tool (Dear ImGui + native window) and is
    # never cross-compiled for the device.
    add_subdirectory(DualityEditor)

    # Gameplay scripts, built as a hot-reload DLL for Play-in-Editor. A
    # statically-linked device build of the same sources is a later phase.
    add_subdirectory(GameScripts)
endif()
