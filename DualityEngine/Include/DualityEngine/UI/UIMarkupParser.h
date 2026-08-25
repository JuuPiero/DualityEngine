#pragma once

#include <string>
#include <vector>

#include "DualityEngine/UI/UIMarkup.h"

namespace Duality {

    // Result of parsing one .uidoc source file's raw markup text -- everything needed to build
    // the final style cascade and element tree, before any <link href="..."> external
    // stylesheets (resolved by the caller, UIDocument.cpp, since only it knows the file's own
    // directory to resolve relative paths against) are merged in.
    struct UIMarkupParseResult {
        bool Success = false;
        std::string Error; // human-readable parse failure reason, only meaningful if !Success

        UIElementNode Root; // the <ui> root element's own children are the real UI tree
        std::vector<std::string> InlineStylesheetSources; // raw text of every <style>...</style> block, in document order
        std::vector<std::string> LinkedStylesheetPaths;   // every <link href="..."/> value, in document order
    };

    // Hand-rolled minimal XML/HTML-like parser -- not a general XML parser (no namespaces, no
    // DTD/CDATA, no entity references beyond the handful of literal characters below): just
    // enough to parse a .uidoc file's own small element vocabulary. Root element must be `<ui>`.
    // A `<style>...</style>` element's content is captured as raw text (not parsed as nested
    // tags, since CSS content itself uses `{`/`}`/`<`/`>`-adjacent characters a naive tag
    // scanner would misread) and does not appear in the returned tree -- see
    // InlineStylesheetSources. `<link href="...">` elements are likewise pulled out into
    // LinkedStylesheetPaths rather than left as regular tree nodes.
    class UIMarkupParser {
    public:
        static UIMarkupParseResult Parse(const std::string& source);
    };

}
