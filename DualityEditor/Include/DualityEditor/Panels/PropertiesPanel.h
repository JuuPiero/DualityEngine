#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "DualityEngine/ECS/Entity.h"

namespace Duality {

    struct EditorContext;
    struct ComponentTypeInfo;
    struct ScriptInstance;

    // Unity's Inspector. For entities: the GameObject header (active toggle, name, Tag, Layer,
    // prefab row with Open / Select / Overrides), one card per component (foldout, enable
    // checkbox, kebab menu with Reset / Remove / Move / Copy / Paste / Edit Script), the
    // categorized searchable Add Component popup (built-in components, C++ and TypeScript
    // scripts, New Script). Several selected entities are edited together: only the components
    // all of them share are shown, differing values display as "-", and an edit applies to every
    // object as one Undo step. Prefab instance values that differ from the prefab asset are bold
    // with Unity's blue margin bar (right-click > Revert). For assets: the registered asset
    // inspector (AssetInspectorRegistry).
    class PropertiesPanel {
    public:
        void OnImGuiRender(EditorContext& ctx);

    private:
        void DrawEntityHeader(EditorContext& ctx, const std::vector<Entity>& targets);
        void DrawPrefabRow(EditorContext& ctx, Entity primary);
        void DrawComponentCard(EditorContext& ctx, ComponentTypeInfo& type, const std::vector<Entity>& targets);
        void DrawScriptCards(EditorContext& ctx, Entity entity);
        void DrawAddComponent(EditorContext& ctx, const std::vector<Entity>& targets);
        bool DrawComponentMenu(EditorContext& ctx, ComponentTypeInfo& type, const std::vector<Entity>& targets);

        // Prefab overrides: the prefab asset's JSON (cached by path + write time) and the value a
        // field of `entity` has in it (null when the entity / field cannot be matched).
        const nlohmann::json* PrefabEntityJson(EditorContext& ctx, Entity entity, bool& isRoot);
        int CountPrefabOverrides(EditorContext& ctx, Entity root);

        char m_AddComponentFilter[128] = "";
        std::string m_AddComponentCategory; // "" = category list (Unity's drill-down)
        bool m_NewScriptPage = false;
        char m_NewScriptName[64] = "NewBehaviour";
        int m_NewScriptLanguage = 1; // 1 = TypeScript, 0 = C++

        bool m_Locked = false;
        std::vector<uint64_t> m_LockedIds;
        std::string m_LockedAssetPath;

        // Copy Component / Paste Component Values / Paste Component As New.
        std::string m_CopiedComponentType;
        nlohmann::json m_CopiedComponent;

        std::string m_PrefabCachePath;
        std::filesystem::file_time_type m_PrefabCacheTime{};
        nlohmann::json m_PrefabCache;
    };

}
