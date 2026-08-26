#include "DualityEngine/UI/UIDocument.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/UI/UIMarkupParser.h"
#include "DualityEngine/UI/UIStylesheetParser.h"
#include "UIParseUtils.h"

namespace Duality {

    namespace {

        bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); i++)
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            return true;
        }

        float SafeStof(const std::string& text, float fallback) {
            try {
                return std::stof(text);
            } catch (...) {
                return fallback;
            }
        }

        UIAnchor ParseUIAnchor(const std::string& text) {
            if (EqualsIgnoreCase(text, "top-center"))    return UIAnchor::TopCenter;
            if (EqualsIgnoreCase(text, "top-right"))     return UIAnchor::TopRight;
            if (EqualsIgnoreCase(text, "middle-left"))   return UIAnchor::MiddleLeft;
            if (EqualsIgnoreCase(text, "middle-center") || EqualsIgnoreCase(text, "center")) return UIAnchor::MiddleCenter;
            if (EqualsIgnoreCase(text, "middle-right"))  return UIAnchor::MiddleRight;
            if (EqualsIgnoreCase(text, "bottom-left"))   return UIAnchor::BottomLeft;
            if (EqualsIgnoreCase(text, "bottom-center")) return UIAnchor::BottomCenter;
            if (EqualsIgnoreCase(text, "bottom-right"))  return UIAnchor::BottomRight;
            return UIAnchor::TopLeft; // includes "top-left" and any unrecognized value -- same default UIRectComponent itself uses
        }

        // Hex (#rgb, #rrggbb, #rrggbbaa) and rgb()/rgba() -- the two color formats real CSS
        // authors reach for most; deliberately not the full CSS color grammar (no named colors,
        // no hsl()). Malformed input falls back to opaque white rather than throwing -- markup
        // is hand-authored external content, a typo here shouldn't crash the caller.
        glm::vec4 ParseUIColor(const std::string& raw) {
            std::string s = UITrim(raw);
            if (s.empty())
                return glm::vec4(1.0f);

            if (s[0] == '#') {
                std::string hex = s.substr(1);
                auto nibble = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    return (c >= 'a' && c <= 'f') ? (c - 'a' + 10) : 0;
                };
                auto byteAt = [&](size_t i) -> float { return static_cast<float>(nibble(hex[i]) * 16 + nibble(hex[i + 1])) / 255.0f; };
                if (hex.size() == 3)
                    return { static_cast<float>(nibble(hex[0]) * 17) / 255.0f, static_cast<float>(nibble(hex[1]) * 17) / 255.0f, static_cast<float>(nibble(hex[2]) * 17) / 255.0f, 1.0f };
                if (hex.size() == 6)
                    return { byteAt(0), byteAt(2), byteAt(4), 1.0f };
                if (hex.size() == 8)
                    return { byteAt(0), byteAt(2), byteAt(4), byteAt(6) };
                return glm::vec4(1.0f);
            }

            if (s.rfind("rgb", 0) == 0) {
                size_t open = s.find('('), close = s.find(')');
                if (open == std::string::npos || close == std::string::npos || close < open)
                    return glm::vec4(1.0f);
                std::string inner = s.substr(open + 1, close - open - 1);
                std::vector<float> values;
                size_t start = 0;
                while (start <= inner.size()) {
                    size_t comma = inner.find(',', start);
                    std::string token = UITrim(inner.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
                    if (!token.empty())
                        values.push_back(SafeStof(token, 0.0f));
                    if (comma == std::string::npos)
                        break;
                    start = comma + 1;
                }
                float r = values.size() > 0 ? values[0] / 255.0f : 1.0f;
                float g = values.size() > 1 ? values[1] / 255.0f : 1.0f;
                float b = values.size() > 2 ? values[2] / 255.0f : 1.0f;
                float a = values.size() > 3 ? values[3] : 1.0f; // CSS rgba()'s alpha is already 0-1, unlike r/g/b's 0-255
                return { r, g, b, a };
            }

            return glm::vec4(1.0f); // unrecognized format
        }

        bool SelectorMatches(const UIStyleSelector& selector, const UIElementNode& node) {
            switch (selector.SelectorKind) {
                case UIStyleSelector::Kind::Element: return EqualsIgnoreCase(selector.Value, node.Tag);
                case UIStyleSelector::Kind::Class:   return std::find(node.Classes.begin(), node.Classes.end(), selector.Value) != node.Classes.end();
                case UIStyleSelector::Kind::Id:      return !node.Id.empty() && selector.Value == node.Id;
                default: return false;
            }
        }

        // Fills in node.ResolvedStyle (and recurses into children) -- base layer is the raw
        // attributes (screen="Top" x="10" ..., lowest priority, like an HTML presentation
        // attribute), then every matching stylesheet rule applied in ascending specificity
        // order (Element < Class < Id, ties broken by source order via stable_sort), then the
        // element's own inline `style="..."` last/highest -- real CSS cascade semantics.
        void ResolveStyleRecursive(UIElementNode& node, const UIStylesheet& stylesheet) {
            node.ResolvedStyle = node.Attributes;

            std::vector<const UIStyleRule*> matching;
            for (auto& rule : stylesheet)
                if (SelectorMatches(rule.Selector, node))
                    matching.push_back(&rule);
            std::stable_sort(matching.begin(), matching.end(), [](const UIStyleRule* a, const UIStyleRule* b) {
                return a->Selector.Specificity() < b->Selector.Specificity();
            });
            for (const UIStyleRule* rule : matching)
                for (auto& [key, value] : rule->Declarations)
                    node.ResolvedStyle[key] = value;

            for (auto& [key, value] : node.InlineStyle)
                node.ResolvedStyle[key] = value;

            for (auto& child : node.Children)
                ResolveStyleRecursive(child, stylesheet);
        }

        void ApplyResolvedProperties(Entity entity, const std::unordered_map<std::string, std::string>& props, const std::string& baseDirectory) {
            auto& rect = entity.GetComponent<UIRectComponent>();
            if (auto it = props.find("screen"); it != props.end())
                rect.Screen = EqualsIgnoreCase(it->second, "Bottom") ? Screen::Bottom : Screen::Top;
            if (auto it = props.find("anchor"); it != props.end())
                rect.Anchor = ParseUIAnchor(it->second);
            if (auto it = props.find("x"); it != props.end()) rect.Offset.x = SafeStof(it->second, rect.Offset.x);
            if (auto it = props.find("y"); it != props.end()) rect.Offset.y = SafeStof(it->second, rect.Offset.y);
            if (auto it = props.find("width"); it != props.end()) rect.Size.x = SafeStof(it->second, rect.Size.x);
            if (auto it = props.find("height"); it != props.end()) rect.Size.y = SafeStof(it->second, rect.Size.y);

            if (entity.HasComponent<UIImageComponent>()) {
                auto& image = entity.GetComponent<UIImageComponent>();
                auto colorIt = props.find("color");
                if (colorIt == props.end()) colorIt = props.find("background-color");
                if (colorIt != props.end())
                    image.Color = ParseUIColor(colorIt->second);

                auto texIt = props.find("src");
                if (texIt == props.end()) texIt = props.find("background-image");
                if (texIt != props.end() && !texIt->second.empty()) {
                    std::filesystem::path resolved = std::filesystem::path(baseDirectory) / texIt->second;
                    if (std::filesystem::exists(resolved)) {
                        std::string guid = AssetMeta::EnsureMetaFile(resolved);
                        AssetDatabase::Register(guid, resolved.string());
                        image.Texture.Guid = guid;
                    } else {
                        Log::Warn("UIDocument: texture '" + texIt->second + "' not found at '" + resolved.string() + "'");
                    }
                }
            }

            if (entity.HasComponent<UIButtonComponent>()) {
                auto& button = entity.GetComponent<UIButtonComponent>();
                if (auto it = props.find("normal-color"); it != props.end()) button.NormalColor = ParseUIColor(it->second);
                if (auto it = props.find("hover-color"); it != props.end()) button.HoverColor = ParseUIColor(it->second);
                if (auto it = props.find("pressed-color"); it != props.end()) button.PressedColor = ParseUIColor(it->second);
            }

            if (entity.HasComponent<UITextComponent>()) {
                if (auto it = props.find("text"); it != props.end())
                    entity.GetComponent<UITextComponent>().Text = it->second;
            }
        }

    }

    UIDocument UIDocument::Load(const std::string& path) {
        UIDocument doc;

        std::ifstream file(path);
        if (!file.is_open()) {
            Log::Error("UIDocument: could not open '" + path + "'");
            return doc;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();

        UIMarkupParseResult parsed = UIMarkupParser::Parse(buffer.str());
        if (!parsed.Success) {
            Log::Error("UIDocument: failed to parse '" + path + "': " + parsed.Error);
            return doc;
        }

        std::filesystem::path fsPath(path);
        std::string baseDirectory = fsPath.parent_path().string();

        UIStylesheet stylesheet;
        for (auto& inlineCss : parsed.InlineStylesheetSources)
            UIStylesheetParser::ParseInto(inlineCss, stylesheet);
        for (auto& linkedPath : parsed.LinkedStylesheetPaths) {
            std::filesystem::path resolved = std::filesystem::path(baseDirectory) / linkedPath;
            std::ifstream linkedFile(resolved);
            if (!linkedFile.is_open()) {
                Log::Warn("UIDocument: '" + path + "' links stylesheet '" + linkedPath + "' which could not be opened at '" + resolved.string() + "'");
                continue;
            }
            std::stringstream linkedBuffer;
            linkedBuffer << linkedFile.rdbuf();
            UIStylesheetParser::ParseInto(linkedBuffer.str(), stylesheet);
        }

        for (auto& child : parsed.Root.Children)
            ResolveStyleRecursive(child, stylesheet);

        doc.m_Root = std::move(parsed.Root);
        doc.m_BaseDirectory = baseDirectory;
        doc.m_DocumentName = fsPath.stem().string();
        doc.m_Loaded = true;
        Log::Info("UIDocument: loaded '" + path + "'");
        return doc;
    }

    Entity UIDocument::Instantiate(Scene& scene, Screen screen, Entity parent) const {
        if (!m_Loaded)
            return Entity{};

        Entity root = scene.CreateEntity(m_DocumentName.empty() ? "UIDocument" : m_DocumentName);
        for (auto& child : m_Root.Children)
            InstantiateElement(scene, child, screen, root);
        scene.SetParent(root, parent);
        return root;
    }

    void UIDocument::InstantiateElement(Scene& scene, const UIElementNode& node, Screen screen, Entity parent) const {
        Entity entity = scene.CreateEntity(!node.Id.empty() ? node.Id : node.Tag);
        entity.AddComponent<UIRectComponent>().Screen = screen;

        bool isText = EqualsIgnoreCase(node.Tag, "Text");
        bool isButton = EqualsIgnoreCase(node.Tag, "Button");
        if (isText) {
            entity.AddComponent<UITextComponent>();
        } else {
            entity.AddComponent<UIImageComponent>();
            if (isButton)
                entity.AddComponent<UIButtonComponent>();
            else if (!EqualsIgnoreCase(node.Tag, "Panel") && !EqualsIgnoreCase(node.Tag, "Image"))
                Log::Warn("UIDocument: unrecognized element <" + node.Tag + "> -- treated as <Panel>");
        }

        ApplyResolvedProperties(entity, node.ResolvedStyle, m_BaseDirectory);

        if (auto it = node.ResolvedStyle.find("behaviour"); it != node.ResolvedStyle.end())
            entity.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ it->second });

        scene.SetParent(entity, parent);

        // A child with no explicit "screen" of its own inherits THIS element's resolved screen
        // (which ApplyResolvedProperties above may have just overridden from `screen`) -- not
        // the original top-level default passed into Instantiate. Otherwise a `screen="Bottom"`
        // set once on a root Panel wouldn't propagate to its own children, forcing it to be
        // repeated on every single nested element.
        Screen effectiveScreen = entity.GetComponent<UIRectComponent>().Screen;
        for (auto& child : node.Children)
            InstantiateElement(scene, child, effectiveScreen, entity);
    }

}
