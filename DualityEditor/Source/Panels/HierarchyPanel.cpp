#include "DualityEditor/Panels/HierarchyPanel.h"

#include <imgui.h>

#include "DualityEditor/EditorContext.h"
#include "DualityEngine/Scene/Components.h"

namespace Duality {

    void HierarchyPanel::OnImGuiRender(EditorContext& ctx) {
        ImGui::Begin("Hierarchy");

        if (ImGui::Button("Create Entity", ImVec2(-1, 0)))
            ctx.Selected = ctx.SceneRef.CreateEntity("Entity");

        ImGui::Separator();

        for (auto handle : ctx.SceneRef.Registry().view<NameComponent>()) {
            Entity entity(handle, &ctx.SceneRef);
            const bool isSelected = (entity == ctx.Selected);
            if (ImGui::Selectable(entity.GetComponent<NameComponent>().Name.c_str(), isSelected))
                ctx.Selected = entity;
        }

        ImGui::End();
    }

}
