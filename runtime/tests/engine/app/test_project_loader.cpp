/// Unit tests for LoadProject() — .uzproject JSON parsing.
///
/// Tests cover: all-fields round-trip, minimal file, empty object defaults,
/// missing file, malformed JSON, non-object root, individual field defaults,
/// window section, render section, snake_case keys, camelCase keys, null/
/// wrong-type fields using defaults, and contentRoot snake_case priority.

#include "project_loader.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Test Fixture — writes temp JSON files and cleans them up after each test
// ─────────────────────────────────────────────────────────────────────────────

class ProjectLoaderTest : public ::testing::Test {
  protected:
    void SetUp() override {
        temp_path_ = fs::temp_directory_path() / "test_project_loader.uzproject";
    }

    void TearDown() override {
        fs::remove(temp_path_);
    }

    /// Writes `content` to the temp project file and returns its path as a string.
    std::string WriteProject(const std::string& content) {
        std::ofstream f(temp_path_, std::ios::out | std::ios::binary);
        f << content;
        return temp_path_.string();
    }

    fs::path temp_path_;
};

// ─────────────────────────────────────────────────────────────────────────────
// 1. Load valid project with all fields
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProjectLoaderTest, AllFields_ParsedCorrectly) {
    auto path = WriteProject(R"({
        "name":          "MyGame",
        "version":       "2.3.1",
        "engineVersion": "0.1.0",
        "defaultScene":  "scenes/main.uzscene",
        "contentRoot":   "assets",
        "window": {
            "title":      "My Game Window",
            "width":      1920,
            "height":     1080,
            "fullscreen": true
        },
        "render": {
            "backend": "vulkan",
            "vsync":   true
        }
    })");

    auto result = uz::LoadProject(path);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->name,          "MyGame");
    EXPECT_EQ(result->version,       "2.3.1");
    EXPECT_EQ(result->engineVersion, "0.1.0");
    EXPECT_EQ(result->defaultScene,  "scenes/main.uzscene");
    EXPECT_EQ(result->contentRoot,   "assets");
    EXPECT_EQ(result->windowTitle,   "My Game Window");
    EXPECT_EQ(result->windowWidth,   1920);
    EXPECT_EQ(result->windowHeight,  1080);
    EXPECT_TRUE(result->fullscreen);
    EXPECT_EQ(result->renderBackend, "vulkan");
    EXPECT_TRUE(result->vsync);
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. Load minimal project (name only)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProjectLoaderTest, NameOnly_ReturnsConfigWithDefaultsForEverythingElse) {
    auto path = WriteProject(R"({ "name": "TinyGame" })");

    auto result = uz::LoadProject(path);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->name,          "TinyGame");
    EXPECT_EQ(result->version,       "");
    EXPECT_EQ(result->engineVersion, "");
    EXPECT_EQ(result->defaultScene,  "");
    EXPECT_EQ(result->contentRoot,   "content");
    EXPECT_EQ(result->windowTitle,   "UzEngine");
    EXPECT_EQ(result->windowWidth,   1280);
    EXPECT_EQ(result->windowHeight,  720);
    EXPECT_FALSE(result->fullscreen);
    EXPECT_EQ(result->renderBackend, "dx12");
    EXPECT_FALSE(result->vsync);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. Empty JSON object → returns config with all defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProjectLoaderTest, EmptyObject_ReturnsAllDefaults) {
    auto path = WriteProject("{}");

    auto result = uz::LoadProject(path);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->name,          "");
    EXPECT_EQ(result->version,       "");
    EXPECT_EQ(result->engineVersion, "");
    EXPECT_EQ(result->defaultScene,  "");
    EXPECT_EQ(result->contentRoot,   "content");
    EXPECT_EQ(result->windowTitle,   "UzEngine");
    EXPECT_EQ(result->windowWidth,   1280);
    EXPECT_EQ(result->windowHeight,  720);
    EXPECT_FALSE(result->fullscreen);
    EXPECT_EQ(result->renderBackend, "dx12");
    EXPECT_FALSE(result->vsync);
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. Missing file → returns nullopt
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProjectLoaderTest, MissingFile_ReturnsNullopt) {
    auto result = uz::LoadProject("/nonexistent/path/does_not_exist.uzproject");

    EXPECT_FALSE(result.has_value());
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. Malformed JSON → returns nullopt
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProjectLoaderTest, MalformedJson_ReturnsNullopt) {
    auto path = WriteProject(R"({ "name": "Broken", "version": )");

    auto result = uz::LoadProject(path);

    EXPECT_FALSE(result.has_value());
}

