# Third-party dependencies. EnTT, GLM, nlohmann/json, ImGui and stb are
# vendored directly as source (copied from the MyGameEngine prototype's own
# Vendor folder, which already has them at the versions/branch this project
# needs) rather than fetched over the network on every clean configure --
# no git clone, no CI-cache-cold slowness, and it works identically offline.
# Box2D is vendored the same way, just fetched once from its own upstream
# repo instead of MyGameEngine's Vendor (that project still pulls it via
# FetchContent) -- see its own comment below. GLFW/GLEW come from the
# mingw-w64 package repo instead (prebuilt, with CMake config files), so
# they don't need vendoring either.

# --- EnTT (ECS) ------------------------------------------------------------
add_library(EnTT INTERFACE)
target_include_directories(EnTT INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/EnTT/include)
add_library(EnTT::EnTT ALIAS EnTT)

# --- GLM (math) --------------------------------------------------------------
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/GLM/include)
add_library(glm::glm ALIAS glm)

# --- nlohmann/json (scene/project serialization) ----------------------------
# Used by DualityEngine itself (Project/SceneSerializer), so it needs to be
# available on both the desktop and 3DS configures.
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/json/include)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)

# --- Box2D (2D physics) ------------------------------------------------------
# Pinned to the classic v2.4.1 C++ API (matching devkitPro's own 3ds-box2d/
# ppc-box2d/switch-box2d portlib version) on BOTH platforms, rather than
# using the mingw-w64 package for desktop -- that one is Box2D v3.x, a
# ground-up rewrite to a plain C API with no b2World/b2Body/b2Fixture
# classes at all, which would force Rigidbody2DComponent/BoxCollider2DComponent
# to have two incompatible implementations instead of one shared one.
# Confirmed elsewhere (devkitPro's own physics/box2d example) that this
# version builds and runs fine on real 3DS hardware.
#
# Vendored directly (its own CMakeLists.txt + include/ + src/ only -- no
# unit-test/testbed/docs/extern, which its own CMakeLists.txt only
# add_subdirectory()s when the BOX2D_BUILD_* options below are ON) instead of
# FetchContent, same "copy it once instead of fetching over the network on
# every clean configure" reasoning as EnTT/GLM/nlohmann-json/ImGui above --
# FetchContent's git clone re-ran, and box2d fully rebuilt from a fresh
# _deps/box2d-src, on every wiped build directory (build-3ds.bat always
# wipes its build dir, see that script's own comment).
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_DOCS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/box2d)

# --- Bullet Physics (3D physics) ---------------------------------------------
# Pinned to tag 2.87 exactly, matching devkitPro's own 3ds-bulletphysics
# portlib version (confirmed via its real PKGBUILD -- same repo tier as
# 3ds-box2d, built by devkitPro's own CI, though unlike box2d it ships no
# worked 3DS example and no known shipped 3DS game uses it, so its runtime
# behavior on real ARM11 hardware is unverified until this project's own
# 3D physics lands and gets tested on-device).
#
# Vendored the same way as Box2D above: only the pieces devkitPro's own
# build actually compiles with BUILD_BULLET3=OFF -- LinearMath, Bullet3Common,
# BulletCollision, BulletDynamics, BulletSoftBody, BulletInverseDynamics --
# not the CPU/OpenGL3/Bullet2 demos, unit tests, or pybullet bindings, which
# its own CMakeLists.txt only add_subdirectory()s when the BUILD_* options
# below are ON (and which aren't vendored here at all, so even an option
# left ON can't reach a directory that doesn't exist).
#
# The flags below are devkitPro's own `-D...` PKGBUILD flags translated to
# CACHE FORCE (same translation box2d's own BOX2D_BUILD_* lines above use).
# Bullet's vendored CMakeLists.txt additionally has two IF(NOT NINTENDO_3DS)
# guards applied directly to it (not a separate .patch file) around its
# FIND_PACKAGE(OpenGL) and Python/pybullet-detection blocks -- both re-derived
# from a direct read of the real 2.87 source rather than devkitPro's own
# bulletphysics.patch (which an earlier pass here deliberately did not trust
# blindly, since a WebFetch summary of it gave slightly different line
# numbers than a direct read of the real file). NINTENDO_3DS itself needs no
# extra wiring here -- devkitPro's own Platform/Nintendo3DS.cmake sets it
# automatically for any configure that goes through this project's existing
# 3DS toolchain file, the same mechanism CMAKE_SYSTEM_NAME checks elsewhere
# in this file already rely on.
set(BUILD_CPU_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_BULLET3 OFF CACHE BOOL "" FORCE)
set(BUILD_OPENGL3_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_BULLET2_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_PYBULLET OFF CACHE BOOL "" FORCE)
set(BUILD_ENET OFF CACHE BOOL "" FORCE)
set(BUILD_CLSOCKET OFF CACHE BOOL "" FORCE)
set(INSTALL_LIBS OFF CACHE BOOL "" FORCE)
set(INSTALL_CMAKE_FILES OFF CACHE BOOL "" FORCE)
set(USE_GRAPHICAL_BENCHMARK OFF CACHE BOOL "" FORCE)
set(USE_DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)

