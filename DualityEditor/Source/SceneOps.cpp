#include "DualityEditor/SceneOps.h"

#include "DualityEditor/EditorContext.h"
#include "DualityEngine/Scene/SceneSerializer.h"

namespace Duality {

    void MarkSceneDirty(EditorContext& ctx) {
        if (!ctx.IsPlaying)
            ctx.SceneDirty = true;
    }

    void SaveScene(EditorContext& ctx) {
        if (ctx.ScenePath.empty())
            return;
        SceneSerializer(ctx.SceneRef).Serialize(ctx.ScenePath);
        ctx.SceneDirty = false;
    }

    void OpenScene(EditorContext& ctx, const std::string& path) {
        if (ctx.IsPlaying) {
            ctx.SceneRef.OnRuntimeStop();
            ctx.IsPlaying = false;
        }

        ctx.SceneRef.Clear();
        ctx.Selected = Entity();
        ctx.ScenePath = path;
        ctx.TopSceneView = SceneViewCamera();
        ctx.BottomSceneView = SceneViewCamera();
        ctx.TopSceneView3D = SceneViewCamera3D();
        ctx.BottomSceneView3D = SceneViewCamera3D();

        // A brand new/empty scene file just logs an error and leaves the (already-cleared)
        // scene empty -- same graceful-degradation precedent as OpenProjectFromDialog's own
        // first load against a fresh project.
        SceneSerializer(ctx.SceneRef).Deserialize(path);
        ctx.SceneDirty = false;
    }

}
