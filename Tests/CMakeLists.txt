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
    MeshLoaderTests.cpp
    RigidbodyAPITests.cpp
    DualityPropertyTests.cpp
    SceneManagementTests.cpp
    RaycastTests.cpp
)
target_link_libraries(DualityEngineTests PRIVATE DualityEngine)
