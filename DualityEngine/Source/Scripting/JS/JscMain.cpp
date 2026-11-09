// DualityJsc -- compiles a bundled TypeScript game script (classic-script JS produced by
// Tools/TypeScript/build-scripts.mjs) to QuickJS bytecode for the 3DS build:
//
//   DualityJsc <input.js> <output.jsbc>
//
// The bytecode format is tied to the exact vendored QuickJS-ng version (Vendor/quickjs-ng), so
// this tool is always built from the same sources the 3DS player links.

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "JsCompile.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: DualityJsc <input.js> <output.jsbc>\n");
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::fprintf(stderr, "DualityJsc: cannot read %s\n", argv[1]);
        return 1;
    }
    std::stringstream buffer;
    buffer << input.rdbuf();
    const std::string source = buffer.str();

    std::string fileName = argv[1];
    const size_t slash = fileName.find_last_of("/\\");
    if (slash != std::string::npos)
        fileName = fileName.substr(slash + 1);

    std::vector<unsigned char> bytecode;
    std::string error;
    if (!Duality::Js::CompileScriptToBytecode(source, fileName, bytecode, &error)) {
        std::fprintf(stderr, "DualityJsc: %s\n", error.c_str());
        return 1;
    }
    std::ofstream output(argv[2], std::ios::binary);
    if (!output) {
        std::fprintf(stderr, "DualityJsc: cannot write %s\n", argv[2]);
        return 1;
    }
    output.write(reinterpret_cast<const char*>(bytecode.data()), static_cast<std::streamsize>(bytecode.size()));
    std::printf("DualityJsc: %s (%zu bytes JS) -> %s (%zu bytes bytecode)\n", argv[1], source.size(), argv[2], bytecode.size());
    return output ? 0 : 1;
}
