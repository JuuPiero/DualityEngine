#pragma once

#include <string>
#include <vector>

namespace Duality::Js {

    // Minimal Source Map v3 reader (the "mappings" VLQ format esbuild emits), enough to turn
    // a QuickJS stack position in the bundled JS back into the TypeScript file:line:col.
    class SourceMap {
    public:
        bool Parse(const std::string& json);
        void Clear();
        bool Empty() const { return m_Lines.empty(); }

        // 1-based generated line/column in, 1-based source line/column out.
        bool Lookup(int generatedLine, int generatedColumn, std::string& outSource, int& outLine, int& outColumn) const;

    private:
        struct Segment {
            int GeneratedColumn; // 0-based
            int Source;
            int Line;            // 0-based
            int Column;          // 0-based
        };
        std::vector<std::string> m_Sources;
        std::vector<std::vector<Segment>> m_Lines;
    };

}
