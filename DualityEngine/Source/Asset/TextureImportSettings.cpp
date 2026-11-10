#include "DualityEngine/Asset/TextureImportSettings.h"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Duality {

    namespace {
        std::string MetaPathFor(const std::string& assetPath) { return assetPath + ".meta"; }

        glm::vec2 ReadVec2(const json& value, const glm::vec2& fallback) {
            if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number())
                return fallback;
            return { value[0].get<float>(), value[1].get<float>() };
        }

        glm::vec4 ReadVec4(const json& value, const glm::vec4& fallback) {
            if (!value.is_array() || value.size() != 4)
                return fallback;
            for (const json& component : value)
                if (!component.is_number())
                    return fallback;
            return { value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>() };
        }

        json WriteVec2(const glm::vec2& value) { return json::array({ value.x, value.y }); }
        json WriteVec4(const glm::vec4& value) { return json::array({ value.x, value.y, value.z, value.w }); }
    }

    const SpriteDefinition* TextureImportSettings::FindSprite(const std::string& id) const {
        const int index = FindSpriteIndex(id);
        return index >= 0 ? &Sprites[static_cast<size_t>(index)] : nullptr;
    }

    int TextureImportSettings::FindSpriteIndex(const std::string& id) const {
        if (id.empty())
            return -1;
        for (size_t i = 0; i < Sprites.size(); ++i)
            if (Sprites[i].Id == id)
                return static_cast<int>(i);
        return -1;
    }

    bool TextureImportSettings::SpriteDataEquals(const TextureImportSettings& other) const {
        return SpriteMode == other.SpriteMode && PixelsPerUnit == other.PixelsPerUnit &&
            Width == other.Width && Height == other.Height && HasSpritePivot == other.HasSpritePivot &&
            SpritePivot == other.SpritePivot && SpriteBorder == other.SpriteBorder && Sprites == other.Sprites;
    }

    TextureImportSettings TextureImportSettings::Load(const std::string& assetPath) {
        TextureImportSettings settings;
        std::ifstream file(MetaPathFor(assetPath));
        if (!file)
            return settings;

        try {
            json root;
            file >> root;
            if (!root.contains("Importer") || !root["Importer"].is_object())
                return settings;
            auto& importer = root["Importer"];
            settings.FilterMode = importer.value("FilterMode", std::string()) == "Point" ? TextureFilterMode::Point : TextureFilterMode::Bilinear;
            settings.WrapMode = importer.value("WrapMode", std::string()) == "Repeat" ? TextureWrapMode::Repeat : TextureWrapMode::Clamp;
            settings.GenerateMipmaps = importer.value("GenerateMipmaps", false);
            settings.N3DSFormat = importer.value("N3DSFormat", std::string("Auto"));
            if (importer.contains("MaxSize") && importer["MaxSize"].is_number_integer())
                settings.MaxSize = std::max(0, importer["MaxSize"].get<int>());

            // Sprite data -- every key optional so pre-sprite .meta files load unchanged.
            settings.SpriteMode = importer.value("SpriteMode", std::string()) == "Multiple" ? SpriteImportMode::Multiple : SpriteImportMode::Single;
            if (importer.contains("PixelsPerUnit") && importer["PixelsPerUnit"].is_number())
                settings.PixelsPerUnit = importer["PixelsPerUnit"].get<float>();
            if (importer.contains("TextureSize")) {
                const glm::vec2 size = ReadVec2(importer["TextureSize"], glm::vec2(0.0f));
                settings.Width = static_cast<int>(size.x);
                settings.Height = static_cast<int>(size.y);
            }
            if (importer.contains("SpritePivot")) {
                settings.HasSpritePivot = true;
                settings.SpritePivot = ReadVec2(importer["SpritePivot"], settings.SpritePivot);
            }
            if (importer.contains("SpriteBorder"))
                settings.SpriteBorder = ReadVec4(importer["SpriteBorder"], settings.SpriteBorder);
            if (importer.contains("Sprites") && importer["Sprites"].is_array()) {
                for (const json& entry : importer["Sprites"]) {
                    if (!entry.is_object())
                        continue;
                    SpriteDefinition sprite;
                    sprite.Id = entry.value("Id", std::string());
                    sprite.Name = entry.value("Name", std::string());
                    if (entry.contains("Rect"))
                        sprite.Rect = ReadVec4(entry["Rect"], sprite.Rect);
                    if (entry.contains("Pivot"))
                        sprite.Pivot = ReadVec2(entry["Pivot"], sprite.Pivot);
                    if (entry.contains("Border"))
                        sprite.Border = ReadVec4(entry["Border"], sprite.Border);
                    if (sprite.Id.empty())
                        continue; // an id-less entry could never be referenced; skip rather than invent one
                    settings.Sprites.push_back(std::move(sprite));
                }
            }
        } catch (const json::exception&) {
            // Fall through and return whatever was read so far (defaults for the rest).
        }
        return settings;
    }

    bool TextureImportSettings::Save(const std::string& assetPath, const TextureImportSettings& settings) {
        std::string metaPath = MetaPathFor(assetPath);

        json root;
        std::ifstream inFile(metaPath);
        if (inFile) {
            try {
                inFile >> root;
            } catch (const json::parse_error&) {
                root = json::object(); // corrupt/empty -- start fresh rather than fail the save
            }
        }
        inFile.close();
        if (!root.is_object())
            root = json::object();

        json& importer = root["Importer"];
        if (!importer.is_object())
            importer = json::object();
        importer["FilterMode"] = settings.FilterMode == TextureFilterMode::Point ? "Point" : "Bilinear";
        importer["WrapMode"] = settings.WrapMode == TextureWrapMode::Repeat ? "Repeat" : "Clamp";
        importer["GenerateMipmaps"] = settings.GenerateMipmaps;
        // Written only when set, so an ordinary texture's .meta stays as compact as before.
        if (!settings.N3DSFormat.empty() && settings.N3DSFormat != "Auto")
            importer["N3DSFormat"] = settings.N3DSFormat;
        else
            importer.erase("N3DSFormat");
        if (settings.MaxSize > 0)
            importer["MaxSize"] = settings.MaxSize;
        else
            importer.erase("MaxSize");

        // Sprite keys are only written when they differ from the defaults, so saving an ordinary
        // texture's filter mode keeps its .meta exactly as compact as before sprites existed.
        if (settings.SpriteMode == SpriteImportMode::Multiple)
            importer["SpriteMode"] = "Multiple";
        else
            importer.erase("SpriteMode");
        if (settings.PixelsPerUnit > 0.0f)
            importer["PixelsPerUnit"] = settings.PixelsPerUnit;
        else
            importer.erase("PixelsPerUnit");
        if (settings.Width > 0 && settings.Height > 0)
            importer["TextureSize"] = json::array({ settings.Width, settings.Height });
        else
            importer.erase("TextureSize");
        if (settings.HasSpritePivot)
            importer["SpritePivot"] = WriteVec2(settings.SpritePivot);
        else
            importer.erase("SpritePivot");
        if (settings.SpriteBorder != glm::vec4(0.0f))
            importer["SpriteBorder"] = WriteVec4(settings.SpriteBorder);
        else
            importer.erase("SpriteBorder");
        if (!settings.Sprites.empty()) {
            json sprites = json::array();
            for (const SpriteDefinition& sprite : settings.Sprites) {
                json entry = json::object();
                entry["Id"] = sprite.Id;
                entry["Name"] = sprite.Name;
                entry["Rect"] = WriteVec4(sprite.Rect);
                entry["Pivot"] = WriteVec2(sprite.Pivot);
                if (sprite.Border != glm::vec4(0.0f))
                    entry["Border"] = WriteVec4(sprite.Border);
                sprites.push_back(std::move(entry));
            }
            importer["Sprites"] = std::move(sprites);
        } else {
            importer.erase("Sprites");
        }

        std::ofstream outFile(metaPath);
        if (!outFile)
            return false;
        outFile << root.dump(4);
        return true;
    }

}
