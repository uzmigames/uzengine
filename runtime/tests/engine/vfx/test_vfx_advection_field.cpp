/// Unit tests for phase 8.21.0 — VFX advection velocity-field DI.
///
/// @ref engine/vfx/data_interfaces/vfx_advection_field_di.h
/// @ref docs/engine/25-vfx/38-advection-field.md

#include <gtest/gtest.h>

#include "engine/vfx/data_interfaces/vfx_advection_field_di.h"

#include <cstring>

using namespace uz;

namespace {

VfxAdvectionFieldConfig MakeConfig() {
    VfxAdvectionFieldConfig c;
    c.fieldSrvIndex = 7u;
    c.worldMin = Vec3{-10.0f, -10.0f, -10.0f};
    c.worldMax = Vec3{ 10.0f,  10.0f,  10.0f};
    c.tightness = 0.5f;
    return c;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Config validation
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxAdvectionConfig, DefaultIsDisabled) {
    VfxAdvectionFieldConfig c;
    EXPECT_FALSE(c.IsEnabled());
}

TEST(VfxAdvectionConfig, EnabledWhenSrvAndAABBValid) {
    EXPECT_TRUE(MakeConfig().IsEnabled());
}

TEST(VfxAdvectionConfig, DegenerateAABBRejected) {
    VfxAdvectionFieldConfig c = MakeConfig();
    c.worldMax = c.worldMin; // zero-size box
    EXPECT_FALSE(c.IsEnabled());
}

TEST(VfxAdvectionConfig, UnboundSrvDisables) {
    VfxAdvectionFieldConfig c = MakeConfig();
    c.fieldSrvIndex = 0xFFFFFFFFu;
    EXPECT_FALSE(c.IsEnabled());
}

// ─────────────────────────────────────────────────────────────────────────────
// World-to-field remap
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxAdvectionRemap, CenterMapsToHalf) {
    const auto c = MakeConfig();
    const Vec3 uvw = VfxAdvectionWorldToField(c, Vec3{0, 0, 0});
    EXPECT_FLOAT_EQ(uvw.x, 0.5f);
    EXPECT_FLOAT_EQ(uvw.y, 0.5f);
    EXPECT_FLOAT_EQ(uvw.z, 0.5f);
}

TEST(VfxAdvectionRemap, MinCornerMapsToZero) {
    const auto c = MakeConfig();
    const Vec3 uvw = VfxAdvectionWorldToField(c, Vec3{-10, -10, -10});
    EXPECT_FLOAT_EQ(uvw.x, 0.0f);
    EXPECT_FLOAT_EQ(uvw.y, 0.0f);
    EXPECT_FLOAT_EQ(uvw.z, 0.0f);
}

TEST(VfxAdvectionRemap, MaxCornerMapsToOne) {
    const auto c = MakeConfig();
    const Vec3 uvw = VfxAdvectionWorldToField(c, Vec3{10, 10, 10});
    EXPECT_FLOAT_EQ(uvw.x, 1.0f);
    EXPECT_FLOAT_EQ(uvw.y, 1.0f);
    EXPECT_FLOAT_EQ(uvw.z, 1.0f);
}

TEST(VfxAdvectionRemap, OutOfBoundsYieldsOutOfRangeUV) {
    const auto c = MakeConfig();
    const Vec3 uvw = VfxAdvectionWorldToField(c, Vec3{20, 0, 0});
    EXPECT_GT(uvw.x, 1.0f); // not clamped — caller uses IsInside to gate
}

TEST(VfxAdvectionIsInside, RecognisesInsideVsOutside) {
    EXPECT_TRUE (VfxAdvectionIsInside(Vec3{0.5f, 0.5f, 0.5f}));
    EXPECT_TRUE (VfxAdvectionIsInside(Vec3{0.0f, 1.0f, 0.5f})); // edges inclusive
    EXPECT_FALSE(VfxAdvectionIsInside(Vec3{-0.01f, 0.5f, 0.5f}));
    EXPECT_FALSE(VfxAdvectionIsInside(Vec3{ 1.01f, 0.5f, 0.5f}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Tightness blend
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxAdvectionBlend, OutsideFieldPreservesCurrent) {
    const Vec3 cur{1, 2, 3}, adv{10, 20, 30};
    const Vec3 out = VfxAdvectionBlendVelocity(cur, adv, /*inside=*/false, 1.0f);
    EXPECT_FLOAT_EQ(out.x, cur.x);
    EXPECT_FLOAT_EQ(out.y, cur.y);
    EXPECT_FLOAT_EQ(out.z, cur.z);
}

TEST(VfxAdvectionBlend, TightnessZeroIgnoresField) {
    const Vec3 cur{1, 2, 3}, adv{10, 20, 30};
    const Vec3 out = VfxAdvectionBlendVelocity(cur, adv, true, 0.0f);
    EXPECT_FLOAT_EQ(out.x, cur.x);
}

TEST(VfxAdvectionBlend, TightnessOneFullFollow) {
    const Vec3 cur{1, 2, 3}, adv{10, 20, 30};
    const Vec3 out = VfxAdvectionBlendVelocity(cur, adv, true, 1.0f);
    EXPECT_FLOAT_EQ(out.x, adv.x);
    EXPECT_FLOAT_EQ(out.y, adv.y);
    EXPECT_FLOAT_EQ(out.z, adv.z);
}

TEST(VfxAdvectionBlend, TightnessHalfMidpoint) {
    const Vec3 cur{0, 0, 0}, adv{10, 10, 10};
    const Vec3 out = VfxAdvectionBlendVelocity(cur, adv, true, 0.5f);
    EXPECT_FLOAT_EQ(out.x, 5.0f);
    EXPECT_FLOAT_EQ(out.y, 5.0f);
    EXPECT_FLOAT_EQ(out.z, 5.0f);
}

TEST(VfxAdvectionBlend, NegativeTightnessClamped) {
    const Vec3 cur{1, 2, 3}, adv{10, 20, 30};
    const Vec3 out = VfxAdvectionBlendVelocity(cur, adv, true, -1.0f);
    EXPECT_FLOAT_EQ(out.x, cur.x); // clamp to 0
}

TEST(VfxAdvectionBlend, OverRangeTightnessClamped) {
    const Vec3 cur{1, 2, 3}, adv{10, 20, 30};
    const Vec3 out = VfxAdvectionBlendVelocity(cur, adv, true, 2.5f);
    EXPECT_FLOAT_EQ(out.x, adv.x); // clamp to 1
}

// ─────────────────────────────────────────────────────────────────────────────
// HLSL snippet sanity
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxAdvectionSnippet, DeclaresSampleAdvection) {
    const char* s = VfxAdvectionEmitHLSLSnippet();
    ASSERT_NE(s, nullptr);
    EXPECT_NE(std::strstr(s, "SampleAdvection"), nullptr);
    EXPECT_NE(std::strstr(s, "ResourceDescriptorHeap"), nullptr);
    EXPECT_NE(std::strstr(s, "SampleLevel"), nullptr);
}

TEST(VfxAdvectionSnippet, GuardsOutOfBoundsReturnsZero) {
    const char* s = VfxAdvectionEmitHLSLSnippet();
    EXPECT_NE(std::strstr(s, "uvw < 0.0"), nullptr);
    EXPECT_NE(std::strstr(s, "float3(0, 0, 0)"), nullptr);
}
