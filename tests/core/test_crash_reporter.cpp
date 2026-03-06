// test_crash_reporter.cpp -- Tests for CrashReporter.
//
// We do NOT test actual signal-induced crashes (EXPECT_DEATH with real SIGSEGV)
// because those are fragile in CI and interact badly with gtest's own signal
// handling. Instead we test the public API surface: install/uninstall lifecycle,
// output directory management, manual report writing, and file content validation.

#include <gtest/gtest.h>
#include "engine/core/crash_reporter.h"
#include "engine/core/platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif

namespace pal = uz::platform;

// ============================================================================
// Helpers -- temp directory management
// ============================================================================

namespace {

// RAII temp directory: creates a unique dir under /tmp, removes it recursively
// on destruction.
struct TempDir {
    std::string path;

    TempDir() {
#ifdef _WIN32
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        std::string tmpl = std::string(tmp) + "uz_crash_test_XXXXXX";
        if (_mktemp_s(tmpl.data(), tmpl.size() + 1) == 0) {
            if (CreateDirectoryA(tmpl.c_str(), nullptr)) {
                path = tmpl;
            }
        }
#else
        std::string tmpl = "/tmp/uz_crash_test_XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        char* result = mkdtemp(buf.data());
        if (result) {
            path = result;
        }
#endif
    }

    ~TempDir() {
        if (!path.empty()) {
#ifdef _WIN32
            std::string cmd = "rmdir /s /q \"" + path + "\"";
#else
            std::string cmd = "rm -rf \"" + path + "\"";
#endif
            (void)system(cmd.c_str());
        }
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

// Read an entire file into a std::string using uz::platform.
std::string read_file(const char* path) {
    size_t size = 0;
    char* data = pal::file_read_all(path, &size);
    if (!data) return {};
    std::string content(data, size);
    std::free(data);
    return content;
}

} // anonymous namespace

// ============================================================================
// Test fixture -- ensures handlers are uninstalled after each test
// ============================================================================

class CrashReporterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure clean state: uninstall any leftover handlers.
        uz::CrashReporter::uninstall();
    }

    void TearDown() override {
        // Always uninstall after each test to avoid polluting other tests.
        uz::CrashReporter::uninstall();
    }
};

// ============================================================================
// Install / Uninstall lifecycle tests
// ============================================================================

TEST_F(CrashReporterTest, InstallSetsInstalledTrue) {
    EXPECT_FALSE(uz::CrashReporter::is_installed());
    uz::CrashReporter::install();
    EXPECT_TRUE(uz::CrashReporter::is_installed());
}

TEST_F(CrashReporterTest, UninstallSetsInstalledFalse) {
    uz::CrashReporter::install();
    EXPECT_TRUE(uz::CrashReporter::is_installed());
    uz::CrashReporter::uninstall();
    EXPECT_FALSE(uz::CrashReporter::is_installed());
}

TEST_F(CrashReporterTest, InstallTwiceIsIdempotent) {
    // Installing twice should not crash or leave handlers in a bad state.
    uz::CrashReporter::install();
    EXPECT_TRUE(uz::CrashReporter::is_installed());
    uz::CrashReporter::install();
    EXPECT_TRUE(uz::CrashReporter::is_installed());
    // Uninstall should still work cleanly.
    uz::CrashReporter::uninstall();
    EXPECT_FALSE(uz::CrashReporter::is_installed());
}

TEST_F(CrashReporterTest, UninstallWithoutInstallDoesNotCrash) {
    // Should be a safe no-op.
    EXPECT_FALSE(uz::CrashReporter::is_installed());
    uz::CrashReporter::uninstall();
    EXPECT_FALSE(uz::CrashReporter::is_installed());
}

TEST_F(CrashReporterTest, InstallUninstallInstallRoundtrip) {
    uz::CrashReporter::install();
    EXPECT_TRUE(uz::CrashReporter::is_installed());
    uz::CrashReporter::uninstall();
    EXPECT_FALSE(uz::CrashReporter::is_installed());
    uz::CrashReporter::install();
    EXPECT_TRUE(uz::CrashReporter::is_installed());
}

// ============================================================================
// Output directory management
// ============================================================================

