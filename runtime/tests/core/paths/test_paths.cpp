/// Tests for uz_paths — exe-relative resource path resolution.

#include "uz_paths.h"
#include <gtest/gtest.h>
#include <filesystem>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// GetExeDir
// ─────────────────────────────────────────────────────────────────────────────

TEST(PathsTest, GetExeDirReturnsNonEmpty) {
    auto dir = uz::GetExeDir();
    EXPECT_FALSE(dir.empty());
}

TEST(PathsTest, GetExeDirExists) {
    auto dir = uz::GetExeDir();
    EXPECT_TRUE(fs::exists(dir));
    EXPECT_TRUE(fs::is_directory(dir));
}

TEST(PathsTest, GetExeDirContainsTestBinary) {
    auto dir = uz::GetExeDir();
    // The test binary itself should be in this directory
    bool found = false;
    for (auto& entry : fs::directory_iterator(dir)) {
        auto name = entry.path().filename().string();
        if (name.find("test_runtime") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "test_runtime binary not found in exe dir: " << dir.string();
}

TEST(PathsTest, GetExeDirIsCachedAndStable) {
    auto dir1 = uz::GetExeDir();
    auto dir2 = uz::GetExeDir();
    EXPECT_EQ(dir1, dir2);
}

TEST(PathsTest, GetExeDirIsNotCWD) {
    // GetExeDir should return the actual exe location, not CWD
    // (they might coincidentally be the same, but the path should be absolute)
    auto dir = uz::GetExeDir();
    EXPECT_TRUE(dir.is_absolute());
}

// ─────────────────────────────────────────────────────────────────────────────
// ResolveEngineResource
// ─────────────────────────────────────────────────────────────────────────────

TEST(PathsTest, ResolveRelativePath) {
    auto resolved = uz::ResolveEngineResource("dxcompiler.dll");
    EXPECT_TRUE(resolved.is_absolute());
    EXPECT_EQ(resolved.filename().string(), "dxcompiler.dll");
    EXPECT_EQ(resolved.parent_path(), uz::GetExeDir());
}

TEST(PathsTest, ResolveSubdirectoryPath) {
    auto resolved = uz::ResolveEngineResource("D3D12/D3D12Core.dll");
    EXPECT_TRUE(resolved.is_absolute());
    EXPECT_EQ(resolved.filename().string(), "D3D12Core.dll");
}

TEST(PathsTest, ResolveAbsolutePathUnchanged) {
#ifdef _WIN32
    auto resolved = uz::ResolveEngineResource("C:\\some\\absolute\\path.dll");
    EXPECT_EQ(resolved, fs::path("C:\\some\\absolute\\path.dll"));
#else
    auto resolved = uz::ResolveEngineResource("/some/absolute/path.so");
    EXPECT_EQ(resolved, fs::path("/some/absolute/path.so"));
#endif
}

TEST(PathsTest, ResolveEmptyPathReturnsExeDir) {
    auto resolved = uz::ResolveEngineResource("");
    // Empty relative path resolves to exe dir itself
    EXPECT_EQ(resolved, uz::GetExeDir() / "");
}