TEST_F(ProjectLoaderTest, MalformedJson_TrailingGarbage_StillParsesFirstObject) {
    auto path = WriteProject(R"({"name":"X"} THIS IS NOT JSON)");

    // nlohmann::json with operator>> parses the first valid object and ignores
    // trailing content, so this succeeds.
    auto result = uz::LoadProject(path);

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->name, "X");
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. Default values when optional scalar fields are absent
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProjectLoaderTest, MissingContentRoot_DefaultsToContent) {
    auto path = WriteProject(R"({ "name": "Game" })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->contentRoot, "content");
}

TEST_F(ProjectLoaderTest, MissingWindowWidth_DefaultsTo1280) {
    auto path = WriteProject(R"({ "window": { "height": 600 } })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->windowWidth,  1280);
    EXPECT_EQ(result->windowHeight, 600);
}

TEST_F(ProjectLoaderTest, MissingWindowHeight_DefaultsTo720) {
    auto path = WriteProject(R"({ "window": { "width": 800 } })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->windowWidth,  800);
    EXPECT_EQ(result->windowHeight, 720);
}

TEST_F(ProjectLoaderTest, MissingWindowTitle_DefaultsToUzEngine) {
    auto path = WriteProject(R"({ "window": {} })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->windowTitle, "UzEngine");
}

TEST_F(ProjectLoaderTest, MissingFullscreen_DefaultsToFalse) {
    auto path = WriteProject(R"({ "window": { "width": 1280 } })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->fullscreen);
}

TEST_F(ProjectLoaderTest, MissingRenderBackend_DefaultsToDx12) {
    auto path = WriteProject(R"({ "render": {} })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->renderBackend, "dx12");
}

TEST_F(ProjectLoaderTest, MissingVsync_DefaultsToFalse) {
    auto path = WriteProject(R"({ "render": { "backend": "dx11" } })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->vsync);
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. Window section parsing
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProjectLoaderTest, WindowSection_AllFields_Parsed) {
    auto path = WriteProject(R"({
        "window": {
            "title":      "Arena Shooter",
            "width":      2560,
            "height":     1440,
            "fullscreen": true
        }
    })");

    auto result = uz::LoadProject(path);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->windowTitle,   "Arena Shooter");
    EXPECT_EQ(result->windowWidth,   2560);
    EXPECT_EQ(result->windowHeight,  1440);
    EXPECT_TRUE(result->fullscreen);
}

TEST_F(ProjectLoaderTest, WindowSection_NotAnObject_IsIgnored) {
    // "window" is a string — must fall back to defaults
    auto path = WriteProject(R"({ "window": "invalid" })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->windowTitle,  "UzEngine");
    EXPECT_EQ(result->windowWidth,  1280);
    EXPECT_EQ(result->windowHeight, 720);
    EXPECT_FALSE(result->fullscreen);
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. Render section parsing
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProjectLoaderTest, RenderSection_AllFields_Parsed) {
    auto path = WriteProject(R"({
        "render": {
            "backend": "dx11",
            "vsync":   true
        }
    })");

    auto result = uz::LoadProject(path);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->renderBackend, "dx11");
    EXPECT_TRUE(result->vsync);
}

TEST_F(ProjectLoaderTest, RenderSection_NotAnObject_IsIgnored) {
    auto path = WriteProject(R"({ "render": 42 })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->renderBackend, "dx12");
    EXPECT_FALSE(result->vsync);
}

// ─────────────────────────────────────────────────────────────────────────────
// 9. snake_case field names work
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProjectLoaderTest, SnakeCase_DefaultScene_IsParsed) {
    auto path = WriteProject(R"({ "default_scene": "scenes/start.uzscene" })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->defaultScene, "scenes/start.uzscene");
}

TEST_F(ProjectLoaderTest, SnakeCase_EngineVersion_IsParsed) {
    auto path = WriteProject(R"({ "engine_version": "0.2.0" })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->engineVersion, "0.2.0");
}

TEST_F(ProjectLoaderTest, SnakeCase_ContentRoot_IsParsed) {
    auto path = WriteProject(R"({ "content_root": "game_assets" })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->contentRoot, "game_assets");
}

// ─────────────────────────────────────────────────────────────────────────────
// 10. camelCase field names work
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProjectLoaderTest, CamelCase_DefaultScene_IsParsed) {
    auto path = WriteProject(R"({ "defaultScene": "scenes/menu.uzscene" })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->defaultScene, "scenes/menu.uzscene");
}

TEST_F(ProjectLoaderTest, CamelCase_EngineVersion_IsParsed) {
    auto path = WriteProject(R"({ "engineVersion": "0.3.0" })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->engineVersion, "0.3.0");
}

TEST_F(ProjectLoaderTest, CamelCase_ContentRoot_IsParsed) {
    auto path = WriteProject(R"({ "contentRoot": "data" })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->contentRoot, "data");
}

// ─────────────────────────────────────────────────────────────────────────────
// 11. Non-object root JSON → returns nullopt
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProjectLoaderTest, RootIsArray_ReturnsNullopt) {
    auto path = WriteProject(R"([{"name":"Game"}])");
    auto result = uz::LoadProject(path);
    EXPECT_FALSE(result.has_value());
}

TEST_F(ProjectLoaderTest, RootIsString_ReturnsNullopt) {
    auto path = WriteProject(R"("just a string")");
    auto result = uz::LoadProject(path);
    EXPECT_FALSE(result.has_value());
}

TEST_F(ProjectLoaderTest, RootIsNumber_ReturnsNullopt) {
    auto path = WriteProject("42");
    auto result = uz::LoadProject(path);
    EXPECT_FALSE(result.has_value());
}

TEST_F(ProjectLoaderTest, RootIsNull_ReturnsNullopt) {
    auto path = WriteProject("null");
    auto result = uz::LoadProject(path);
    EXPECT_FALSE(result.has_value());
}

TEST_F(ProjectLoaderTest, RootIsBool_ReturnsNullopt) {
    auto path = WriteProject("true");
    auto result = uz::LoadProject(path);
    EXPECT_FALSE(result.has_value());
}

// ─────────────────────────────────────────────────────────────────────────────
// 12. Null / wrong-type fields fall back to defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProjectLoaderTest, NullName_FallsBackToEmpty) {
    auto path = WriteProject(R"({ "name": null })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->name, "");
}

TEST_F(ProjectLoaderTest, WrongTypeVersion_NumberInsteadOfString_FallsBackToEmpty) {
    auto path = WriteProject(R"({ "version": 123 })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->version, "");
}

TEST_F(ProjectLoaderTest, WrongTypeWindowWidth_StringInsteadOfInt_FallsBackToDefault) {
    auto path = WriteProject(R"({ "window": { "width": "wide" } })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->windowWidth, 1280);
}

TEST_F(ProjectLoaderTest, WrongTypeWindowHeight_BoolInsteadOfInt_FallsBackToDefault) {
    auto path = WriteProject(R"({ "window": { "height": true } })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->windowHeight, 720);
}

TEST_F(ProjectLoaderTest, WrongTypeFullscreen_StringInsteadOfBool_FallsBackToFalse) {
    auto path = WriteProject(R"({ "window": { "fullscreen": "yes" } })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->fullscreen);
}

TEST_F(ProjectLoaderTest, WrongTypeVsync_NumberInsteadOfBool_FallsBackToFalse) {
    auto path = WriteProject(R"({ "render": { "vsync": 1 } })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->vsync);
}

TEST_F(ProjectLoaderTest, WrongTypeRenderBackend_NullInsteadOfString_FallsBackToDx12) {
    auto path = WriteProject(R"({ "render": { "backend": null } })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->renderBackend, "dx12");
}

// ─────────────────────────────────────────────────────────────────────────────
// 13. camelCase takes priority over snake_case when both are present
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProjectLoaderTest, CamelCaseTakesPriority_EngineVersion) {
    // camelCase "engineVersion" is read first; snake_case fallback should not override.
    auto path = WriteProject(R"({
        "engineVersion": "1.0.0",
        "engine_version": "9.9.9"
    })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->engineVersion, "1.0.0");
}

TEST_F(ProjectLoaderTest, CamelCaseTakesPriority_DefaultScene) {
    auto path = WriteProject(R"({
        "defaultScene":  "scenes/camel.uzscene",
        "default_scene": "scenes/snake.uzscene"
    })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->defaultScene, "scenes/camel.uzscene");
}

// ─────────────────────────────────────────────────────────────────────────────
// 14. contentRoot snake_case is used when camelCase is absent or still default
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProjectLoaderTest, ContentRoot_SnakeCase_UsedWhenCamelCaseAbsent) {
    auto path = WriteProject(R"({ "content_root": "raw" })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->contentRoot, "raw");
}

TEST_F(ProjectLoaderTest, ContentRoot_CamelCaseOverrides_SnakeCase) {
    // contentRoot is non-default so snake_case branch is skipped.
    auto path = WriteProject(R"({
        "contentRoot":  "override",
        "content_root": "ignored"
    })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->contentRoot, "override");
}

// ─────────────────────────────────────────────────────────────────────────────
// 15. Unknown keys are silently ignored
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProjectLoaderTest, UnknownTopLevelKeys_AreIgnored) {
    auto path = WriteProject(R"({
        "name":        "KnownGame",
        "unknownKey":  "should be ignored",
        "anotherKey":  12345
    })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->name, "KnownGame");
}

TEST_F(ProjectLoaderTest, UnknownWindowSubKeys_AreIgnored) {
    auto path = WriteProject(R"({
        "window": {
            "width":      800,
            "resolution": "4K",
            "monitor":    0
        }
    })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->windowWidth, 800);
}

// ─────────────────────────────────────────────────────────────────────────────
// 16. cvars block — number, boolean, and unsupported-type handling
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProjectLoaderTest, CvarsBlock_NumbersAndBoolsParsed) {
    auto path = WriteProject(R"({
        "name": "CvarTest",
        "cvars": {
            "r.AutoExposure":          false,
            "r.AutoExposure.Bias":     0.5,
            "r.SSGI":                  true,
            "r.AutoExposure.SpeedDown": 5
        }
    })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->cvarOverrides.size(), 4u);

    // Order is preserved (JSON insertion order).
    EXPECT_EQ(result->cvarOverrides[0].first, "r.AutoExposure");
    EXPECT_FLOAT_EQ(result->cvarOverrides[0].second, 0.0f);

    EXPECT_EQ(result->cvarOverrides[1].first, "r.AutoExposure.Bias");
    EXPECT_FLOAT_EQ(result->cvarOverrides[1].second, 0.5f);

    EXPECT_EQ(result->cvarOverrides[2].first, "r.SSGI");
    EXPECT_FLOAT_EQ(result->cvarOverrides[2].second, 1.0f);

    EXPECT_EQ(result->cvarOverrides[3].first, "r.AutoExposure.SpeedDown");
    EXPECT_FLOAT_EQ(result->cvarOverrides[3].second, 5.0f);
}

TEST_F(ProjectLoaderTest, CvarsBlock_StringValueIsSkipped) {
    auto path = WriteProject(R"({
        "cvars": {
            "r.Valid":   1,
            "r.Invalid": "not a number"
        }
    })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->cvarOverrides.size(), 1u);
    EXPECT_EQ(result->cvarOverrides[0].first, "r.Valid");
    EXPECT_FLOAT_EQ(result->cvarOverrides[0].second, 1.0f);
}

TEST_F(ProjectLoaderTest, CvarsBlock_Missing_LeavesEmptyVector) {
    auto path = WriteProject(R"({ "name": "NoCvars" })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->cvarOverrides.empty());
}

TEST_F(ProjectLoaderTest, CvarsBlock_NotAnObject_IsIgnored) {
    auto path = WriteProject(R"({
        "cvars": "should be an object, not a string"
    })");
    auto result = uz::LoadProject(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->cvarOverrides.empty());
}
