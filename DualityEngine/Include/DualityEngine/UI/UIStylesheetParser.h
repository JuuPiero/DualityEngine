#pragma once

#include <string>

#include "DualityEngine/UI/UIMarkup.h"

namespace Duality {

    // Hand-rolled minimal CSS-like parser -- single simple selectors only (`Panel`, `.primary`,
    // `#playBtn`; no compound/descendant selectors, no pseudo-classes, no @-rules). Appends
    // every rule found to `outStylesheet` (in source order) so multiple <style> blocks/linked
    // files can all cascade into one combined stylesheet by calling this repeatedly. Malformed
    // rules are skipped individually rather than aborting the whole parse -- a typo in one rule
    // shouldn't silently blank out every other style in the document.
    class UIStylesheetParser {
    public:
        static void ParseInto(const std::string& source, UIStylesheet& outStylesheet);
    };

}