TEST_F(CrashReporterTest, SetOutputDirUpdatesDir) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());

    uz::CrashReporter::set_output_dir(tmpdir.path.c_str());
    EXPECT_STREQ(uz::CrashReporter::output_dir(), tmpdir.path.c_str());
}

TEST_F(CrashReporterTest, InstallWithOutputDirSetsDir) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());

    uz::CrashReporter::install(tmpdir.path.c_str());
    EXPECT_STREQ(uz::CrashReporter::output_dir(), tmpdir.path.c_str());
    EXPECT_TRUE(uz::CrashReporter::is_installed());
}

TEST_F(CrashReporterTest, SetOutputDirNullIsIgnored) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());

    uz::CrashReporter::set_output_dir(tmpdir.path.c_str());
    const char* before = uz::CrashReporter::output_dir();
    EXPECT_STREQ(before, tmpdir.path.c_str());

    // Setting nullptr should not change the directory.
    uz::CrashReporter::set_output_dir(nullptr);
    EXPECT_STREQ(uz::CrashReporter::output_dir(), tmpdir.path.c_str());
}

TEST_F(CrashReporterTest, DefaultOutputDirIsNonEmpty) {
    // The default output dir should be set to something reasonable.
    const char* dir = uz::CrashReporter::output_dir();
    ASSERT_NE(dir, nullptr);
    EXPECT_GT(std::strlen(dir), 0u);
}

// ============================================================================
// Manual report writing
// ============================================================================

TEST_F(CrashReporterTest, WriteReportReturnsNonEmptyPath) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());
    uz::CrashReporter::set_output_dir(tmpdir.path.c_str());

    const std::string path = uz::CrashReporter::write_report("test reason", "test details");
    EXPECT_FALSE(path.empty());
}

TEST_F(CrashReporterTest, WriteReportCreatesFileOnDisk) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());
    uz::CrashReporter::set_output_dir(tmpdir.path.c_str());

    const std::string path = uz::CrashReporter::write_report("test reason");
    ASSERT_FALSE(path.empty());
    EXPECT_TRUE(pal::path_exists(path.c_str()));
}

TEST_F(CrashReporterTest, WriteReportContainsReason) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());
    uz::CrashReporter::set_output_dir(tmpdir.path.c_str());

    const std::string path = uz::CrashReporter::write_report("test reason", "test details");
    ASSERT_FALSE(path.empty());

    const std::string content = read_file(path.c_str());
    EXPECT_NE(content.find("test reason"), std::string::npos)
        << "Report should contain the reason string. Content:\n" << content;
}

TEST_F(CrashReporterTest, WriteReportContainsCrashSection) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());
    uz::CrashReporter::set_output_dir(tmpdir.path.c_str());

    const std::string path = uz::CrashReporter::write_report("segfault");
    ASSERT_FALSE(path.empty());

    const std::string content = read_file(path.c_str());
    EXPECT_NE(content.find("[crash]"), std::string::npos)
        << "Report should contain [crash] section header. Content:\n" << content;
}

TEST_F(CrashReporterTest, WriteReportContainsEngineSection) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());
    uz::CrashReporter::set_output_dir(tmpdir.path.c_str());

    const std::string path = uz::CrashReporter::write_report("segfault");
    ASSERT_FALSE(path.empty());

    const std::string content = read_file(path.c_str());
    EXPECT_NE(content.find("[engine]"), std::string::npos)
        << "Report should contain [engine] section header. Content:\n" << content;
}

TEST_F(CrashReporterTest, WriteReportContainsStackSection) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());
    uz::CrashReporter::set_output_dir(tmpdir.path.c_str());

    const std::string path = uz::CrashReporter::write_report("test");
    ASSERT_FALSE(path.empty());

    const std::string content = read_file(path.c_str());
    EXPECT_NE(content.find("[stack]"), std::string::npos)
        << "Report should contain [stack] section header. Content:\n" << content;
}

TEST_F(CrashReporterTest, WriteReportContainsEngineVersion) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());
    uz::CrashReporter::set_output_dir(tmpdir.path.c_str());

    const std::string path = uz::CrashReporter::write_report("test");
    ASSERT_FALSE(path.empty());

    const std::string content = read_file(path.c_str());
    EXPECT_NE(content.find("version = 0.3.0"), std::string::npos)
        << "Report should contain engine version. Content:\n" << content;
}