# devkitPro's own PKGBUILD additionally compiles Bullet with
# -fno-exceptions -fno-rtti on ARM (a size/perf tuning choice, not a
# correctness requirement -- Bullet's public API doesn't throw or need
# RTTI from the outside). Scoped to just this add_subdirectory call and
# restored right after, so it doesn't change DualityEngine's own compile
# flags or leak into ImGui/stb/miniaudio below.
set(_bullet_saved_cxx_flags "${CMAKE_CXX_FLAGS}")
if(NINTENDO_3DS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/bullet3)
set(CMAKE_CXX_FLAGS "${_bullet_saved_cxx_flags}")
unset(_bullet_saved_cxx_flags)

# Bullet's own CMakeLists.txt uses old-style directory-scoped
# INCLUDE_DIRECTORIES() rather than target_include_directories(), so unlike
# box2d above (which does use target_include_directories(box2d PUBLIC ...)),
# linking against BulletDynamics alone does not expose its headers to
# consumers outside Vendor/bullet3/ -- added explicitly here instead of
# patching every one of Bullet's own per-module CMakeLists.txt files.
target_include_directories(BulletDynamics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/bullet3/src)

# --- Desktop-only editor dependencies -------------------------------------
# Never pulled in for the 3DS configure (the Editor itself is desktop-only).
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Nintendo3DS")
    # GLFW + GLEW come from the mingw-w64 package repo (prebuilt, with CMake
    # config files) rather than being built from source -- this sidesteps
    # GLAD's offline code-generation step entirely.
    find_package(glfw3 CONFIG REQUIRED)
    find_package(GLEW REQUIRED)

    # Dear ImGui has no usable upstream CMake package (consumers are expected
    # to compile the sources + the backend files they need directly into
    # their own target). Vendored copy is a "docking" branch build (version
    # "1.91.3 WIP" -- docking/multi-viewport support has never been merged
    # into imgui's master/release tags), needed for ImGui::DockSpace's
    # Unity/Cocos-Creator-style dockable panel layout.
    add_library(ImGui STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/ImGui/src/imgui.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ImGui/src/imgui_demo.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ImGui/src/imgui_draw.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ImGui/src/imgui_tables.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ImGui/src/imgui_widgets.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ImGui/src/imgui_impl_glfw.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ImGui/src/imgui_impl_opengl3.cpp
    )
    target_include_directories(ImGui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ImGui/include)
    target_link_libraries(ImGui PUBLIC glfw)

    # stb_image, for loading real thumbnail previews of image assets in the
    # Content Browser (Editor-only for now -- the Engine doesn't have a
    # texture/asset-loading subsystem yet, that's a later phase).
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb/include)

    # miniaudio (single-header, public domain/MIT-0), desktop audio backend.
    # Not in MyGameEngine's own Vendor/ (that project has no audio system),
    # so fetched fresh from its official GitHub repo -- same "fetch once,
    # keep it locally" precedent as Tools/makerom.exe and bannertool.exe.
    add_library(miniaudio INTERFACE)
    target_include_directories(miniaudio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio/include)

    # ImGuizmo is deferred until the phase that actually adds viewport gizmos
    # -- MyGameEngine's own vendored ImGuizmo is paired with its exact ImGui
    # commit; re-check compatibility against this project's ImGui copy when
    # gizmos are wired up.
endif()
