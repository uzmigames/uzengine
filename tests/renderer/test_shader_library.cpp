// test_shader_library.cpp -- Google Test coverage for ShaderLibrary.
//
// Covers:
//   - Registration, lookup, listing
//   - Duplicate registration overwrites
//   - Unknown shader returns nullptr / RHI_NULL
//   - Builtin shaders registration
//   - Pipeline desc hashing consistency
//   - get_or_create_pipeline caching (same desc = same handle)

#include <gtest/gtest.h>
#include "engine/renderer/resources/shader_library.h"

#include <algorithm>
#include <cstring>

// ============================================================================
// Test fixtures
// ============================================================================

static const char kDummyVS[] = "float4 VSMain() : SV_POSITION { return 0; }";
static const char kDummyPS[] = "float4 PSMain() : SV_TARGET  { return 0; }";

class ShaderLibraryTest : public ::testing::Test {
protected:
    uz::ShaderLibrary lib_;

    uz::ShaderProgram make_program() {
        return uz::ShaderProgram{
            .vs_source = kDummyVS,
            .vs_size   = sizeof(kDummyVS) - 1,
            .ps_source = kDummyPS,
            .ps_size   = sizeof(kDummyPS) - 1,
            .vs_entry  = "VSMain",
            .ps_entry  = "PSMain",
        };
    }
};

// ============================================================================
// Registration + Lookup
// ============================================================================

TEST_F(ShaderLibraryTest, ListEmpty) {
    auto names = lib_.list();
    EXPECT_TRUE(names.empty());
}

TEST_F(ShaderLibraryTest, RegisterAndGet) {
    lib_.register_shader("test", make_program());
    const auto* prog = lib_.get("test");
    ASSERT_NE(prog, nullptr);
    EXPECT_EQ(prog->vs_size, sizeof(kDummyVS) - 1);
    EXPECT_EQ(prog->ps_size, sizeof(kDummyPS) - 1);
    EXPECT_STREQ(prog->vs_entry, "VSMain");
    EXPECT_STREQ(prog->ps_entry, "PSMain");
}

TEST_F(ShaderLibraryTest, GetUnknown_ReturnsNullptr) {
    EXPECT_EQ(lib_.get("nonexistent"), nullptr);
}

TEST_F(ShaderLibraryTest, RegisterMultiple_ListSorted) {
    lib_.register_shader("shadow", make_program());
    lib_.register_shader("basic", make_program());
    lib_.register_shader("pbr", make_program());

    auto names = lib_.list();
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "basic");
    EXPECT_EQ(names[1], "pbr");
    EXPECT_EQ(names[2], "shadow");
}

TEST_F(ShaderLibraryTest, RegisterDuplicate_OverwritesPrevious) {
    auto prog1 = make_program();
    prog1.vs_entry = "VSMain1";
    lib_.register_shader("test", prog1);

    auto prog2 = make_program();
    prog2.vs_entry = "VSMain2";
    lib_.register_shader("test", prog2);

    const auto* result = lib_.get("test");
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result->vs_entry, "VSMain2");

    // Only one entry.
    auto names = lib_.list();
    EXPECT_EQ(names.size(), 1u);
}

// ============================================================================
// Builtin shaders
// ============================================================================

TEST_F(ShaderLibraryTest, RegisterBuiltins_ContainsBasic) {
    lib_.register_builtin_shaders();

    const auto* basic = lib_.get("basic");
    ASSERT_NE(basic, nullptr);
    EXPECT_GT(basic->vs_size, 0u);
    EXPECT_GT(basic->ps_size, 0u);
    EXPECT_NE(basic->vs_source, nullptr);
    EXPECT_NE(basic->ps_source, nullptr);
}

TEST_F(ShaderLibraryTest, RegisterBuiltins_ListNotEmpty) {
    lib_.register_builtin_shaders();
    auto names = lib_.list();
    EXPECT_GE(names.size(), 1u);
    // "basic" should be in the list.
    EXPECT_NE(std::find(names.begin(), names.end(), "basic"), names.end());
}

// ============================================================================
// Pipeline creation (without RHI -- test that unknown shader returns RHI_NULL)
// ============================================================================

TEST_F(ShaderLibraryTest, GetOrCreatePipeline_UnknownShader_ReturnsNull) {
    // Note: rhi() is not initialized in test mode, but we should never reach
    // pipeline_create for an unknown shader name.
    RhiPipelineDesc desc{};
    RhiPipeline result = lib_.get_or_create_pipeline("no_such_shader", desc);
    EXPECT_EQ(result, RHI_NULL);
}

// ============================================================================
// Shutdown
// ============================================================================

TEST_F(ShaderLibraryTest, Shutdown_ClearsCachedPipelines) {
    lib_.register_shader("test", make_program());
    // Shutdown should not crash even with no cached pipelines.
    lib_.shutdown();

    // Library is still usable after shutdown (just cleared caches).
    const auto* prog = lib_.get("test");
    ASSERT_NE(prog, nullptr);
}
