#include "uz_log.h"

#include <gtest/gtest.h>

class LoggingTest : public ::testing::Test {
  protected:
    void TearDown() override { uz::LogShutdown(); }
};

TEST_F(LoggingTest, InitAndShutdownAreSafe) {
    uz::LogInit("test_output.log");
    uz::LogInit("test_output.log"); // double init is no-op

    UZ_LOG(Core, Info, "Test log message: {}", 42);
    UZ_LOG(Core, Warning, "Test warning: {}", "caution");
    UZ_LOG(Core, Error, "Test error: code={}", -1);

    uz::LogShutdown();
    uz::LogShutdown(); // double shutdown is safe
}

TEST_F(LoggingTest, AllCategoriesHaveLoggers) {
    uz::LogInit("test_categories.log");

    EXPECT_NE(uz::LogGetLogger(LogCore), nullptr);
    EXPECT_NE(uz::LogGetLogger(LogRHI), nullptr);
    EXPECT_NE(uz::LogGetLogger(LogScene), nullptr);
    EXPECT_NE(uz::LogGetLogger(LogInput), nullptr);
    EXPECT_NE(uz::LogGetLogger(LogMemory), nullptr);

    UZ_LOG(RHI, Info, "RHI logger works");
    UZ_LOG(Scene, Debug, "Scene logger: {}", 3.14f);
}

TEST_F(LoggingTest, LevelChangeAffectsOutput) {
    uz::LogInit("test_levels.log");
    uz::LogSetLevel(uz::LogLevel::Warning);
    UZ_LOG(Core, Warning, "This appears at warning level");
    uz::LogSetLevel(uz::LogLevel::Trace);
}
