add_executable(DualityPlayerDesktop Source/Main.cpp)

# GameScripts is a real SHARED DLL on desktop (see GameScripts/CMakeLists.txt) -- unlike
# DualityPlayer's own 3DS-only --whole-archive workaround, a DLL always contains all of its own
# compiled code regardless of what the loading process references directly, so
# BounceBehaviour/ApiShowcaseBehaviour's REGISTER_BEHAVIOUR self-registration runs correctly
# with a plain link here. GameScripts.dll itself still needs to be find-able at runtime (same
# directory or PATH) -- see run-desktop-player.bat, which adds its own build output folder to
# PATH the same way it already does for glfw3.dll/glew32.dll.
target_link_libraries(DualityPlayerDesktop PRIVATE DualityEngine GameScripts)

find_package(glfw3 CONFIG REQUIRED)
find_package(GLEW REQUIRED)
target_link_libraries(DualityPlayerDesktop PRIVATE glfw GLEW::GLEW opengl32)