TEST_F(CrashReporterTest, WriteReportContainsBuildType) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());
    uz::CrashReporter::set_output_dir(tmpdir.path.c_str());

    const std::string path = uz::CrashReporter::write_report("test");
    ASSERT_FALSE(path.empty());

    const std::string content = read_file(path.c_str());
    // In test builds, UZENGINE_DEBUG=1 is defined, so build should be "Debug".
    EXPECT_NE(content.find("build = Debug"), std::string::npos)
        << "Report should contain build type. Content:\n" << content;
}

TEST_F(CrashReporterTest, WriteReportContainsTimestamp) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());
    uz::CrashReporter::set_output_dir(tmpdir.path.c_str());

    const std::string path = uz::CrashReporter::write_report("test");
    ASSERT_FALSE(path.empty());

    const std::string content = read_file(path.c_str());
    EXPECT_NE(content.find("timestamp = "), std::string::npos)
        << "Report should contain a timestamp. Content:\n" << content;
}

TEST_F(CrashReporterTest, WriteReportContainsDetails) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());
    uz::CrashReporter::set_output_dir(tmpdir.path.c_str());

    const std::string path = uz::CrashReporter::write_report("crash", "memory corruption at 0xDEAD");
    ASSERT_FALSE(path.empty());

    const std::string content = read_file(path.c_str());
    EXPECT_NE(content.find("memory corruption at 0xDEAD"), std::string::npos)
        << "Report should contain the details string. Content:\n" << content;
}

TEST_F(CrashReporterTest, WriteReportWithNullDetailsSucceeds) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());
    uz::CrashReporter::set_output_dir(tmpdir.path.c_str());

    const std::string path = uz::CrashReporter::write_report("null_details", nullptr);
    EXPECT_FALSE(path.empty());
    EXPECT_TRUE(pal::path_exists(path.c_str()));

    // Should NOT contain "details =" line since details was null.
    const std::string content = read_file(path.c_str());
    EXPECT_EQ(content.find("details = "), std::string::npos)
        << "Report with null details should not have a details line. Content:\n" << content;
}

TEST_F(CrashReporterTest, WriteReportWithNullReasonUsesUnknown) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());
    uz::CrashReporter::set_output_dir(tmpdir.path.c_str());

    const std::string path = uz::CrashReporter::write_report(nullptr);
    ASSERT_FALSE(path.empty());

    const std::string content = read_file(path.c_str());
    EXPECT_NE(content.find("signal = unknown"), std::string::npos)
        << "Report with null reason should use 'unknown'. Content:\n" << content;
}

// ============================================================================
// Last report path tracking
// ============================================================================

TEST_F(CrashReporterTest, LastReportPathMatchesWriteReport) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());
    uz::CrashReporter::set_output_dir(tmpdir.path.c_str());

    const std::string path = uz::CrashReporter::write_report("test");
    ASSERT_FALSE(path.empty());

    EXPECT_EQ(uz::CrashReporter::last_report_path(), path);
}

TEST_F(CrashReporterTest, LastReportPathUpdatesOnSubsequentWrite) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());
    uz::CrashReporter::set_output_dir(tmpdir.path.c_str());

    const std::string path1 = uz::CrashReporter::write_report("first");
    ASSERT_FALSE(path1.empty());

    // Sleep briefly so the timestamp-based filename differs.
    pal::thread_sleep_ms(1100);

    const std::string path2 = uz::CrashReporter::write_report("second");
    ASSERT_FALSE(path2.empty());

    EXPECT_EQ(uz::CrashReporter::last_report_path(), path2);
}

// ============================================================================
// Pre-crash callback
// ============================================================================

TEST_F(CrashReporterTest, SetPreCrashCallbackDoesNotCrash) {
    // Setting a callback should not crash even when handlers are not installed.
    uz::CrashReporter::set_pre_crash_callback([]() {
        // No-op callback. We cannot easily verify it's called without
        // triggering a real signal, which we avoid in tests.
    });

    // Setting nullptr should also be safe.
    uz::CrashReporter::set_pre_crash_callback(nullptr);
}

