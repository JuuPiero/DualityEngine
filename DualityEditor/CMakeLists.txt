add_executable(DualityEditor
    Source/Main.cpp
    Source/Framebuffer.cpp
    Source/ThumbnailCache.cpp
    Source/ScriptEngine.cpp
    Source/BuildPipeline.cpp
    Source/SceneGizmo.cpp
    Source/Panels/ContentBrowserPanel.cpp
    Source/Panels/ConsolePanel.cpp
)

# find_package's imported targets are only guaranteed visible within the
# directory scope that called it (and its children) -- call it again here
# rather than relying on Vendor's/DualityEngine's copies being visible to
# this sibling directory.
find_package(glfw3 CONFIG REQUIRED)
find_package(GLEW REQUIRED)

target_include_directories(DualityEditor PRIVATE Include)
target_link_libraries(DualityEditor PRIVATE DualityEngine glfw GLEW::GLEW opengl32 ImGui stb)
