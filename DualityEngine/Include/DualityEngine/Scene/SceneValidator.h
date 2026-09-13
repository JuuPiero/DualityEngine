#pragma once

#include <string>
#include <vector>

namespace Duality {

    class Scene;

    enum class SceneValidationSeverity {
        Warning,
        Error
    };

    struct SceneValidationIssue {
        SceneValidationSeverity Severity = SceneValidationSeverity::Warning;
        std::string EntityName;
        std::string Message;
    };

    struct SceneValidationResult {
        std::vector<SceneValidationIssue> Issues;

        bool HasErrors() const;
    };

    // Fast, allocation-light checks that are safe to run before entering Play
    // mode or cooking a scene. This deliberately validates authored data only;
    // it cannot prove arbitrary native C++ script logic is memory-safe.
    class SceneValidator {
    public:
        static SceneValidationResult Validate(const Scene& scene);
    };

}
