#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include "uz_log.h"

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
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();

    // Cleanup temp directory
    std::error_code ec;
    fs::remove_all(tmpDir, ec);

    return result;
}
