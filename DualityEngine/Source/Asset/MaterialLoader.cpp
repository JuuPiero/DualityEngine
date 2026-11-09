#include "DualityEngine/Asset/MaterialLoader.h"

#include <algorithm>
#include <fstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "DualityEngine/Core/Log.h"

using json = nlohmann::json;

namespace Duality {

    namespace {
        std::unordered_map<std::string, Material> s_Cache;

        constexpr int MaterialFormatVersion = 2;

        json ColorToJson(const glm::vec4& value) { return json::array({ value.r, value.g, value.b, value.a }); }

        // Tolerates [r,g,b] as well as [r,g,b,a] (alpha defaults to 1) and ignores anything
        // that isn't an array of numbers, keeping the prototype value instead.
        void ReadVec4(const json& properties, const char* key, glm::vec4& value, float defaultW = 1.0f) {
            auto it = properties.find(key);
            if (it == properties.end() || !it->is_array() || it->size() < 3)
                return;
            for (size_t i = 0; i < it->size() && i < 4; ++i)
                if (!(*it)[i].is_number()) return;
            value = { (*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>(),
                      it->size() >= 4 ? (*it)[3].get<float>() : defaultW };
        }

        void ReadFloat(const json& properties, const char* key, float& value) {
            auto it = properties.find(key);
            if (it != properties.end() && it->is_number())
                value = it->get<float>();
        }

        void ReadInt(const json& properties, const char* key, int& value) {
            auto it = properties.find(key);
            if (it != properties.end() && it->is_number())
                value = static_cast<int>(it->get<double>());
        }

        void ReadGuid(const json& properties, const char* key, AssetRef& value) {
            auto it = properties.find(key);
            if (it != properties.end() && it->is_string())
                value.Guid = it->get<std::string>();
        }

        // Pre-shader format (MaterialFormatVersion 1, never written with a "Shader" key):
        //   { "Color": [r,g,b,a], "Texture": "<guid>", "Shading Mode": "Unlit" | "VertexLit" }
        // Migrates to the shader whose output is identical: Unlit/Texture (texture * color *
        // vertex color, opaque, no culling) or Mobile/VertexLit. Nothing is written back to disk
        // until the material is edited, so an untouched project stays byte-identical.
        Material FromLegacyJson(const json& root) {
            Material material; // legacy defaults: Unlit/Texture, Opaque, Cull Off
            ReadVec4(root, "Color", material.Color);
            ReadGuid(root, "Texture", material.Texture);
            auto shading = root.find("Shading Mode");
            if (shading != root.end() && shading->is_string() && shading->get<std::string>() == "VertexLit")
                material.ShadingMode = MaterialShadingMode::VertexLit;
            material.Normalize();
            return material;
        }

        Material FromJson(const json& root, const std::string& pathForLogs) {
            if (!root.is_object())
                return Material{};
            if (!root.contains("Shader"))
                return FromLegacyJson(root);

            Material material;
            const std::string shaderName = root["Shader"].is_string() ? root["Shader"].get<std::string>() : std::string();
            if (!FindShaderByName(shaderName, material.Shader)) {
                Log::Warn("MaterialLoader: unknown shader '" + shaderName + "' in '" + pathForLogs + "', using Unlit/Texture");
                material.Shader = BuiltinShader::UnlitTexture;
            }
            // ShadingMode is derived from the shader by Normalize(); keep it consistent so the
            // legacy VertexLit rule cannot override an explicit Unlit shader choice.
            material.ShadingMode = GetShaderInfo(material.Shader).Model == LightingModel::Unlit
                ? MaterialShadingMode::Unlit : MaterialShadingMode::VertexLit;

            if (root.contains("Surface") && root["Surface"].is_string())
                ParseSurfaceType(root["Surface"].get<std::string>(), material.Surface);
            if (root.contains("Cull") && root["Cull"].is_string())
                ParseCullMode(root["Cull"].get<std::string>(), material.Cull);
            if (root.contains("ZWrite") && root["ZWrite"].is_string())
                ParseZWriteMode(root["ZWrite"].get<std::string>(), material.ZWrite);
            ReadInt(root, "Render Queue", material.RenderQueue);

            const json& properties = root.contains("Properties") && root["Properties"].is_object() ? root["Properties"] : root;
            ReadVec4(properties, "_Color", material.Color);
            ReadGuid(properties, "_MainTex", material.Texture);
            ReadVec4(properties, "_MainTex_ST", material.MainTexST, 0.0f);
            ReadGuid(properties, "_BumpMap", material.NormalMap);
            ReadFloat(properties, "_Cutoff", material.Cutoff);
            ReadVec4(properties, "_SpecColor", material.SpecColor);
            ReadFloat(properties, "_Shininess", material.Shininess);
            ReadVec4(properties, "_EmissionColor", material.EmissionColor);
            ReadVec4(properties, "_ShadeColor", material.ShadeColor);
            ReadInt(properties, "_ToonSteps", material.ToonSteps);
            ReadFloat(properties, "_ToonSmoothness", material.ToonSmoothness);
            material.Normalize();
            return material;
        }

        json ToJson(Material material) {
            material.Normalize();
            json root;
            root["Version"] = MaterialFormatVersion;
            root["Shader"] = GetShaderInfo(material.Shader).Name;
            root["Surface"] = SurfaceTypeName(material.Surface);
            root["Cull"] = CullModeName(material.Cull);
            root["ZWrite"] = ZWriteModeName(material.ZWrite);
            root["Render Queue"] = material.RenderQueue;
            // Every property is written regardless of the current shader (Unity keeps them
            // too), so switching Lit/Specular -> Unlit -> Lit/Specular loses nothing.
            json properties;
            properties["_Color"] = ColorToJson(material.Color);
            properties["_MainTex"] = material.Texture.Guid;
            properties["_MainTex_ST"] = ColorToJson(material.MainTexST);
            properties["_BumpMap"] = material.NormalMap.Guid;
            properties["_Cutoff"] = material.Cutoff;
            properties["_SpecColor"] = ColorToJson(material.SpecColor);
            properties["_Shininess"] = material.Shininess;
            properties["_EmissionColor"] = ColorToJson(material.EmissionColor);
            properties["_ShadeColor"] = ColorToJson(material.ShadeColor);
            properties["_ToonSteps"] = material.ToonSteps;
            properties["_ToonSmoothness"] = material.ToonSmoothness;
            root["Properties"] = properties;
            return root;
        }
    }

