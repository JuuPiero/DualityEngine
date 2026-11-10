#pragma once

#include <filesystem>
#include <string>

struct ImDrawList;
struct ImVec2;

namespace Duality {

    struct EditorContext;
    class Entity;

    // Editor UI of the model importer: the Model Import Settings inspector (Unity's Model / Rig /
    // Animation / Materials tabs, Apply / Revert, 3DS budget report), the Project-window import
    // badge, the global "Importing..." indicator and the importer's automation commands.
    namespace ModelImportUI {
        // Asset inspectors for .fbx / .gltf / .glb / .obj (import settings) and .dmesh (mesh info).
        void RegisterInspectors();
        // Once per frame (Application): finalizes background imports, watches Assets, refreshes
        // renderer caches after a re-import and draws the progress indicator.
        void Update(EditorContext& ctx);
        // Content Browser tile overlay while a model is queued / importing / failed.
        void DrawTileBadge(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const std::filesystem::path& path);
        // Instantiates a model (its generated prefab, linked to the model GUID) into the scene
        // like a Project -> Scene drop. Empty Entity when the model has not been imported.
        Entity InstantiateModel(EditorContext& ctx, const std::string& modelGuid, Entity parent);
    }

}
