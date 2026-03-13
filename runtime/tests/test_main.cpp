/// GoogleTest entry point for the UzEngine runtime test suite.
/// Initialises the logging system before any test runs so test output
/// captures engine diagnostics alongside GoogleTest results.
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include "log.h"

// Force-reference all log categories so the linker pulls them from the static
// library. Without this, categories only used by engine .cpp files (but not
// directly by test .cpp files) get discarded by the linker.
UZ_DECLARE_LOG_CATEGORY(Renderer);
UZ_DECLARE_LOG_CATEGORY(Asset);
static volatile const void* kForceLogCategories[] = {
    &LogRenderer,
    &LogAsset,
};

namespace fs = std::filesystem;

/// Returns a temp directory for test artifacts, cleaned up on exit.
static fs::path GetTestTempDir() {
    fs::path tmp = fs::temp_directory_path() / "uzengine_test";
    fs::create_directories(tmp);
    return tmp;
}

int main(int argc, char** argv) {
    const auto tmpDir = GetTestTempDir();
    uz::LogInit((tmpDir / "uzengine.log").string().c_str());
    uz::LogSetLevel(uz::LogLevel::Error);
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();

    // Cleanup temp directory
    std::error_code ec;
    fs::remove_all(tmpDir, ec);

    return result;
}
