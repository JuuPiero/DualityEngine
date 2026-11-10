add_executable(DualityEditor
    Source/Main.cpp
    Source/Application.cpp
    Source/Window.cpp
    Source/FileDialogs.cpp
    Source/Framebuffer.cpp
    Source/ThumbnailCache.cpp
    Source/ScriptEngine.cpp
    Source/TypeScriptCompiler.cpp
    Source/BuildPipeline.cpp
    Source/SceneGizmo.cpp
    Source/SceneHistory.cpp
    Source/EntityIdRegistry.cpp
    Source/EditorCommands.cpp
    Source/EditorAutomation.cpp
    Source/MultiEdit.cpp
    Source/FieldEditorWidget.cpp
    Source/AssetInspectorRegistry.cpp
    Source/AssetInspectors.cpp
    Source/MaterialInspector.cpp
    Source/BuiltinMeshPreviews.cpp
    Source/SceneView/SceneViewNavigation.cpp
    Source/SceneView/SceneViewState.cpp
    Source/SceneView/SceneAssetDrop.cpp
    Source/SceneView/SceneGizmoDrawing.cpp
    Source/SceneView/SceneViewOverlays.cpp
    Source/SceneOps.cpp
    Source/EditorSettings.cpp
    Source/EditorTheme.cpp
    Source/ModelImporter.cpp
    Source/Panels/MenuBarPanel.cpp
    Source/Panels/HierarchyPanel.cpp
    Source/Panels/PropertiesPanel.cpp
    Source/Panels/ScenePanel.cpp
    Source/Panels/GamePanel.cpp
    Source/Panels/ContentBrowserPanel.cpp
    Source/Panels/ConsolePanel.cpp
    Source/Panels/BuildSettingsPanel.cpp
    Source/Panels/ProjectSettingsPanel.cpp
    Source/Panels/PackageManagerPanel.cpp
    Source/Panels/PreferencesPanel.cpp
    Source/Panels/LightingPanel.cpp
    Source/Windows/SpriteEditorWindow.cpp
    Source/Windows/SpriteAssetUI.cpp
    Source/Windows/AnimationWindow.cpp
    Source/Windows/TilePaletteWindow.cpp
    Source/Windows/ProfilerWindow.cpp
)

# find_package's imported targets are only guaranteed visible within the
# directory scope that called it (and its children) -- call it again here
# rather than relying on Vendor's/DualityEngine's copies being visible to
# this sibling directory.
find_package(glfw3 CONFIG REQUIRED)
find_package(GLEW REQUIRED)

# Scene-view gizmos of the Unity-style components added later (joints, effectors, Edge
# Collider 2D, Character Controller...).
target_sources(DualityEditor PRIVATE Source/SceneView/ComponentGizmos.cpp)

# Model import pipeline (FBX/glTF/OBJ -> 3DS-ready .dmesh/.mat/.png/.anim + model prefab) and
# its Inspector / Project-window UI. The cooker files have no editor UI dependency (Tests/
# compiles them too).
target_sources(DualityEditor PRIVATE
    Source/Import/ModelImportSettings.cpp
    Source/Import/ModelCooker.cpp
    Source/Import/TextureBaker.cpp
    Source/Import/ModelPrefabBuilder.cpp
    Source/Import/ModelImportUI.cpp
)

target_include_directories(DualityEditor PRIVATE Include)
target_link_libraries(DualityEditor PRIVATE DualityEngine glfw GLEW::GLEW opengl32 comdlg32 ImGui stb assimp::assimp)

# Keep editor-only visual assets next to the executable for both IDE launches and a copied
# desktop editor folder. They are never included by DualityPlayer / the 3DS build.
add_custom_command(TARGET DualityEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Assets"
        "$<TARGET_FILE_DIR:DualityEditor>/Assets"
)
