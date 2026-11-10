#include "DualityEditor/SceneView/SceneAssetDrop.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "DualityEditor/EditorCommands.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEditor/Windows/SpriteAssetUI.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/MeshLoader.h"
#include "DualityEngine/Asset/ModelAsset.h"
#include "DualityEngine/Asset/SpriteDatabase.h"
#include "DualityEngine/Physics/PhysicsUnits.h"
#include "DualityEngine/Renderer/BuiltinMeshes.h"
#include "DualityEngine/Renderer/UIRenderer.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/PrefabSerializer.h"

// Moved out of ScenePanel.cpp unchanged.
namespace Duality {

    namespace {
        // Content Browser uses one generic ASSET_GUID drag payload for every asset type. Map
        // each spawnable asset to its natural component rather than treating all files as a
        // texture: images create Sprite/UI Image, mesh/material create Mesh Renderer, audio
        // creates Audio Source, and prefab creates a linked prefab instance.
        bool IsImageAsset(const std::string& guid) {
            const std::string path = AssetDatabase::ResolvePath(guid);
            if (path.empty())
                return false;
            std::string extension = std::filesystem::path(path).extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
                extension == ".bmp" || extension == ".tga" || extension == ".gif" ||
                extension == ".psd" || extension == ".hdr";
        }

        bool IsPrefabAsset(const std::string& guid) {
            const std::string path = AssetDatabase::ResolvePath(guid);
            if (path.empty())
                return false;
            std::string extension = std::filesystem::path(path).extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            // An imported model (FBX/glTF/OBJ) is a prefab too: dropping it instantiates its
            // generated hierarchy, linked to the model (Unity's model prefab).
            return extension == ".prefab" || (ModelAsset::IsModelFile(path) && !ModelAsset::ResolveInstantiablePrefab(guid).empty());
        }

        std::string AssetExtension(const std::string& guid) {
            const std::string path = AssetDatabase::ResolvePath(guid);
            std::string extension = std::filesystem::path(path).extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension;
        }

        bool IsMeshAsset(const std::string& guid) {
            if (BuiltinMeshes::IsBuiltin(guid))
                return true; // dragged from the Mesh picker
            const std::string extension = AssetExtension(guid);
            return extension == ".obj" || extension == ".mesh" || extension == ".dmesh";
        }

        bool IsAnimationAsset(const std::string& guid) { return AssetExtension(guid) == ".anim"; }

        bool IsMaterialAsset(const std::string& guid) { return AssetExtension(guid) == ".mat"; }
        bool IsAudioAsset(const std::string& guid) {
            const std::string extension = AssetExtension(guid);
            return extension == ".wav" || extension == ".ogg" || extension == ".mp3";
        }

        std::string EntityNameFromAsset(const std::string& guid) {
            ResolvedSprite sprite; // a sliced sprite names the new object (Unity)
            if (SpriteRef::IsSubSprite(guid) && SpriteDatabase::Resolve(guid, sprite) && sprite.IsSubSprite())
                return sprite.Name;
            MeshPrimitive builtinMesh;
            if (BuiltinMeshes::TryGetPrimitive(guid, builtinMesh))
                return BuiltinMeshes::NameOf(builtinMesh);
            const std::string path = AssetDatabase::ResolvePath(guid);
            const std::string stem = path.empty() ? std::string() : std::filesystem::path(path).stem().string();
            return stem.empty() ? "Image" : stem;
        }

        // A Canvas is a screen-space layout context rather than a world-space rectangle.  The
        // currently selected Canvas (or one of its children) therefore makes the user's intent
        // explicit: dropping an image in the matching Scene pane creates an Image under it;
        // otherwise the exact same gesture creates a world SpriteRenderer.
        Entity SelectedCanvasForScreen(EditorContext& ctx, Screen screen) {
            if (!ctx.Selected)
                return {};
            Entity canvas = FindOwningCanvas(ctx.SceneRef, ctx.Selected);
            if (!canvas || !canvas.HasComponent<CanvasComponent>())
                return {};
            const CanvasComponent& component = canvas.GetComponent<CanvasComponent>();
            return component.Enabled && component.Screen == screen ? canvas : Entity{};
        }

