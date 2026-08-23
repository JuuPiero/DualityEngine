add_library(DualityEngine STATIC
    Source/Core/Log.cpp
    Source/Asset/AssetMeta.cpp
    Source/Asset/AssetDatabase.cpp
    Source/Input/Input.cpp
    Source/Reflection/TypeRegistry.cpp
    Source/Reflection/Reflection.cpp
    Source/Scripting/ScriptRegistry.cpp
    Source/Scene/Scene.cpp
    Source/Scene/SceneSerializer.cpp
    Source/Renderer/SceneRenderer.cpp
    Source/Project/Project.cpp
)

target_include_directories(DualityEngine PUBLIC Include)
target_link_libraries(DualityEngine PUBLIC EnTT::EnTT glm::glm nlohmann_json::nlohmann_json box2d)

if(CMAKE_SYSTEM_NAME STREQUAL "Nintendo3DS")
    target_sources(DualityEngine PRIVATE
        Source/Renderer/N3DS/Citro2DRenderer.cpp
        Source/Audio/AudioEngine_3DS.cpp
        Source/Audio/WavLoader.cpp
    )
    target_compile_definitions(DualityEngine PUBLIC DE_PLATFORM_3DS)
    # citro2d/citro3d are not part of CTR_STANDARD_LIBRARIES (only -lctru -lm
    # are linked automatically by devkitPro's platform file), so they need to
    # be added explicitly. Their headers/libs already live under libctru's
    # own include/lib dirs, which ARE on the default search path.
    target_link_libraries(DualityEngine PUBLIC citro2d citro3d)
else()
    target_sources(DualityEngine PRIVATE
        Source/Renderer/OpenGL/OpenGLRenderer2D.cpp
        Source/Renderer/OpenGL/GLTextureLoader.cpp
        Source/Audio/AudioEngine_Desktop.cpp
    )
    target_compile_definitions(DualityEngine PUBLIC DE_PLATFORM_DESKTOP)
    find_package(GLEW REQUIRED)
    # miniaudio resolves WinMM/WASAPI/COM entirely via runtime
    # LoadLibrary/GetProcAddress -- no extra Windows import libs to link.
    target_link_libraries(DualityEngine PUBLIC GLEW::GLEW opengl32 stb miniaudio)
endif()
