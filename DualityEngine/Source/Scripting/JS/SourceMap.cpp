#include "SourceMap.h"

#include <nlohmann/json.hpp>

namespace Duality::Js {

    namespace {
        int Base64Value(char c) {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
        }

        // Decodes one base64 VLQ value starting at `i`; advances `i`. False on malformed input.
        bool DecodeVlq(const std::string& text, size_t& i, int& out) {
            int result = 0;
            int shift = 0;
            while (i < text.size()) {
                int digit = Base64Value(text[i++]);
                if (digit < 0)
                    return false;
                const bool more = (digit & 32) != 0;
                result += (digit & 31) << shift;
                shift += 5;
                if (!more) {
                    const bool negative = (result & 1) != 0;
                    result >>= 1;
                    out = negative ? -result : result;
                    return true;
                }
                if (shift > 30)
                    return false;
            }
            return false;
        }
    }

    void SourceMap::Clear() {
        m_Sources.clear();
        m_Lines.clear();
    }

    bool SourceMap::Parse(const std::string& jsonText) {
        Clear();
        nlohmann::json root = nlohmann::json::parse(jsonText, nullptr, false);
        if (root.is_discarded() || !root.is_object() || !root.contains("mappings"))
            return false;
        if (root.contains("sources") && root["sources"].is_array())
            for (const auto& source : root["sources"])
                m_Sources.push_back(source.is_string() ? source.get<std::string>() : std::string());

        const std::string mappings = root["mappings"].is_string() ? root["mappings"].get<std::string>() : std::string();
        int source = 0, line = 0, column = 0;
        m_Lines.emplace_back();
        size_t i = 0;
        int generatedColumn = 0;
        while (i < mappings.size()) {
            const char c = mappings[i];
            if (c == ';') {
                m_Lines.emplace_back();
                generatedColumn = 0;
                ++i;
                continue;
            }
            if (c == ',') {
                ++i;
                continue;
            }
            int fields[5] = { 0, 0, 0, 0, 0 };
            int count = 0;
            while (i < mappings.size() && mappings[i] != ',' && mappings[i] != ';' && count < 5) {
                if (!DecodeVlq(mappings, i, fields[count]))
                    return false;
                ++count;
            }
            while (i < mappings.size() && mappings[i] != ',' && mappings[i] != ';')
                ++i; // tolerate unexpected extra fields
            generatedColumn += fields[0];
            if (count >= 4) {
                source += fields[1];
                line += fields[2];
                column += fields[3];
                m_Lines.back().push_back({ generatedColumn, source, line, column });
            }
        }
        return true;
    }

    bool SourceMap::Lookup(int generatedLine, int generatedColumn, std::string& outSource, int& outLine, int& outColumn) const {
        if (generatedLine < 1 || generatedLine > static_cast<int>(m_Lines.size()))
            return false;
        const auto& segments = m_Lines[generatedLine - 1];
        if (segments.empty())
            return false;
        const int column0 = generatedColumn > 0 ? generatedColumn - 1 : 0;
        const Segment* best = &segments.front();
        for (const Segment& segment : segments) {
            if (segment.GeneratedColumn <= column0)
                best = &segment;
            else
                break;
        }
        if (best->Source < 0 || best->Source >= static_cast<int>(m_Sources.size()))
            return false;
        outSource = m_Sources[best->Source];
        outLine = best->Line + 1;
        outColumn = best->Column + 1;
        return true;
    }

}