        void CreateDroppedImage(EditorContext& ctx, Screen screen, const std::string& guid,
            const glm::vec3& worldPosition, const glm::vec2& canvasPosition) {
            const std::string entityName = EntityNameFromAsset(guid);
            Entity canvas = SelectedCanvasForScreen(ctx, screen);
            if (canvas) {
                Entity image = ctx.SceneRef.CreateEntity(entityName);
                UIRectComponent& rect = image.AddComponent<UIRectComponent>();
                rect.AnchorMin = { 0.5f, 0.5f };
                rect.AnchorMax = { 0.5f, 0.5f };
                rect.Pivot = { 0.5f, 0.5f };
                const glm::vec2 screenSize = screen == Screen::Top
                    ? glm::vec2{ static_cast<float>(TopScreenWidth), static_cast<float>(TopScreenHeight) }
                    : glm::vec2{ static_cast<float>(BottomScreenWidth), static_cast<float>(BottomScreenHeight) };
                rect.AnchoredPosition = canvasPosition - screenSize * 0.5f;
                // Unity's Set Native Size: a sliced sprite (or a texture whose size is known)
                // gets its pixel size; otherwise a neutral square, easy to resize.
                rect.SizeDelta = { 100.0f, 100.0f };
                ResolvedSprite resolvedSprite;
                if (SpriteDatabase::Resolve(guid, resolvedSprite) && resolvedSprite.SizePixels.x > 0.0f && resolvedSprite.SizePixels.y > 0.0f)
                    rect.SizeDelta = resolvedSprite.SizePixels;
                image.AddComponent<UIImageComponent>().Texture.Guid = guid;
                ctx.SceneRef.SetParent(image, canvas);
                ctx.Selected = image;
            } else {
                Entity sprite = ctx.SceneRef.CreateEntity(entityName);
                sprite.GetComponent<TransformComponent>().Translation = worldPosition;
                SpriteRendererComponent& renderer = sprite.AddComponent<SpriteRendererComponent>();
                renderer.Texture.Guid = guid;
                // Unity draws a sprite at its native size (rect px / Pixels Per Unit).
                glm::vec2 nativeSize;
                if (SpriteAssetUI::NativeWorldSize(guid, nativeSize))
                    renderer.Size = nativeSize;
                // Keep 3DS screen routing explicit, rather than leaving a dropped sprite on
                // Default where it could appear on both physical screens.
                ctx.SceneRef.SetParent(sprite, ctx.SceneRef.EnsureScreenRoot(screen), {}, true);
                ctx.Selected = sprite;
            }
            MarkSceneDirty(ctx);
        }

        void CreateDroppedPrefab(EditorContext& ctx, Screen screen, const std::string& guid,
            const glm::vec3& worldPosition, const glm::vec2& canvasPosition) {
            const std::string path = ModelAsset::ResolveInstantiablePrefab(guid); // .prefab or a model's generated prefab
            Entity instance = PrefabSerializer::Instantiate(ctx.SceneRef, path, {}, AssetRef{ guid });
            if (!instance)
                return;

            Entity canvas = SelectedCanvasForScreen(ctx, screen);
            if (canvas && instance.HasComponent<UIRectComponent>()) {
                // A UI prefab gets the same pointer-position treatment as a directly dropped
                // image, while retaining all authored anchors, visuals and child hierarchy.
                ctx.SceneRef.SetParent(instance, canvas, {}, false);
                const glm::vec2 screenSize = screen == Screen::Top
                    ? glm::vec2{ static_cast<float>(TopScreenWidth), static_cast<float>(TopScreenHeight) }
                    : glm::vec2{ static_cast<float>(BottomScreenWidth), static_cast<float>(BottomScreenHeight) };
                instance.GetComponent<UIRectComponent>().AnchoredPosition = canvasPosition - screenSize * 0.5f;
            } else {
                // Non-UI prefabs stay in the world and inherit the physical-screen layer from
                // the Top/Bottom root, exactly like a directly dropped SpriteRenderer.
                ctx.SceneRef.SetParent(instance, ctx.SceneRef.EnsureScreenRoot(screen), {}, true);
                instance.GetComponent<TransformComponent>().Translation = worldPosition;
            }
            ctx.Selected = instance;
            MarkSceneDirty(ctx);
        }

