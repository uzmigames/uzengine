/// Unit tests for the Config system — JSON file loading, CLI argument overrides,
/// dotted-path key resolution, type coercion, missing-key defaults, and error
/// handling for malformed or absent config files.
#include "config.h"
#include "log.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <string>

namespace fs = std::filesystem;

class ConfigTest : public ::testing::Test {
  protected:
    void SetUp() override {
        auto tmp = fs::temp_directory_path() / "uzengine_test";
        fs::create_directories(tmp);
        uz::LogInit((tmp / "uzengine.log").string().c_str());
        uz::LogSetLevel(uz::LogLevel::Error);
    }
    void TearDown() override {
        uz::LogShutdown();
        // Re-init with Error level so subsequent tests stay quiet
        auto tmp = fs::temp_directory_path() / "uzengine_test";
        fs::create_directories(tmp);
        uz::LogInit((tmp / "uzengine.log").string().c_str());
        uz::LogSetLevel(uz::LogLevel::Error);
    }
};

TEST_F(ConfigTest, LoadFromStringParsesCorrectly) {
    uz::Config cfg;
    ASSERT_TRUE(cfg.LoadFromString(R"({
        "name": "TestProject",
        "version": "1.0",
        "render": { "backend": "dx12", "vsync": true }
    })"));
    EXPECT_EQ(cfg.GetString("name"), "TestProject");
    EXPECT_EQ(cfg.GetString("version"), "1.0");
    EXPECT_TRUE(cfg.GetBool("render.vsync"));
    EXPECT_EQ(cfg.GetString("render.backend"), "dx12");
}

TEST_F(ConfigTest, DottedPathResolvesNestedAndArrays) {
    uz::Config cfg;
    cfg.LoadFromString(R"({ "a": { "b": { "c": 42 } }, "arr": [10, 20, 30] })");
    EXPECT_EQ(cfg.GetInt("a.b.c"), 42);

    auto* val = cfg.Resolve("arr.1");
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(val->get<int>(), 20);

    EXPECT_FALSE(cfg.Has("nonexistent"));
    EXPECT_EQ(cfg.GetInt("nonexistent", -1), -1);
    EXPECT_EQ(cfg.GetString("nonexistent", "fallback"), "fallback");
    EXPECT_FLOAT_EQ(cfg.GetFloat("nonexistent", 3.14f), 3.14f);
}

TEST_F(ConfigTest, CLIOverridesApplyCorrectly) {
    uz::Config cfg;
    cfg.LoadFromString(R"({ "name": "Original", "debug": false })");

    const char* argv[] = {"engine", "--name=Overridden", "--debug=true", "--extra", "hello"};
    cfg.ApplyCLIOverrides(5, argv);

    EXPECT_EQ(cfg.GetString("name"), "Overridden");
    EXPECT_TRUE(cfg.GetBool("debug"));
    EXPECT_EQ(cfg.GetString("extra"), "hello");
}

TEST_F(ConfigTest, EngineDefaultsAreSet) {
    uz::Config cfg;
    uz::ConfigSetEngineDefaults(cfg);
    EXPECT_EQ(cfg.GetString("render.backend"), "dx12");
    EXPECT_EQ(cfg.GetInt("window.width"), 1280);
    EXPECT_EQ(cfg.GetInt("window.height"), 720);
    EXPECT_TRUE(cfg.GetBool("render.vsync"));
}

TEST_F(ConfigTest, InvalidJsonReturnsFalse) {
    uz::Config cfg;
    EXPECT_FALSE(cfg.LoadFromString("{ broken json }}}"));
}
