#pragma once

// Internal (not under Include/) -- tiny text-parsing helpers shared by UIMarkupParser.cpp (an
// inline `style="..."` attribute) and UIStylesheetParser.cpp (a real `selector { ... }` rule's
// body uses the exact same `key: value; key2: value2` declaration-block shape).

#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

namespace Duality {

    inline std::string UITrim(const std::string& s) {
        size_t start = 0, end = s.size();
        while (start < end && std::isspace(static_cast<unsigned char>(s[start]))) start++;
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
        return s.substr(start, end - start);
    }

    // Parses a bare `key: value; key2: value2` block (no selector, no braces).
    inline std::unordered_map<std::string, std::string> UIParseDeclarationBlock(const std::string& text) {
        std::unordered_map<std::string, std::string> declarations;
        size_t i = 0;
        while (i < text.size()) {
            while (i < text.size() && (std::isspace(static_cast<unsigned char>(text[i])) || text[i] == ';'))
                i++;
            size_t keyStart = i;
            while (i < text.size() && text[i] != ':' && text[i] != ';')
                i++;
            if (i >= text.size() || text[i] != ':')
                break; // malformed trailing text -- stop rather than loop forever
            std::string key = UITrim(text.substr(keyStart, i - keyStart));
            i++; // skip ':'
            size_t valueStart = i;
            while (i < text.size() && text[i] != ';')
                i++;
            std::string value = UITrim(text.substr(valueStart, i - valueStart));
            if (!key.empty())
                declarations[key] = value;
        }
        return declarations;
    }

    inline std::vector<std::string> UISplitClasses(const std::string& value) {
        std::vector<std::string> classes;
        std::string current;
        for (char c : value) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!current.empty()) { classes.push_back(current); current.clear(); }
            } else {
                current += c;
            }
        }
        if (!current.empty())
            classes.push_back(current);
        return classes;
    }

}