        void CreateDroppedWorldResource(EditorContext& ctx, Screen screen, const std::string& guid,
            const glm::vec3& worldPosition) {
            const std::string name = EntityNameFromAsset(guid);
            Entity entity = ctx.SceneRef.CreateEntity(name);
            entity.GetComponent<TransformComponent>().Translation = worldPosition;
            if (IsMeshAsset(guid)) {
                // Unity: dropping a mesh creates a GameObject with MeshFilter + MeshRenderer.
                entity.AddComponent<MeshFilterComponent>().Mesh = AssetRef{ guid };
                entity.AddComponent<MeshRendererComponent>();
            } else if (IsMaterialAsset(guid)) {
                // A material alone shows on a cube (what a bare Mesh Renderer drew before).
                entity.AddComponent<MeshFilterComponent>().Mesh = BuiltinMeshes::Ref(MeshPrimitive::Cube);
                entity.AddComponent<MeshRendererComponent>().Materials = { AssetRef{ guid } };
            } else if (IsAudioAsset(guid)) {
                entity.AddComponent<AudioSourceComponent>().Clip = AssetRef{ guid };
            } else if (IsAnimationAsset(guid)) {
                entity.AddComponent<AnimationComponent>().Clip = AssetRef{ guid };
            } else {
                ctx.SceneRef.DestroyEntity(entity);
                return;
            }
            ctx.SceneRef.SetParent(entity, ctx.SceneRef.EnsureScreenRoot(screen), {}, true);
            ctx.Selected = entity;
            ctx.SelectedEntities = { entity };
            MarkSceneDirty(ctx);
        }
    }

    // Called directly after a Scene framebuffer Image(), while that image is still ImGui's
    // active drop target. `worldPosition` and `canvasPosition` are each precomputed in the
    // coordinate system relevant to the possible entity type.
    void AcceptSceneAssetDrop(EditorContext& ctx, Screen screen, ImVec2 imagePos, ImVec2 imageSize,
        const glm::vec3& worldPosition, const glm::vec2& canvasPosition) {
        const ImGuiPayload* activePayload = ImGui::GetDragDropPayload();
        const bool draggingAsset = activePayload && activePayload->IsDataType("ASSET_GUID");
        const bool willCreateUI = SelectedCanvasForScreen(ctx, screen);
        const std::string activeGuid = draggingAsset ? static_cast<const char*>(activePayload->Data) : std::string{};
        const bool draggingPrefab = draggingAsset && IsPrefabAsset(activeGuid);
        const bool draggingWorldResource = draggingAsset &&
            (IsMeshAsset(activeGuid) || IsMaterialAsset(activeGuid) || IsAudioAsset(activeGuid) || IsAnimationAsset(activeGuid));
        if (draggingAsset && (draggingPrefab || IsImageAsset(activeGuid) || draggingWorldResource)) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImU32 border = willCreateUI ? IM_COL32(84, 210, 170, 255) : IM_COL32(85, 160, 255, 255);
            drawList->AddRect(imagePos, ImVec2(imagePos.x + imageSize.x, imagePos.y + imageSize.y), border, 2.0f, 0, 2.0f);
            const char* hint = draggingPrefab ? "Drop prefab: instantiate"
                : (IsMeshAsset(activeGuid) ? "Drop mesh: create Mesh Renderer"
                : (IsMaterialAsset(activeGuid) ? "Drop material: create Mesh Renderer"
                : (IsAudioAsset(activeGuid) ? "Drop audio: create Audio Source"
                : (IsAnimationAsset(activeGuid) ? "Drop animation: create Animation"
                : (willCreateUI ? "Drop image: create UI Image" : "Drop image: create Sprite")))));
            const ImVec2 textSize = ImGui::CalcTextSize(hint);
            const ImVec2 textPos{ imagePos.x + (imageSize.x - textSize.x) * 0.5f, imagePos.y + 10.0f };
            drawList->AddRectFilled(ImVec2(textPos.x - 6.0f, textPos.y - 3.0f),
                ImVec2(textPos.x + textSize.x + 6.0f, textPos.y + textSize.y + 3.0f), IM_COL32(15, 20, 30, 210), 3.0f);
            drawList->AddText(textPos, border, hint);
        }

        if (!ImGui::BeginDragDropTarget())
            return;
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID")) {
            const std::string guid(static_cast<const char*>(payload->Data));
            if (IsImageAsset(guid))
                CreateDroppedImage(ctx, screen, guid, worldPosition, canvasPosition);
            else if (IsPrefabAsset(guid))
                CreateDroppedPrefab(ctx, screen, guid, worldPosition, canvasPosition);
            else
                CreateDroppedWorldResource(ctx, screen, guid, worldPosition);
        }
        ImGui::EndDragDropTarget();
    }

}
