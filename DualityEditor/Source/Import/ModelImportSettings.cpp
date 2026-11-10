#include "DualityEditor/Import/ModelImportSettings.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>

using json = nlohmann::json;

namespace Duality {

    namespace {
        template<typename Enum>
        struct EnumName { Enum Value; const char* Name; };

        const EnumName<ModelNormalsMode> NormalsNames[] = {
            { ModelNormalsMode::Import, "Import" }, { ModelNormalsMode::Calculate, "Calculate" }, { ModelNormalsMode::None, "None" } };
        const EnumName<ModelTangentsMode> TangentsNames[] = {
            { ModelTangentsMode::Import, "Import" }, { ModelTangentsMode::Calculate, "Calculate" }, { ModelTangentsMode::None, "None" } };
        const EnumName<ModelRigType> RigNames[] = { { ModelRigType::None, "None" }, { ModelRigType::Generic, "Generic" } };
        const EnumName<ModelMaterialCreation> MaterialNames[] = {
            { ModelMaterialCreation::Import, "Import" }, { ModelMaterialCreation::None, "None" } };
        const EnumName<ModelMeshCompression> CompressionNames[] = {
            { ModelMeshCompression::Off, "Off" }, { ModelMeshCompression::On, "On" } };

        template<typename Enum, size_t N>
        const char* NameOf(const EnumName<Enum> (&names)[N], Enum value) {
            for (const auto& entry : names)
                if (entry.Value == value) return entry.Name;
            return names[0].Name;
        }

        template<typename Enum, size_t N>
        void ReadEnum(const json& root, const char* key, const EnumName<Enum> (&names)[N], Enum& value) {
            auto it = root.find(key);
            if (it == root.end() || !it->is_string())
                return;
            const std::string text = it->get<std::string>();
            for (const auto& entry : names)
                if (text == entry.Name) { value = entry.Value; return; }
        }

        template<typename T>
        void ReadNumber(const json& root, const char* key, T& value) {
            auto it = root.find(key);
            if (it != root.end() && it->is_number())
                value = it->get<T>();
        }

        void ReadBool(const json& root, const char* key, bool& value) {
            auto it = root.find(key);
            if (it != root.end() && it->is_boolean())
                value = it->get<bool>();
        }

        void ReadString(const json& root, const char* key, std::string& value) {
            auto it = root.find(key);
            if (it != root.end() && it->is_string())
                value = it->get<std::string>();
        }

        const char* const TextureFormats[] = { "Auto", "RGBA8", "RGB8", "RGBA5551", "RGB565", "RGBA4", "ETC1", "ETC1A4" };

        json ReadMetaFile(const std::string& metaPath) {
            std::ifstream input(metaPath, std::ios::binary);
            if (!input)
                return json::object();
            std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            // Files written by Windows tools may start with a UTF-8 BOM.
            if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
                static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
                text.erase(0, 3);
            json root = json::parse(text, nullptr, false);
            return root.is_object() ? root : json::object();
        }
    }

