#include "DualityEngine/UI/UIMarkupParser.h"

#include <cctype>
#include <cstring>

#include "UIParseUtils.h"

namespace Duality {

    namespace {

        bool IsNameChar(char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ':';
        }

        class Parser {
        public:
            explicit Parser(const std::string& source) : m_Source(source) {}

            UIMarkupParseResult Run() {
                UIMarkupParseResult result;
                SkipWhitespaceAndComments();
                if (!StartsWith("<ui")) {
                    result.Error = "expected root <ui> element";
                    return result;
                }
                UIElementNode uiRoot;
                if (!ParseElement(uiRoot, result)) {
                    result.Error = m_Error.empty() ? "malformed markup" : m_Error;
                    return result;
                }
                result.Root = std::move(uiRoot);
                result.Success = true;
                return result;
            }

        private:
            const std::string& m_Source;
            size_t m_Pos = 0;
            std::string m_Error;

            char Peek() const { return m_Pos < m_Source.size() ? m_Source[m_Pos] : '\0'; }
            bool StartsWith(const char* s) const { return m_Source.compare(m_Pos, std::strlen(s), s) == 0; }

            void SkipWhitespaceAndComments() {
                while (m_Pos < m_Source.size()) {
                    if (std::isspace(static_cast<unsigned char>(Peek()))) {
                        m_Pos++;
                    } else if (StartsWith("<!--")) {
                        size_t end = m_Source.find("-->", m_Pos);
                        m_Pos = (end == std::string::npos) ? m_Source.size() : end + 3;
                    } else {
                        break;
                    }
                }
            }

            std::string ParseName() {
                size_t start = m_Pos;
                while (m_Pos < m_Source.size() && IsNameChar(m_Source[m_Pos]))
                    m_Pos++;
                return m_Source.substr(start, m_Pos - start);
            }

            // Parses `key="value"` or `key='value'` pairs until '>' or "/>". Returns false (and
            // sets m_Error) on a genuinely malformed tag (unterminated quote, no closing '>').
            bool ParseAttributes(std::unordered_map<std::string, std::string>& outAttributes, bool& outSelfClosing) {
                outSelfClosing = false;
                while (true) {
                    SkipWhitespaceAndComments();
                    if (StartsWith("/>")) { m_Pos += 2; outSelfClosing = true; return true; }
                    if (Peek() == '>') { m_Pos += 1; return true; }
                    if (m_Pos >= m_Source.size()) { m_Error = "unterminated tag"; return false; }

                    std::string key = ParseName();
                    if (key.empty()) { m_Error = "expected attribute name or '>'"; return false; }
                    SkipWhitespaceAndComments();

                    if (Peek() == '=') {
                        m_Pos++;
                        SkipWhitespaceAndComments();
                        char quote = Peek();
                        if (quote != '"' && quote != '\'') { m_Error = "expected quoted attribute value for '" + key + "'"; return false; }
                        m_Pos++;
                        size_t start = m_Pos;
                        size_t end = m_Source.find(quote, m_Pos);
                        if (end == std::string::npos) { m_Error = "unterminated attribute value for '" + key + "'"; return false; }
                        outAttributes[key] = m_Source.substr(start, end - start);
                        m_Pos = end + 1;
                    } else {
                        outAttributes[key] = "true"; // bare boolean-style attribute, e.g. `disabled`
                    }
                }
            }

            // Parses one element starting at the current '<' through its matching closing tag
            // (or immediately, if self-closing). `<style>`/`<link>` children are pulled directly
            // into `result` and never appear in the returned tree -- see UIMarkupParser.h.
            bool ParseElement(UIElementNode& outNode, UIMarkupParseResult& result) {
                if (Peek() != '<') { m_Error = "expected '<'"; return false; }
                m_Pos++;
                std::string tag = ParseName();
                if (tag.empty()) { m_Error = "expected tag name"; return false; }
                outNode.Tag = tag;

                std::unordered_map<std::string, std::string> attributes;
                bool selfClosing = false;
                if (!ParseAttributes(attributes, selfClosing))
                    return false;

                // <style> is special: everything up to the literal "</style>" is raw stylesheet
                // text, not nested markup -- CSS itself freely contains '<'/'>'/'{'/'}' in ways
                // that would otherwise confuse this tag scanner.
                if (tag == "style" && !selfClosing) {
                    size_t end = m_Source.find("</style>", m_Pos);
                    if (end == std::string::npos) { m_Error = "unterminated <style> block"; return false; }
                    result.InlineStylesheetSources.push_back(m_Source.substr(m_Pos, end - m_Pos));
                    m_Pos = end + std::strlen("</style>");
                    return true; // caller (ParseChildren) discards this node, never adds it to the tree
                }
                if (tag == "link") {
                    auto it = attributes.find("href");
                    if (it != attributes.end())
                        result.LinkedStylesheetPaths.push_back(it->second);
                    return true; // self-closing by convention; even if not, no children expected
                }

                ExtractIdAndClass(attributes, outNode);
                auto styleIt = attributes.find("style");
                if (styleIt != attributes.end()) {
                    outNode.InlineStyle = UIParseDeclarationBlock(styleIt->second);
                    attributes.erase(styleIt);
                }
                outNode.Attributes = std::move(attributes);

                if (selfClosing)
                    return true;

                std::string pendingText;
                while (true) {
                    if (m_Pos >= m_Source.size()) { m_Error = "unterminated <" + tag + ">"; return false; }
                    if (StartsWith("</")) {
                        m_Pos += 2;
                        std::string closeTag = ParseName();
                        SkipWhitespaceAndComments();
                        if (Peek() != '>') { m_Error = "malformed closing tag for <" + tag + ">"; return false; }
                        m_Pos++;
                        if (closeTag != tag) { m_Error = "mismatched closing tag: <" + tag + "> closed by </" + closeTag + ">"; return false; }
                        break;
                    }
                    if (StartsWith("<!--")) { SkipWhitespaceAndComments(); continue; }
                    if (Peek() == '<') {
                        UIElementNode child;
                        if (!ParseElement(child, result))
                            return false;
                        if (!child.Tag.empty() && child.Tag != "style" && child.Tag != "link")
                            outNode.Children.push_back(std::move(child));
                        continue;
                    }
                    // Text content -- captured up to the next '<', trimmed, and (if this element
                    // ends up with no explicit "text" attribute) used as one -- lets `<Text>Score:
                    // 0</Text>` work as naturally as `<Text text="Score: 0"/>`.
                    size_t start = m_Pos;
                    size_t next = m_Source.find('<', m_Pos);
                    if (next == std::string::npos) next = m_Source.size();
                    pendingText += m_Source.substr(start, next - start);
                    m_Pos = next;
                }

                std::string trimmedText = UITrim(pendingText);
                if (!trimmedText.empty() && outNode.Attributes.find("text") == outNode.Attributes.end())
                    outNode.Attributes["text"] = trimmedText;

                return true;
            }

            void ExtractIdAndClass(std::unordered_map<std::string, std::string>& attributes, UIElementNode& node) {
                auto idIt = attributes.find("id");
                if (idIt != attributes.end()) { node.Id = idIt->second; attributes.erase(idIt); }
                auto classIt = attributes.find("class");
                if (classIt != attributes.end()) { node.Classes = UISplitClasses(classIt->second); attributes.erase(classIt); }
            }
        };

    }

    UIMarkupParseResult UIMarkupParser::Parse(const std::string& source) {
        Parser parser(source);
        return parser.Run();
    }

}
