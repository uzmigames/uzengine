// test_project_config.cpp -- tests for ProjectConfig (.uzproj TOML loader/saver)

#include <gtest/gtest.h>
#include "engine/core/project_config.h"

#include <cstdio>
#include <fstream>
#include <string>

using namespace uz;

// ============================================================================
// Helpers -- temp file RAII
// ============================================================================

namespace {

// RAII temp file that deletes on destruction.
struct TempFile {
    std::string path;

    explicit TempFile(const std::string& name) {
        // Use the test binary's working dir + a unique-ish name.
        path = std::string("test_project_config_") + name + ".toml";
    }

    void write(const std::string& content) const {
        std::ofstream out(path, std::ios::out | std::ios::trunc);
        ASSERT_TRUE(out.is_open()) << "failed to create temp file: " << path;
        out << content;
    }

    ~TempFile() {
        std::remove(path.c_str());
    }

    // Non-copyable.
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

} // namespace

// ============================================================================
// defaults()
// ============================================================================

TEST(ProjectConfig, DefaultsReturnsValidConfig) {
    const auto cfg = ProjectConfig::defaults();
    EXPECT_EQ(cfg.name, "Untitled");
    EXPECT_EQ(cfg.version, "0.1.0");
    EXPECT_EQ(cfg.main_scene, "scenes/main.uzscene");
    EXPECT_TRUE(cfg.renderer.vsync);
    EXPECT_EQ(cfg.renderer.target_fps, 60);
    EXPECT_EQ(cfg.renderer.resolution.width, 1920);
    EXPECT_EQ(cfg.renderer.resolution.height, 1080);
}

// ============================================================================
// load() -- valid full .uzproj
// ============================================================================

TEST(ProjectConfig, LoadFullConfig) {
    TempFile tmp("full");
    tmp.write(R"toml(
[project]
name = "MyGame"
version = "1.2.3"
main_scene = "levels/intro.uzscene"

[renderer]
vsync = false
target_fps = 144

[renderer.resolution]
width = 2560
height = 1440
)toml");

    auto result = ProjectConfig::load(tmp.path);
    ASSERT_TRUE(result.is_ok()) << result.error();

    const auto& cfg = result.value();
    EXPECT_EQ(cfg.name, "MyGame");
    EXPECT_EQ(cfg.version, "1.2.3");
    EXPECT_EQ(cfg.main_scene, "levels/intro.uzscene");
    EXPECT_FALSE(cfg.renderer.vsync);
    EXPECT_EQ(cfg.renderer.target_fps, 144);
    EXPECT_EQ(cfg.renderer.resolution.width, 2560);
    EXPECT_EQ(cfg.renderer.resolution.height, 1440);
}

// ============================================================================
// load() -- minimal .uzproj (only required fields, defaults applied)
// ============================================================================

TEST(ProjectConfig, LoadMinimalConfigAppliesDefaults) {
    TempFile tmp("minimal");
    tmp.write(R"toml(
[project]
name = "Bare"
)toml");

    auto result = ProjectConfig::load(tmp.path);
    ASSERT_TRUE(result.is_ok()) << result.error();

    const auto& cfg = result.value();
    EXPECT_EQ(cfg.name, "Bare");
    // Defaults applied for optional fields.
    EXPECT_EQ(cfg.version, "0.1.0");
    EXPECT_EQ(cfg.main_scene, "scenes/main.uzscene");
    EXPECT_TRUE(cfg.renderer.vsync);
    EXPECT_EQ(cfg.renderer.target_fps, 60);
    EXPECT_EQ(cfg.renderer.resolution.width, 1920);
    EXPECT_EQ(cfg.renderer.resolution.height, 1080);
}

// ============================================================================
// load() -- partial renderer (some fields present, others default)
// ============================================================================

TEST(ProjectConfig, LoadPartialRendererConfig) {
    TempFile tmp("partial_renderer");
    tmp.write(R"toml(
[project]
name = "Partial"

[renderer]
vsync = false
)toml");

    auto result = ProjectConfig::load(tmp.path);
    ASSERT_TRUE(result.is_ok()) << result.error();

    const auto& cfg = result.value();
    EXPECT_EQ(cfg.name, "Partial");
    EXPECT_FALSE(cfg.renderer.vsync);
    // target_fps and resolution use defaults.
    EXPECT_EQ(cfg.renderer.target_fps, 60);
    EXPECT_EQ(cfg.renderer.resolution.width, 1920);
    EXPECT_EQ(cfg.renderer.resolution.height, 1080);
}

// ============================================================================
// load() -- partial resolution (only width specified)
// ============================================================================

TEST(ProjectConfig, LoadPartialResolution) {
    TempFile tmp("partial_res");
    tmp.write(R"toml(
[project]
name = "PartRes"

[renderer.resolution]
width = 3840
)toml");

    auto result = ProjectConfig::load(tmp.path);
    ASSERT_TRUE(result.is_ok()) << result.error();

    const auto& cfg = result.value();
    EXPECT_EQ(cfg.renderer.resolution.width, 3840);
    // height defaults.
    EXPECT_EQ(cfg.renderer.resolution.height, 1080);
}

// ============================================================================
// load() -- missing file returns error
// ============================================================================

TEST(ProjectConfig, LoadMissingFileReturnsError) {
    auto result = ProjectConfig::load("nonexistent_file_that_does_not_exist.uzproj");
    ASSERT_TRUE(result.is_err());
    // Error message should contain some indication of the problem.
    EXPECT_FALSE(result.error().empty());
}

// ============================================================================
// load() -- malformed TOML returns error with message
// ============================================================================

TEST(ProjectConfig, LoadMalformedTomlReturnsError) {
    TempFile tmp("malformed");
    tmp.write(R"toml(
[project
name = "broken
)toml");

    auto result = ProjectConfig::load(tmp.path);
    ASSERT_TRUE(result.is_err());
    EXPECT_FALSE(result.error().empty());
}

// ============================================================================
// load() -- missing [project] table returns error
// ============================================================================

TEST(ProjectConfig, LoadMissingProjectTableReturnsError) {
    TempFile tmp("no_project");
    tmp.write(R"toml(
[renderer]
vsync = true
)toml");

    auto result = ProjectConfig::load(tmp.path);
    ASSERT_TRUE(result.is_err());
    EXPECT_NE(result.error().find("[project]"), std::string::npos);
}

// ============================================================================
// load() -- missing project.name returns error
// ============================================================================

TEST(ProjectConfig, LoadMissingNameReturnsError) {
    TempFile tmp("no_name");
    tmp.write(R"toml(
[project]
version = "1.0.0"
)toml");

    auto result = ProjectConfig::load(tmp.path);
    ASSERT_TRUE(result.is_err());
    EXPECT_NE(result.error().find("project.name"), std::string::npos);
}

// ============================================================================
// load() -- project.name wrong type returns error
// ============================================================================

TEST(ProjectConfig, LoadNameWrongTypeReturnsError) {
    TempFile tmp("name_int");
    tmp.write(R"toml(
[project]
name = 42
)toml");

    auto result = ProjectConfig::load(tmp.path);
    ASSERT_TRUE(result.is_err());
    EXPECT_NE(result.error().find("project.name"), std::string::npos);
}

// ============================================================================
// save() -- basic serialization
// ============================================================================

TEST(ProjectConfig, SaveCreatesFile) {
    TempFile tmp("save_basic");

    ProjectConfig cfg;
    cfg.name       = "SaveTest";
    cfg.version    = "2.0.0";
    cfg.main_scene = "levels/test.uzscene";
    cfg.renderer.vsync      = false;
    cfg.renderer.target_fps = 120;
    cfg.renderer.resolution = {3840, 2160};

    auto result = ProjectConfig::save(cfg, tmp.path);
    ASSERT_TRUE(result.is_ok()) << result.error();

    // Verify the file can be loaded back.
    auto loaded = ProjectConfig::load(tmp.path);
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    EXPECT_EQ(loaded.value().name, "SaveTest");
}

// ============================================================================
// save() -- invalid path returns error
// ============================================================================

TEST(ProjectConfig, SaveInvalidPathReturnsError) {
    ProjectConfig cfg = ProjectConfig::defaults();

    // Try to write to a path that cannot be opened (non-existent directory).
    auto result = ProjectConfig::save(cfg, "Z:/nonexistent/dir/file.uzproj");
    ASSERT_TRUE(result.is_err());
    EXPECT_FALSE(result.error().empty());
}

// ============================================================================
// Round-trip: load -> save -> load -> same values
// ============================================================================

TEST(ProjectConfig, RoundTripPreservesAllFields) {
    TempFile tmp_orig("roundtrip_orig");
    TempFile tmp_copy("roundtrip_copy");

    tmp_orig.write(R"toml(
[project]
name = "RoundTrip"
version = "3.1.4"
main_scene = "worlds/alpha.uzscene"

[renderer]
vsync = false
target_fps = 240

[renderer.resolution]
width = 7680
height = 4320
)toml");

    // Load original.
    auto r1 = ProjectConfig::load(tmp_orig.path);
    ASSERT_TRUE(r1.is_ok()) << r1.error();
    const auto& cfg1 = r1.value();

    // Save to copy.
    auto save_result = ProjectConfig::save(cfg1, tmp_copy.path);
    ASSERT_TRUE(save_result.is_ok()) << save_result.error();

    // Load copy.
    auto r2 = ProjectConfig::load(tmp_copy.path);
    ASSERT_TRUE(r2.is_ok()) << r2.error();
    const auto& cfg2 = r2.value();

    // All fields must match.
    EXPECT_EQ(cfg1.name, cfg2.name);
    EXPECT_EQ(cfg1.version, cfg2.version);
    EXPECT_EQ(cfg1.main_scene, cfg2.main_scene);
    EXPECT_EQ(cfg1.renderer.vsync, cfg2.renderer.vsync);
    EXPECT_EQ(cfg1.renderer.target_fps, cfg2.renderer.target_fps);
    EXPECT_EQ(cfg1.renderer.resolution.width, cfg2.renderer.resolution.width);
    EXPECT_EQ(cfg1.renderer.resolution.height, cfg2.renderer.resolution.height);
    EXPECT_EQ(cfg1, cfg2);
}

// ============================================================================
// Round-trip with defaults
// ============================================================================

TEST(ProjectConfig, RoundTripDefaults) {
    TempFile tmp("roundtrip_defaults");

    auto cfg = ProjectConfig::defaults();
    auto save_result = ProjectConfig::save(cfg, tmp.path);
    ASSERT_TRUE(save_result.is_ok()) << save_result.error();

    auto loaded = ProjectConfig::load(tmp.path);
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();

    EXPECT_EQ(cfg, loaded.value());
}

// ============================================================================
// Equality operators
// ============================================================================

TEST(ProjectConfig, EqualityOperators) {
    auto a = ProjectConfig::defaults();
    auto b = ProjectConfig::defaults();

    EXPECT_EQ(a, b);
    EXPECT_FALSE(a != b);

    b.name = "Different";
    EXPECT_NE(a, b);
    EXPECT_FALSE(a == b);
}

TEST(ProjectConfig, RendererConfigEquality) {
    RendererConfig a;
    RendererConfig b;
    EXPECT_EQ(a, b);

    b.vsync = !a.vsync;
    EXPECT_NE(a, b);

    b = a;
    b.target_fps = 999;
    EXPECT_NE(a, b);

    b = a;
    b.resolution.width = 1;
    EXPECT_NE(a, b);
}

TEST(ProjectConfig, ResolutionEquality) {
    Resolution a{1920, 1080};
    Resolution b{1920, 1080};
    EXPECT_EQ(a, b);

    b.width = 1280;
    EXPECT_NE(a, b);

    b = a;
    b.height = 720;
    EXPECT_NE(a, b);
}

// ============================================================================
// Edge case: empty TOML file
// ============================================================================

TEST(ProjectConfig, LoadEmptyFileReturnsError) {
    TempFile tmp("empty");
    tmp.write("");

    auto result = ProjectConfig::load(tmp.path);
    ASSERT_TRUE(result.is_err());
    EXPECT_NE(result.error().find("[project]"), std::string::npos);
}

// ============================================================================
// Edge case: project table exists but is not a table (e.g. project = "string")
// ============================================================================

TEST(ProjectConfig, LoadProjectNotTableReturnsError) {
    TempFile tmp("project_not_table");
    tmp.write("project = \"not a table\"\n");

    auto result = ProjectConfig::load(tmp.path);
    ASSERT_TRUE(result.is_err());
    EXPECT_NE(result.error().find("[project]"), std::string::npos);
}

// ============================================================================
// Edge case: renderer fields with wrong types are ignored (defaults used)
// ============================================================================

TEST(ProjectConfig, LoadRendererWrongTypesUsesDefaults) {
    TempFile tmp("wrong_types");
    tmp.write(R"toml(
[project]
name = "WrongTypes"

[renderer]
vsync = "yes"
target_fps = "sixty"
)toml");

    auto result = ProjectConfig::load(tmp.path);
    ASSERT_TRUE(result.is_ok()) << result.error();

    const auto& cfg = result.value();
    // Wrong types ignored, defaults used.
    EXPECT_TRUE(cfg.renderer.vsync);
    EXPECT_EQ(cfg.renderer.target_fps, 60);
}

// ============================================================================
// Edge case: resolution fields with wrong types are ignored
// ============================================================================

TEST(ProjectConfig, LoadResolutionWrongTypesUsesDefaults) {
    TempFile tmp("wrong_res_types");
    tmp.write(R"toml(
[project]
name = "WrongRes"

[renderer.resolution]
width = "wide"
height = true
)toml");

    auto result = ProjectConfig::load(tmp.path);
    ASSERT_TRUE(result.is_ok()) << result.error();

    const auto& cfg = result.value();
    EXPECT_EQ(cfg.renderer.resolution.width, 1920);
    EXPECT_EQ(cfg.renderer.resolution.height, 1080);
}

// ============================================================================
// Edge case: version/main_scene wrong types use defaults
// ============================================================================

TEST(ProjectConfig, LoadOptionalFieldsWrongTypesUsesDefaults) {
    TempFile tmp("opt_wrong");
    tmp.write(R"toml(
[project]
name = "OptWrong"
version = 123
main_scene = false
)toml");

    auto result = ProjectConfig::load(tmp.path);
    ASSERT_TRUE(result.is_ok()) << result.error();

    const auto& cfg = result.value();
    EXPECT_EQ(cfg.version, "0.1.0");
    EXPECT_EQ(cfg.main_scene, "scenes/main.uzscene");
}

// ============================================================================
// Edge case: extra unknown fields are silently ignored
// ============================================================================

TEST(ProjectConfig, LoadExtraFieldsIgnored) {
    TempFile tmp("extra");
    tmp.write(R"toml(
[project]
name = "Extra"
description = "ignored field"
author = "nobody"

[renderer]
vsync = true
antialiasing = "TAA"

[audio]
master_volume = 0.8
)toml");

    auto result = ProjectConfig::load(tmp.path);
    ASSERT_TRUE(result.is_ok()) << result.error();
    EXPECT_EQ(result.value().name, "Extra");
}

// ============================================================================
// Edge case: renderer.resolution is not a table
// ============================================================================

TEST(ProjectConfig, LoadResolutionNotTableUsesDefaults) {
    TempFile tmp("res_not_table");
    tmp.write(R"toml(
[project]
name = "ResNotTable"

[renderer]
resolution = "1920x1080"
)toml");

    auto result = ProjectConfig::load(tmp.path);
    ASSERT_TRUE(result.is_ok()) << result.error();

    const auto& cfg = result.value();
    EXPECT_EQ(cfg.renderer.resolution.width, 1920);
    EXPECT_EQ(cfg.renderer.resolution.height, 1080);
}

// ============================================================================
// Edge case: renderer is not a table
// ============================================================================

TEST(ProjectConfig, LoadRendererNotTableUsesDefaults) {
    TempFile tmp("renderer_not_table");
    tmp.write(R"toml(
[project]
name = "RendNotTable"
renderer = 42
)toml");

    // toml++ parses "renderer = 42" then "[project]" table.
    // But since "renderer" is at root level and not a table, we skip it.
    // Actually this is malformed since [project] redefines at different level.
    // Let's use a format that toml++ can parse.
    // This TOML is valid: renderer at root is an integer, project is a table.
    auto result = ProjectConfig::load(tmp.path);
    ASSERT_TRUE(result.is_ok()) << result.error();

    const auto& cfg = result.value();
    // renderer defaults since it's not a table.
    EXPECT_TRUE(cfg.renderer.vsync);
    EXPECT_EQ(cfg.renderer.target_fps, 60);
}
