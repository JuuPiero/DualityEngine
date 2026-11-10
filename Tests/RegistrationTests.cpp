// Registration completeness: every built-in component is reflected, serializable and has an Add
// Component category; the TypeScript API (Tools/TypeScript/duality/duality.ts) only names
// components and fields that the C++ registration really has, so renaming a component or a
// field without updating duality.ts fails here instead of silently returning undefined in TS.
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include "TestFramework.h"

#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"

using namespace Duality;

namespace {
    std::string ReadText(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    std::vector<std::string> QuotedStrings(const std::string& text) {
        std::vector<std::string> out;
        static const std::regex quoted("'([^']*)'");
        for (auto it = std::sregex_iterator(text.begin(), text.end(), quoted); it != std::sregex_iterator(); ++it)
            out.push_back((*it)[1].str());
        return out;
    }

    bool HasField(const ComponentTypeInfo& type, const std::string& name) {
        for (const FieldHandle& field : type.Fields)
            if (field.Name == name)
                return true;
        return false;
    }
}

TEST_CASE("Every registered component has fields that read back, a unique name and a category") {
    std::set<std::string> names;
    Scene scene;
    Entity entity = scene.CreateEntity("Probe");
    for (ComponentTypeInfo& type : TypeRegistry::All()) {
        CHECK_SOFT(names.insert(type.DisplayName).second, ("unique name " + type.DisplayName).c_str());
        CHECK_SOFT(TypeRegistry::Find(type.DisplayName) == &type, ("Find(" + type.DisplayName + ")").c_str());
        type.AddDefault(entity);
        CHECK_SOFT(type.Has(entity), ("AddDefault adds " + type.DisplayName).c_str());
        void* component = type.GetPtr(entity);
        std::set<std::string> fieldNames;
        for (const FieldHandle& field : type.Fields) {
            CHECK_SOFT(fieldNames.insert(field.Name).second, (type.DisplayName + "." + field.Name + " is unique").c_str());
            CHECK_SOFT(static_cast<bool>(field.Get) && static_cast<bool>(field.Set), (type.DisplayName + "." + field.Name + " has accessors").c_str());
            const FieldValue value = field.Get(component);
            field.Set(component, value); // must accept its own value type
        }
        // Built-in components (all but the special "Scripts" holder) are listed in Add Component.
        if (type.DisplayName != "Scripts" && !type.Mandatory && type.DisplayName != "Prefab Instance")
            CHECK_SOFT(!type.Category.empty(), (type.DisplayName + " has an Add Component category").c_str());
    }
    for (const char* required : { "Mesh Filter", "Mesh Renderer", "Skinned Mesh Renderer", "Transform", "Camera", "Rigidbody 2D", "Rigidbody 3D", "Canvas", "Scripts" })
        CHECK_SOFT(TypeRegistry::Find(required) != nullptr, (std::string("registered: ") + required).c_str());
}

TEST_CASE("duality.ts only names registered components and fields") {
    const std::string source = ReadText(std::string(DUALITY_REPO_ROOT) + "/Tools/TypeScript/duality/duality.ts");
    CHECK(!source.empty());
    // Split into `export class X ... {` blocks.
    static const std::regex classHeader(R"(export class (\w+)(?: extends (\w+))?)");
    std::vector<std::pair<std::string, size_t>> classes;
    std::map<std::string, std::string> baseOf;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), classHeader); it != std::sregex_iterator(); ++it) {
        classes.push_back({ (*it)[1].str(), static_cast<size_t>(it->position()) });
        if ((*it)[2].matched)
            baseOf[(*it)[1].str()] = (*it)[2].str();
    }
    std::map<std::string, std::vector<std::string>> typeNamesOf;
    std::map<std::string, std::set<std::string>> fieldsOf;
    static const std::regex typeNames(R"(static readonly typeNames[^=]*=\s*\[([^\]]*)\])");
    static const std::regex fieldUse(R"(\b(?:this\.(?:get|set)|getVec2|getVec3|getColor|getAsset)\('([^']+)')");
    for (size_t i = 0; i < classes.size(); ++i) {
        const size_t end = i + 1 < classes.size() ? classes[i + 1].second : source.size();
        const std::string body = source.substr(classes[i].second, end - classes[i].second);
        std::smatch match;
        if (std::regex_search(body, match, typeNames))
            typeNamesOf[classes[i].first] = QuotedStrings(match[1].str());
        for (auto it = std::sregex_iterator(body.begin(), body.end(), fieldUse); it != std::sregex_iterator(); ++it)
            if ((*it)[1].str().back() != ' ') // `'Point ' + i`: a name built at runtime
                fieldsOf[classes[i].first].insert((*it)[1].str());
    }
    CHECK_SOFT(typeNamesOf.size() > 30, "found the component wrapper classes");

    int checkedFields = 0;
    for (const auto& [className, names] : typeNamesOf) {
        for (const std::string& name : names)
            CHECK_SOFT(TypeRegistry::Find(name) != nullptr, (className + ".typeNames: '" + name + "' is a registered component").c_str());
    }
    for (const auto& [className, fields] : fieldsOf) {
        // A class without its own typeNames uses its base class's (Collider2D -> BoxCollider2D...).
        std::string owner = className;
        while (!typeNamesOf.count(owner) && baseOf.count(owner))
            owner = baseOf[owner];
        if (!typeNamesOf.count(owner) || typeNamesOf[owner].empty())
            continue;
        for (const std::string& field : fields) {
            bool found = false;
            for (const std::string& typeName : typeNamesOf[owner])
                if (const ComponentTypeInfo* type = TypeRegistry::Find(typeName))
                    found |= HasField(*type, field);
            CHECK_SOFT(found, (className + " uses field '" + field + "' that none of its components has").c_str());
            checkedFields++;
        }
    }
    CHECK_SOFT(checkedFields > 60, "checked the fields duality.ts reads/writes");
}
