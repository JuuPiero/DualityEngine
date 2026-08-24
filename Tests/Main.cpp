// Runs every TEST_CASE registered across the Tests/ .cpp files (see TestFramework.h) and
// reports a summary -- exit code 0 only if every case passed, non-zero otherwise, so CI
// can gate on it directly (`run-tests.bat` / a GitHub Actions step just checks $LASTEXITCODE).
#include <cstdio>

#include "DualityEngine/Reflection/Reflection.h"
#include "TestFramework.h"

int main() {
    using namespace Duality::Test;

    // Required before any test touches serialization (SceneSerializer/PrefabSerializer
    // both walk TypeRegistry::All()) -- every real entry point (DualityPlayer,
    // DualityPlayerDesktop, the Editor) calls this once at startup too.
    Duality::RegisterBuiltinComponents();

    int passed = 0, failed = 0;
    for (auto& test : AllTests()) {
        SoftPassCount() = 0;
        SoftFailCount() = 0;
        std::printf("[ RUN  ] %s\n", test.Name.c_str());
        try {
            test.Run();
            if (SoftFailCount() == 0) {
                std::printf("[ PASS ] %s", test.Name.c_str());
                if (SoftPassCount() > 0)
                    std::printf(" (%d checks)", SoftPassCount());
                std::printf("\n");
                passed++;
            } else {
                std::printf("[ FAIL ] %s (%d/%d checks failed)\n", test.Name.c_str(), SoftFailCount(), SoftPassCount() + SoftFailCount());
                failed++;
            }
        } catch (const CheckFailure& failure) {
            std::printf("[ FAIL ] %s -- %s\n", test.Name.c_str(), failure.Message.c_str());
            failed++;
        } catch (const std::exception& e) {
            std::printf("[ FAIL ] %s -- unhandled exception: %s\n", test.Name.c_str(), e.what());
            failed++;
        }
    }

    std::printf("\n%d passed, %d failed, %d total\n", passed, failed, passed + failed);
    return failed == 0 ? 0 : 1;
}
