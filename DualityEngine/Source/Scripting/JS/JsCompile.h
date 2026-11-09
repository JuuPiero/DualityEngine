#pragma once

#include <string>
#include <vector>

namespace Duality::Js {

    // Compiles a classic-script JS bundle to QuickJS bytecode (JS_WriteObject). Depends on
    // QuickJS only, so the tiny DualityJsc command-line tool can link it without the engine.
    // Line/column debug info is kept so runtime errors still map back to TypeScript.
    bool CompileScriptToBytecode(const std::string& source, const std::string& fileName,
                                 std::vector<unsigned char>& outBytecode, std::string* outError);

}