    void Material::Normalize() {
        // Legacy rule: an Unlit-model shader flagged VertexLit (old files, or older code that
        // only knows about ShadingMode) means Mobile/VertexLit.
        if (ShadingMode == MaterialShadingMode::VertexLit && GetShaderInfo().Model == LightingModel::Unlit)
            Shader = BuiltinShader::MobileVertexLit;
        ShadingMode = GetShaderInfo().Model == LightingModel::Unlit ? MaterialShadingMode::Unlit : MaterialShadingMode::VertexLit;

        Cutoff = std::clamp(Cutoff, 0.0f, 1.0f);
        Shininess = std::clamp(Shininess, 0.01f, 1.0f);
        ToonSteps = std::clamp(ToonSteps, 1, 5);
        ToonSmoothness = std::clamp(ToonSmoothness, 0.0f, 1.0f);
        RenderQueue = std::clamp(RenderQueue, -1, 5000);
    }

    Material MaterialLoader::Load(const std::string& path) {
        auto it = s_Cache.find(path);
        if (it != s_Cache.end())
            return it->second;

        Material material;
        std::ifstream file(path);
        if (file) {
            try {
                json root;
                file >> root;
                material = FromJson(root, path);
            } catch (const json::exception&) {
                // Fall through and cache the default Material below.
                material = Material{};
            }
        }

        s_Cache[path] = material;
        return material;
    }

    bool MaterialLoader::Save(const std::string& path, const Material& material) {
        json root = ToJson(material);

        std::ofstream file(path);
        if (!file)
            return false;
        file << root.dump(4);

        Material normalized = material;
        normalized.Normalize();
        s_Cache[path] = normalized; // refresh the cache immediately so the Editor sees the edit this frame
        return true;
    }

    Material MaterialLoader::ParseJsonText(const std::string& text) {
        try {
            return FromJson(json::parse(text), "<memory>");
        } catch (const json::exception&) {
            return Material{};
        }
    }

    std::string MaterialLoader::ToJsonText(const Material& material) {
        return ToJson(material).dump(4);
    }

    void MaterialLoader::ClearCache() {
        s_Cache.clear();
    }

}
