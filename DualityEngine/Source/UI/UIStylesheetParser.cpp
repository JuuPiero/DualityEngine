#include "DualityEngine/UI/UIStylesheetParser.h"

#include <cctype>

#include "UIParseUtils.h"

namespace Duality {

    namespace {

        void SkipWhitespaceAndComments(const std::string& source, size_t& pos) {
            while (pos < source.size()) {
                if (std::isspace(static_cast<unsigned char>(source[pos]))) {
                    pos++;
                } else if (source.compare(pos, 2, "/*") == 0) {
                    size_t end = source.find("*/", pos + 2);
                    pos = (end == std::string::npos) ? source.size() : end + 2;
                } else {
                    break;
                }
            }
        }

        bool IsSelectorChar(char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
        }

    }

    void UIStylesheetParser::ParseInto(const std::string& source, UIStylesheet& outStylesheet) {
        size_t pos = 0;
        while (true) {
            SkipWhitespaceAndComments(source, pos);
            if (pos >= source.size())
                return;

            UIStyleSelector selector;
            char c = source[pos];
            if (c == '.') {
                selector.SelectorKind = UIStyleSelector::Kind::Class;
                pos++;
            } else if (c == '#') {
                selector.SelectorKind = UIStyleSelector::Kind::Id;
                pos++;
            } else if (std::isalpha(static_cast<unsigned char>(c))) {
                selector.SelectorKind = UIStyleSelector::Kind::Element;
            } else {
                // Unrecognized character where a selector was expected (stray '}', compound/
                // descendant selector this parser doesn't support, ...) -- skip to the next '}'
                // so one bad rule doesn't take the rest of the stylesheet down with it, and
                // resume from there.
                size_t recover = source.find('}', pos);
                if (recover == std::string::npos)
                    return;
                pos = recover + 1;
                continue;
            }

            size_t nameStart = pos;
            while (pos < source.size() && IsSelectorChar(source[pos]))
                pos++;
            selector.Value = source.substr(nameStart, pos - nameStart);
            if (selector.Value.empty()) {
                size_t recover = source.find('}', pos);
                if (recover == std::string::npos) return;
                pos = recover + 1;
                continue;
            }

            SkipWhitespaceAndComments(source, pos);
            if (pos >= source.size() || source[pos] != '{') {
                // No '{' following a selector-looking token -- not a rule this parser
                // understands (e.g. a compound selector like "Panel.foo"); skip to past the
                // next '}' and keep going rather than aborting the whole stylesheet.
                size_t recover = source.find('}', pos);
                if (recover == std::string::npos) return;
                pos = recover + 1;
                continue;
            }
            pos++; // skip '{'

            size_t bodyStart = pos;
            size_t bodyEnd = source.find('}', pos);
            if (bodyEnd == std::string::npos)
                return; // unterminated block -- nothing more to parse
            std::string body = source.substr(bodyStart, bodyEnd - bodyStart);
            pos = bodyEnd + 1;

            UIStyleRule rule;
            rule.Selector = selector;
            rule.Declarations = UIParseDeclarationBlock(body);
            outStylesheet.push_back(std::move(rule));
        }
    }

}
