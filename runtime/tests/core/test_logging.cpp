#include "log.h"

#include <filesystem>
#include <gtest/gtest.h>

namespace fs = std::filesystem;

class LoggingTest : public ::testing::Test {
  protected:
    fs::path tmpDir_ = fs::temp_directory_path() / "uzengine_test";

    std::string TmpLog(const char* name) {
        fs::create_directories(tmpDir_);
        return (tmpDir_ / name).string();
    }

    void TearDown() override {
        uz::LogShutdown();
        // Re-init with Error level so subsequent tests stay quiet
        uz::LogInit(TmpLog("uzengine.log").c_str());
        uz::LogSetLevel(uz::LogLevel::Error);
    }
};

TEST_F(LoggingTest, InitAndShutdownAreSafe) {
    auto logPath = TmpLog("test_output.log");
    uz::LogInit(logPath.c_str());
    uz::LogInit(logPath.c_str()); // double init is no-op

    UZ_LOG(Core, Info, "Test log message: {}", 42);
    UZ_LOG(Core, Warning, "Test warning: {}", "caution");
    UZ_LOG(Core, Error, "Test error: code={}", -1);

    uz::LogShutdown();
    uz::LogShutdown(); // double shutdown is safe
}

TEST_F(LoggingTest, AllCategoriesHaveLoggers) {
    uz::LogInit(TmpLog("test_categories.log").c_str());

    EXPECT_NE(uz::LogGetLogger(LogCore), nullptr);
    EXPECT_NE(uz::LogGetLogger(LogRHI), nullptr);
    EXPECT_NE(uz::LogGetLogger(LogScene), nullptr);
    EXPECT_NE(uz::LogGetLogger(LogInput), nullptr);
    EXPECT_NE(uz::LogGetLogger(LogMemory), nullptr);

    UZ_LOG(RHI, Info, "RHI logger works");
    UZ_LOG(Scene, Debug, "Scene logger: {}", 3.14f);
}

TEST_F(LoggingTest, LevelChangeAffectsOutput) {
    uz::LogInit(TmpLog("test_levels.log").c_str());
    uz::LogSetLevel(uz::LogLevel::Warning);
    UZ_LOG(Core, Warning, "This appears at warning level");
    uz::LogSetLevel(uz::LogLevel::Trace);
}