    uint64_t Fnv1a64(const void* data, size_t size, uint64_t seed) {
        uint64_t hash = seed;
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    std::string HexString(uint64_t value) {
        char buffer[17];
        std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
        return buffer;
    }

    const char* const* ModelImportSettings::TextureFormatNames(int& count) {
        count = static_cast<int>(sizeof(TextureFormats) / sizeof(TextureFormats[0]));
        return TextureFormats;
    }

    json ModelImportSettings::ToJson() const {
        json root = json::object();
        root["ScaleFactor"] = ScaleFactor;
        root["ConvertUnits"] = ConvertUnits;
        root["Normals"] = NameOf(NormalsNames, Normals);
        root["SmoothingAngle"] = SmoothingAngle;
        root["Tangents"] = NameOf(TangentsNames, Tangents);
        root["OptimizeMesh"] = OptimizeMesh;
        root["MeshCompression"] = NameOf(CompressionNames, MeshCompression);
        root["ReadWrite"] = ReadWrite;
        root["ImportVertexColors"] = ImportVertexColors;
        root["MaxTextureSize"] = MaxTextureSize;
        root["TextureFormat"] = TextureFormat;
        root["Rig"] = NameOf(RigNames, Rig);
        root["MaxBonesPerVertex"] = MaxBonesPerVertex;
        root["ImportAnimation"] = ImportAnimation;
        root["SampleRate"] = SampleRate;
        root["KeyframeReduction"] = KeyframeReduction;
        root["PositionError"] = PositionError;
        root["RotationError"] = RotationError;
        root["ScaleError"] = ScaleError;
        root["RemoveRootMotion"] = RemoveRootMotion;
        json clips = json::array();
        for (const ModelClipSettings& clip : Clips)
            clips.push_back({ { "Name", clip.Name }, { "Take", clip.Take }, { "Start", clip.Start }, { "End", clip.End }, { "Loop", clip.Loop } });
        root["Clips"] = std::move(clips);
        root["MaterialCreation"] = NameOf(MaterialNames, MaterialCreation);
        json remaps = json::object();
        for (const auto& [name, guid] : MaterialRemaps)
            if (!guid.empty()) remaps[name] = guid;
        root["MaterialRemaps"] = std::move(remaps);
        json textures = json::object();
        for (const auto& [key, guid] : TextureRemaps)
            if (!guid.empty()) textures[key] = guid;
        root["TextureRemaps"] = std::move(textures);
        return root;
    }

    ModelImportSettings ModelImportSettings::FromJson(const json& root) {
        ModelImportSettings settings;
        if (!root.is_object())
            return settings;
        ReadNumber(root, "ScaleFactor", settings.ScaleFactor);
        ReadBool(root, "ConvertUnits", settings.ConvertUnits);
        ReadEnum(root, "Normals", NormalsNames, settings.Normals);
        ReadNumber(root, "SmoothingAngle", settings.SmoothingAngle);
        ReadEnum(root, "Tangents", TangentsNames, settings.Tangents);
        ReadBool(root, "OptimizeMesh", settings.OptimizeMesh);
        ReadEnum(root, "MeshCompression", CompressionNames, settings.MeshCompression);
        ReadBool(root, "ReadWrite", settings.ReadWrite);
        ReadBool(root, "ImportVertexColors", settings.ImportVertexColors);
        ReadNumber(root, "MaxTextureSize", settings.MaxTextureSize);
        ReadString(root, "TextureFormat", settings.TextureFormat);
        ReadEnum(root, "Rig", RigNames, settings.Rig);
        ReadNumber(root, "MaxBonesPerVertex", settings.MaxBonesPerVertex);
        ReadBool(root, "ImportAnimation", settings.ImportAnimation);
        ReadNumber(root, "SampleRate", settings.SampleRate);
        ReadBool(root, "KeyframeReduction", settings.KeyframeReduction);
        ReadNumber(root, "PositionError", settings.PositionError);
        ReadNumber(root, "RotationError", settings.RotationError);
        ReadNumber(root, "ScaleError", settings.ScaleError);
        ReadBool(root, "RemoveRootMotion", settings.RemoveRootMotion);
        if (auto clips = root.find("Clips"); clips != root.end() && clips->is_array()) {
            for (const json& entry : *clips) {
                if (!entry.is_object())
                    continue;
                ModelClipSettings clip;
                ReadString(entry, "Name", clip.Name);
                ReadString(entry, "Take", clip.Take);
                ReadNumber(entry, "Start", clip.Start);
                ReadNumber(entry, "End", clip.End);
                ReadBool(entry, "Loop", clip.Loop);
                settings.Clips.push_back(std::move(clip));
            }
        }
        ReadEnum(root, "MaterialCreation", MaterialNames, settings.MaterialCreation);
        if (auto remaps = root.find("MaterialRemaps"); remaps != root.end() && remaps->is_object())
            for (const auto& [name, guid] : remaps->items())
                if (guid.is_string() && !guid.get<std::string>().empty()) settings.MaterialRemaps[name] = guid.get<std::string>();
        if (auto remaps = root.find("TextureRemaps"); remaps != root.end() && remaps->is_object())
            for (const auto& [key, guid] : remaps->items())
                if (guid.is_string() && !guid.get<std::string>().empty()) settings.TextureRemaps[key] = guid.get<std::string>();

        // Clamp to what the cooker and both players support.
        settings.ScaleFactor = settings.ScaleFactor > 0.0f ? settings.ScaleFactor : 1.0f;
        settings.SmoothingAngle = std::clamp(settings.SmoothingAngle, 0.0f, 180.0f);
        settings.MaxTextureSize = std::clamp(settings.MaxTextureSize, 8, 1024);
        settings.MaxBonesPerVertex = std::clamp(settings.MaxBonesPerVertex, 1, 4);
        settings.SampleRate = std::clamp(settings.SampleRate, 1.0f, 120.0f);
        settings.PositionError = std::max(0.0f, settings.PositionError);
        settings.RotationError = std::max(0.0f, settings.RotationError);
        settings.ScaleError = std::max(0.0f, settings.ScaleError);
        int formatCount = 0;
        const char* const* formats = TextureFormatNames(formatCount);
        if (std::find_if(formats, formats + formatCount, [&](const char* name) { return settings.TextureFormat == name; }) == formats + formatCount)
            settings.TextureFormat = "Auto";
        return settings;
    }

    std::string ModelImportSettings::Hash() const {
        const std::string text = ToJson().dump();
        return HexString(Fnv1a64(text.data(), text.size()));
    }

    ModelImportSettings ModelImportSettings::Load(const std::string& modelPath) {
        const json root = ReadMetaFile(modelPath + ".meta");
        auto it = root.find("ModelImporterSettings");
        return it != root.end() ? FromJson(*it) : ModelImportSettings{};
    }

    bool ModelImportSettings::Save(const std::string& modelPath, const ModelImportSettings& settings) {
        const std::string metaPath = modelPath + ".meta";
        json root = ReadMetaFile(metaPath);
        root["ModelImporterSettings"] = settings.ToJson();
        std::ofstream output(metaPath, std::ios::binary | std::ios::trunc);
        if (!output)
            return false;
        output << root.dump(2);
        return static_cast<bool>(output);
    }

}
