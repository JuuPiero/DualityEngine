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