TEST_F(CrashReporterTest, SetPreCrashCallbackStoresCallback) {
    // Verify indirectly: set a callback, install, uninstall, set null.
    // None of these operations should crash.
    uz::CrashReporter::set_pre_crash_callback([]() {});
    uz::CrashReporter::install();
    EXPECT_TRUE(uz::CrashReporter::is_installed());
    uz::CrashReporter::uninstall();
    uz::CrashReporter::set_pre_crash_callback(nullptr);
}

// ============================================================================
// Directory creation
// ============================================================================

TEST_F(CrashReporterTest, WriteReportCreatesOutputDirIfNeeded) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());

    // Point to a subdirectory that does not exist yet.
    const std::string subdir = tmpdir.path + "/nested/crash_output";
    uz::CrashReporter::set_output_dir(subdir.c_str());

    const std::string path = uz::CrashReporter::write_report("test_mkdir");
    ASSERT_FALSE(path.empty());
    EXPECT_TRUE(pal::path_exists(path.c_str()));
    EXPECT_TRUE(pal::path_exists(subdir.c_str()));
}

// ============================================================================
// Report filename format
// ============================================================================

TEST_F(CrashReporterTest, WriteReportFilenameStartsWithCrash) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());
    uz::CrashReporter::set_output_dir(tmpdir.path.c_str());

    const std::string path = uz::CrashReporter::write_report("test");
    ASSERT_FALSE(path.empty());

    // The filename should start with "crash_" after the directory.
    const size_t last_slash = path.rfind('/');
    ASSERT_NE(last_slash, std::string::npos);
    const std::string filename = path.substr(last_slash + 1);
    EXPECT_EQ(filename.substr(0, 6), "crash_")
        << "Filename should start with 'crash_'. Got: " << filename;
}

TEST_F(CrashReporterTest, WriteReportFilenameEndsWith_txt) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());
    uz::CrashReporter::set_output_dir(tmpdir.path.c_str());

    const std::string path = uz::CrashReporter::write_report("test");
    ASSERT_FALSE(path.empty());

    // The filename should end with ".txt".
    const size_t ext_pos = path.rfind(".txt");
    ASSERT_NE(ext_pos, std::string::npos);
    EXPECT_EQ(ext_pos + 4, path.size());
}

// ============================================================================
// Robustness
// ============================================================================

TEST_F(CrashReporterTest, MultipleWriteReportsSucceed) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());
    uz::CrashReporter::set_output_dir(tmpdir.path.c_str());

    // Write several reports in quick succession (same second -> same filename,
    // but write_report should still succeed by overwriting or appending timestamp).
    for (int i = 0; i < 5; ++i) {
        const std::string path = uz::CrashReporter::write_report("batch_test");
        EXPECT_FALSE(path.empty()) << "Report #" << i << " should succeed";
        EXPECT_TRUE(pal::path_exists(path.c_str()))
            << "Report #" << i << " should exist on disk";
    }
}

TEST_F(CrashReporterTest, ReportFileHasNonZeroSize) {
    TempDir tmpdir;
    ASSERT_FALSE(tmpdir.path.empty());
    uz::CrashReporter::set_output_dir(tmpdir.path.c_str());

    const std::string path = uz::CrashReporter::write_report("size_test", "some details");
    ASSERT_FALSE(path.empty());

    const uint64_t size = pal::file_size(path.c_str());
    EXPECT_GT(size, 0u) << "Crash report file should not be empty";
    // A reasonable minimum: [crash]\n + signal line + [engine]\n + version line >= 50 bytes
    EXPECT_GT(size, 50u) << "Crash report seems too small";
}

TEST_F(CrashReporterTest, InstallWithNullOutputDirKeepsDefault) {
    // Get the current default before installing.
    const std::string default_dir = uz::CrashReporter::output_dir();
    EXPECT_FALSE(default_dir.empty());

    // Install with nullptr should not change the directory.
    uz::CrashReporter::install(nullptr);
    EXPECT_STREQ(uz::CrashReporter::output_dir(), default_dir.c_str());
    EXPECT_TRUE(uz::CrashReporter::is_installed());
}
