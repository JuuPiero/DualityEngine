#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace Duality {

    // One parsed element from a .uidoc markup file (e.g. `<Button id="playBtn" class="primary"
    // x="10" y="10">`) -- a plain data tree, not yet resolved against any stylesheet or turned
    // into real entities. `Attributes` holds every attribute as raw text (already resolved: an
    // inline `style="..."` attribute is parsed separately into `InlineStyle` by the markup
    // parser, not left as a string here) -- UIDocument::Instantiate is what interprets specific
    // attribute names into real component field values.
    struct UIElementNode {
        std::string Tag;      // "Panel", "Button", "Text", ... -- see UIElementTypeFromTag
        std::string Id;       // attribute "id", empty if none
        std::vector<std::string> Classes; // attribute "class", space-separated
        std::unordered_map<std::string, std::string> Attributes; // every attribute except id/class/style
        std::unordered_map<std::string, std::string> InlineStyle; // parsed from attribute "style" (CSS declarations, no selector)
        std::vector<UIElementNode> Children;

        // Filled in by UIDocument's cascade-resolution pass (Attributes as the base layer,
        // matching UIStylesheet rules applied in ascending specificity order, InlineStyle
        // applied last/highest) -- what Instantiate actually reads to set component fields.
        // Empty until that pass runs; not touched by the markup parser itself.
        std::unordered_map<std::string, std::string> ResolvedStyle;
    };

    // A parsed .uidoc document, before Instantiate -- the element tree plus every stylesheet
    // rule collected from <style> blocks and <link href="..."> references (both merged into one
    // cascade, order-of-appearance in the source file determines which cascades first when
    // specificity ties, same as real CSS/Polyphase-Engine's own UIStyleSheet).
    struct UIStyleSelector {
        enum class Kind { Element, Class, Id };
        Kind SelectorKind;
        std::string Value; // element tag name, class name (no leading '.'), or id (no leading '#')

        // Real CSS specificity ordering: Id > Class > Element -- higher wins when multiple
        // rules match the same element, applied in ascending order so higher-specificity
        // property values simply overwrite earlier ones (matches Polyphase-Engine's own
        // GetSpecificity/cascade approach).
        int Specificity() const {
            switch (SelectorKind) {
                case Kind::Id:      return 100;
                case Kind::Class:   return 10;
                case Kind::Element: default: return 1;
            }
        }
    };

    struct UIStyleRule {
        UIStyleSelector Selector;
        std::unordered_map<std::string, std::string> Declarations;
    };

    using UIStylesheet = std::vector<UIStyleRule>;

}
