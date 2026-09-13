add_library(DualityEngine STATIC
    Source/Core/Log.cpp
    Source/Asset/AssetMeta.cpp
    Source/Asset/AssetDatabase.cpp
    Source/Asset/MaterialLoader.cpp
    Source/Asset/PhysicsMaterialLoader.cpp
    Source/Asset/MeshLoader.cpp
    Source/Asset/ScriptableObjectLoader.cpp
    Source/Asset/TextureImportSettings.cpp
    Source/Asset/AudioImportSettings.cpp
    Source/Input/InputManager.cpp
    Source/Physics/PhysicsUnits.cpp
    Source/Reflection/TypeRegistry.cpp
    Source/Reflection/Reflection.cpp
    Source/Reflection/FieldSerialization.cpp
    Source/Scripting/ScriptRegistry.cpp
    Source/Scripting/ScriptableObjectRegistry.cpp
    Source/Scene/Scene.cpp
    Source/Scene/SceneValidator.cpp
    Source/Scene/SceneRuntimeSystems.cpp
    Source/Scene/SceneSerializer.cpp
    Source/Scene/EntitySerialization.cpp
    Source/Scene/PrefabSerializer.cpp
    Source/Renderer/SceneRenderer.cpp
    Source/Renderer/DrawHelpers2D.cpp
    Source/Renderer/UIRenderer.cpp
    Source/Renderer/UIAnchor.cpp
    Source/Renderer/PrimitiveMeshes.cpp
    Source/Project/Project.cpp
)

target_include_directories(DualityEngine PUBLIC Include)
# Bullet's own per-module CMakeLists.txt links BulletDynamics -> BulletCollision
# -> LinearMath via old-style (non-keyword) target_link_libraries calls, which
# does NOT reliably resolve BulletDynamics's own internal references to
# BulletCollision/LinearMath symbols for a static-archive link (GNU ld only
# scans each archive once, left to right -- confirmed by hitting exactly this
# as "undefined reference to btCollisionWorld::..." until these were listed
# explicitly here, most-dependent first, same order Bullet's own exported
# BULLET_LIBRARIES list implies).
target_link_libraries(DualityEngine PUBLIC EnTT::EnTT glm::glm nlohmann_json::nlohmann_json box2d BulletDynamics BulletCollision LinearMath)

if(CMAKE_SYSTEM_NAME STREQUAL "Nintendo3DS")
    target_sources(DualityEngine PRIVATE
        Source/Renderer/N3DS/Citro2DRenderer.cpp
        Source/Renderer/N3DS/Citro3DRenderer.cpp
        Source/Audio/AudioEngine_3DS.cpp
        Source/Audio/WavLoader.cpp
    )
    target_compile_definitions(DualityEngine PUBLIC DE_PLATFORM_3DS)
    # citro2d/citro3d are not part of CTR_STANDARD_LIBRARIES (only -lctru -lm
    # are linked automatically by devkitPro's platform file), so they need to
    # be added explicitly. Their headers/libs already live under libctru's
    # own include/lib dirs, which ARE on the default search path.
    target_link_libraries(DualityEngine PUBLIC citro2d citro3d)

    # Compiles Citro3DRenderer's unlit mesh vertex shader (Assets3DS/mesh.v.pica) to a .shbin
    # via picasso, then embeds it as a linkable object exposing mesh_shbin/mesh_shbin_size
    # symbols (via bin2s) -- these are devkitPro's own CMake helpers (Platform/Nintendo3DS.cmake,
    # dkp-embedded-binary.cmake), the same pattern the tex3ds/.t3x asset pipeline mirrors, just
    # for fixed engine-owned shader source instead of a dynamic per-project asset folder (see
    # BuildPipeline::CookAssets for that one).
    # NOTE: sources must be passed positionally, NOT via a SOURCES keyword --
    # devkitPro's own ctr_add_shader_library (Platform/Nintendo3DS.cmake)
    # only treats CTR_PICASSO_UNPARSED_ARGUMENTS as sources; passing SOURCES
    # explicitly makes cmake_parse_arguments populate CTR_PICASSO_SOURCES
    # directly, which the function's own logic then rejects as "no source
    # code file" (confirmed by hitting that exact error message).
    ctr_add_shader_library(mesh_shader
        ${CMAKE_CURRENT_SOURCE_DIR}/Assets3DS/mesh.v.pica
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mesh.shbin
    )
    dkp_add_embedded_binary_library(mesh_shader_embed mesh_shader)
    target_link_libraries(DualityEngine PUBLIC mesh_shader_embed)
else()
    target_sources(DualityEngine PRIVATE
        Source/Renderer/OpenGL/OpenGLRenderer2D.cpp
        Source/Renderer/OpenGL/OpenGLRenderer3D.cpp
        Source/Renderer/OpenGL/GLTextureLoader.cpp
        Source/Renderer/OpenGL/GLFontLoader.cpp
        Source/Renderer/OpenGL/GLShaderProgram.cpp
        Source/Renderer/OpenGL/GLVertexArray.cpp
        Source/Audio/AudioEngine_Desktop.cpp
    )
    target_compile_definitions(DualityEngine PUBLIC DE_PLATFORM_DESKTOP)
    find_package(GLEW REQUIRED)
    # miniaudio resolves WinMM/WASAPI/COM entirely via runtime
    # LoadLibrary/GetProcAddress -- no extra Windows import libs to link.
    target_link_libraries(DualityEngine PUBLIC GLEW::GLEW opengl32 stb miniaudio)
endif()
