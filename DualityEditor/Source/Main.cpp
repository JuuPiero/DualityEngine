#include <algorithm>
#include <cstdlib>
#include <string>

#include "DualityEditor/Application.h"

// DualityEditor [--project <path/to/Game.dproj>] [--editor-test <script.txt>] [--size <W>x<H>]
//
//   --project      open this project instead of ./SampleProject
//   --editor-test  run an editor automation script (see EditorAutomation.h); the window is
//                  created without taking focus and the process exits with the number of
//                  failed checks when the script ends
//   --size         initial window size (default 1280x800)
int main(int argc, char** argv) {
    Duality::ApplicationOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--project" && hasValue) {
            options.ProjectPath = argv[++i];
        } else if (arg == "--editor-test" && hasValue) {
            options.TestScript = argv[++i];
        } else if (arg == "--size" && hasValue) {
            const std::string size = argv[++i];
            const size_t x = size.find('x');
            if (x != std::string::npos) {
                options.Width = std::max(640, std::atoi(size.substr(0, x).c_str()));
                options.Height = std::max(480, std::atoi(size.substr(x + 1).c_str()));
            }
        }
    }
    Duality::Application app(options);
    return app.Run();
}
