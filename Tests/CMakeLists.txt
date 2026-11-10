# Desktop-only automated test suite for engine-level logic (ECS/Active-Disable, 2D/3D
# physics, collision/trigger events) -- never cross-compiled for the 3DS, same reasoning
# as DualityEditor (no GUI/device dependency here either, just DualityEngine's own compiled
# code exercised headlessly). Linking PRIVATE DualityEngine is enough to also pull in
# box2d/BulletDynamics/BulletCollision/LinearMath transitively (DualityEngine links them
# PUBLIC), so Tests/CollisionTriggerTests.cpp's own #include <box2d/box2d.h> just works
# with no extra include paths listed here.
add_executable(DualityEngineTests
    Main.cpp
    ActiveEnableTests.cpp
    CollisionTriggerTests.cpp
    Physics3DTests.cpp
    SceneManagerTests.cpp
    PrefabTests.cpp
    SceneSerializerTests.cpp
    ScriptableObjectTests.cpp
    AssetImportSettingsTests.cpp
    MaterialTests.cpp
    RenderingMaterialTests.cpp
    MeshLoaderTests.cpp
    ModelRuntimeAssetTests.cpp
    RigidbodyAPITests.cpp
    DualityPropertyTests.cpp
    SceneManagementTests.cpp
    RaycastTests.cpp
    UIRectComponentTests.cpp
    MultiScriptTests.cpp
    ScriptContextDllBoundaryTests.cpp
)
target_link_libraries(DualityEngineTests PRIVATE DualityEngine)

# Joints, effectors, Character Controller (PhysicsJointsTests) and the non-physics component
# systems: UI widgets, constraints, LOD, trails (ComponentSystemsTests).
target_sources(DualityEngineTests PRIVATE
    PhysicsJointsTests.cpp
    ComponentSystemsTests.cpp
)

# Save/load stability of every SampleProject scene and prefab (format regression guard).
target_sources(DualityEngineTests PRIVATE SampleSceneSerializationTests.cpp MeshFilterTests.cpp RegistrationTests.cpp)
target_compile_definitions(DualityEngineTests PRIVATE DUALITY_REPO_ROOT="${CMAKE_SOURCE_DIR}")

# Sprite Editor / Animation / Tile Palette / Profiler engine-side logic (sprite import data and
# slicing, sub-sprite drawing, tilemap brushes, animation curves, profiler, tracked undo files).
target_sources(DualityEngineTests PRIVATE SpriteToolsTests.cpp)

# Editor logic without UI (Undo history, stable entity ids, Inspector multi-edit): those editor
# sources only depend on DualityEngine, so they are compiled straight into the test binary.
target_sources(DualityEngineTests PRIVATE
    EditorLogicTests.cpp
    ${CMAKE_SOURCE_DIR}/DualityEditor/Source/SceneHistory.cpp
    ${CMAKE_SOURCE_DIR}/DualityEditor/Source/EntityIdRegistry.cpp
    ${CMAKE_SOURCE_DIR}/DualityEditor/Source/MultiEdit.cpp
    SceneViewNavigationTests.cpp
    ${CMAKE_SOURCE_DIR}/DualityEditor/Source/SceneView/SceneViewNavigation.cpp
)
target_include_directories(DualityEngineTests PRIVATE ${CMAKE_SOURCE_DIR}/DualityEditor/Include)

# TypeScript scripting (QuickJS) bridge tests. The fixture project's .ts files are bundled by the
# same toolchain the Editor uses (Tools/TypeScript/build-scripts.mjs, needs node); without node the
# JsScriptingTests cases report "skipped" instead of failing.
target_sources(DualityEngineTests PRIVATE JsScriptingTests.cpp)
find_program(DUALITY_NODE_EXECUTABLE node)
if(DUALITY_NODE_EXECUTABLE)
    set(DUALITY_TS_FIXTURE_OUT ${CMAKE_CURRENT_BINARY_DIR}/TypeScriptFixture)
    file(GLOB DUALITY_TS_FIXTURE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/TypeScriptFixture/Assets/Scripts/*.ts)
    add_custom_command(
        OUTPUT ${DUALITY_TS_FIXTURE_OUT}/scripts.js
        COMMAND ${DUALITY_NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/Tools/TypeScript/build-scripts.mjs
                ${CMAKE_CURRENT_SOURCE_DIR}/TypeScriptFixture --out ${DUALITY_TS_FIXTURE_OUT} --no-project-types
        DEPENDS ${DUALITY_TS_FIXTURE_SOURCES} ${CMAKE_SOURCE_DIR}/Tools/TypeScript/duality/duality.ts
                ${CMAKE_SOURCE_DIR}/Tools/TypeScript/build-scripts.mjs
        COMMENT "Bundling the TypeScript test fixture"
        VERBATIM)
    add_custom_target(DualityTsFixture DEPENDS ${DUALITY_TS_FIXTURE_OUT}/scripts.js)
    add_dependencies(DualityEngineTests DualityTsFixture)
    target_compile_definitions(DualityEngineTests PRIVATE DUALITY_TS_FIXTURE="${DUALITY_TS_FIXTURE_OUT}/scripts.js")
endif()

# Model import pipeline (cooker rules + a real Assimp round trip on generated glTF fixtures). The
# importer is editor code, compiled into the test binary like SceneHistory above.
target_sources(DualityEngineTests PRIVATE
    ModelImportTests.cpp
    ${CMAKE_SOURCE_DIR}/DualityEditor/Source/ModelImporter.cpp
    ${CMAKE_SOURCE_DIR}/DualityEditor/Source/Import/ModelImportSettings.cpp
    ${CMAKE_SOURCE_DIR}/DualityEditor/Source/Import/ModelCooker.cpp
    ${CMAKE_SOURCE_DIR}/DualityEditor/Source/Import/TextureBaker.cpp
    ${CMAKE_SOURCE_DIR}/DualityEditor/Source/Import/ModelPrefabBuilder.cpp
)
target_link_libraries(DualityEngineTests PRIVATE assimp::assimp stb)
