/// Tests for GBuffer default state, IsValid() predicate, and field defaults.
///
/// All tests are CPU-only — no GPU device is created.
/// They verify the zero-initialized struct field layout, the three-handle
/// validity contract of IsValid(), and move semantics on default instances.

#include <gtest/gtest.h>

#include "gbuffer.h"
#include "rhi_types.h"

#include <cstdint>
#include <limits>
#include <utility>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// GBuffer default state — texture handles
// ─────────────────────────────────────────────────────────────────────────────

/// Default-constructed GBuffer albedo handle must be invalid.
TEST(GBuffer, Default_AlbedoHandle_IsInvalid) {
    GBuffer gb;
    EXPECT_FALSE(gb.albedo.IsValid())
        << "Default GBuffer albedo handle must be invalid";
}

/// Default-constructed GBuffer normal handle must be invalid.
TEST(GBuffer, Default_NormalHandle_IsInvalid) {
    GBuffer gb;
    EXPECT_FALSE(gb.normal.IsValid())
        << "Default GBuffer normal handle must be invalid";
}

/// Default-constructed GBuffer depth handle must be invalid.
TEST(GBuffer, Default_DepthHandle_IsInvalid) {
    GBuffer gb;
    EXPECT_FALSE(gb.depth.IsValid())
        << "Default GBuffer depth handle must be invalid";
}

// ─────────────────────────────────────────────────────────────────────────────
// GBuffer default state — SRV indices
// ─────────────────────────────────────────────────────────────────────────────

/// Default albedoSRV must be UINT32_MAX.
TEST(GBuffer, Default_AlbedoSRV_IsMaxUint32) {
    GBuffer gb;
    EXPECT_EQ(gb.albedoSRV, UINT32_MAX)
        << "albedoSRV must default to UINT32_MAX (unassigned sentinel)";
}

/// Default normalSRV must be UINT32_MAX.
TEST(GBuffer, Default_NormalSRV_IsMaxUint32) {
    GBuffer gb;
    EXPECT_EQ(gb.normalSRV, UINT32_MAX);
}

/// Default depthSRV must be UINT32_MAX.
TEST(GBuffer, Default_DepthSRV_IsMaxUint32) {
    GBuffer gb;
    EXPECT_EQ(gb.depthSRV, UINT32_MAX);
}

// ─────────────────────────────────────────────────────────────────────────────
// GBuffer default state — dimensions
// ─────────────────────────────────────────────────────────────────────────────

/// Default width must be 0.
TEST(GBuffer, Default_Width_IsZero) {
    GBuffer gb;
    EXPECT_EQ(gb.width, 0u);
}

/// Default height must be 0.
TEST(GBuffer, Default_Height_IsZero) {
    GBuffer gb;
    EXPECT_EQ(gb.height, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// GBuffer::IsValid()
// ─────────────────────────────────────────────────────────────────────────────

/// Default GBuffer must not be valid.
TEST(GBuffer, Default_IsValid_ReturnsFalse) {
    GBuffer gb;
    EXPECT_FALSE(gb.IsValid())
        << "Default GBuffer must report IsValid() == false";
}

/// GBuffer with only albedo valid must not be valid (all three required).
TEST(GBuffer, OnlyAlbedoValid_IsValid_ReturnsFalse) {
    GBuffer gb;
    gb.albedo.index      = 1u;
    gb.albedo.generation = 1u;
    EXPECT_FALSE(gb.IsValid())
        << "IsValid() requires all three handles to be valid";
}

/// GBuffer with only normal valid must not be valid.
TEST(GBuffer, OnlyNormalValid_IsValid_ReturnsFalse) {
    GBuffer gb;
    gb.normal.index      = 1u;
    gb.normal.generation = 1u;
    EXPECT_FALSE(gb.IsValid());
}

/// GBuffer with only depth valid must not be valid.
TEST(GBuffer, OnlyDepthValid_IsValid_ReturnsFalse) {
    GBuffer gb;
    gb.depth.index      = 1u;
    gb.depth.generation = 1u;
    EXPECT_FALSE(gb.IsValid());
}

/// GBuffer with two of three handles valid (missing depth) must not be valid.
TEST(GBuffer, TwoHandlesValid_MissingDepth_IsValid_ReturnsFalse) {
    GBuffer gb;
    gb.albedo.index      = 1u; gb.albedo.generation      = 1u;
    gb.normal.index      = 2u; gb.normal.generation      = 1u;
    // depth intentionally left invalid
    EXPECT_FALSE(gb.IsValid());
}

/// GBuffer with all six handles valid must be valid.
TEST(GBuffer, AllSixHandlesValid_IsValid_ReturnsTrue) {
    GBuffer gb;
    gb.albedo.index      = 1u; gb.albedo.generation      = 1u;
    gb.normal.index      = 2u; gb.normal.generation      = 1u;
    gb.depth.index       = 3u; gb.depth.generation       = 1u;
    gb.velocity.index    = 4u; gb.velocity.generation    = 1u;
    gb.positionWS.index  = 5u; gb.positionWS.generation  = 1u;
    gb.emissive.index    = 6u; gb.emissive.generation    = 1u;
    EXPECT_TRUE(gb.IsValid())
        << "IsValid() must return true when all six texture handles are valid";
}

/// Invalidating one handle on a fully-valid GBuffer must make IsValid() false.
TEST(GBuffer, AfterInvalidatingOneHandle_IsValid_ReturnsFalse) {
    GBuffer gb;
    gb.albedo.index      = 1u; gb.albedo.generation      = 1u;
    gb.normal.index      = 2u; gb.normal.generation      = 1u;
    gb.depth.index       = 3u; gb.depth.generation       = 1u;
    gb.velocity.index    = 4u; gb.velocity.generation    = 1u;
    gb.positionWS.index  = 5u; gb.positionWS.generation  = 1u;
    gb.emissive.index    = 6u; gb.emissive.generation    = 1u;
    ASSERT_TRUE(gb.IsValid());

    gb.normal.generation = 0u; // invalidate normal
    EXPECT_FALSE(gb.IsValid());
}

// ─────────────────────────────────────────────────────────────────────────────
// GBuffer — SRV index round-trips
// ─────────────────────────────────────────────────────────────────────────────

/// albedoSRV stores assigned value without modification.
TEST(GBuffer, AlbedoSRV_Assignment_RoundTrip) {
    GBuffer gb;
    gb.albedoSRV = 10u;
    EXPECT_EQ(gb.albedoSRV, 10u);
}

/// normalSRV stores assigned value without modification.
TEST(GBuffer, NormalSRV_Assignment_RoundTrip) {
    GBuffer gb;
    gb.normalSRV = 11u;
    EXPECT_EQ(gb.normalSRV, 11u);
}

/// depthSRV stores assigned value without modification.
TEST(GBuffer, DepthSRV_Assignment_RoundTrip) {
    GBuffer gb;
    gb.depthSRV = 13u;
    EXPECT_EQ(gb.depthSRV, 13u);
}

// ─────────────────────────────────────────────────────────────────────────────
// GBuffer — dimension field round-trips
// ─────────────────────────────────────────────────────────────────────────────

/// Width field stores assigned value correctly.
TEST(GBuffer, Width_Assignment_RoundTrip) {
    GBuffer gb;
    gb.width = 1920u;
    EXPECT_EQ(gb.width, 1920u);
}

/// Height field stores assigned value correctly.
TEST(GBuffer, Height_Assignment_RoundTrip) {
    GBuffer gb;
    gb.height = 1080u;
    EXPECT_EQ(gb.height, 1080u);
}

/// Width and height together round-trip for a 4K resolution.
TEST(GBuffer, WidthHeight_4K_RoundTrip) {
    GBuffer gb;
    gb.width  = 3840u;
    gb.height = 2160u;
    EXPECT_EQ(gb.width,  3840u);
    EXPECT_EQ(gb.height, 2160u);
}

// ─────────────────────────────────────────────────────────────────────────────
// GBuffer — move semantics on default instances
// ─────────────────────────────────────────────────────────────────────────────

/// Move-constructing from a default GBuffer produces a default GBuffer.
TEST(GBuffer, MoveConstruct_FromDefault_ProducesDefaultState) {
    GBuffer src;
    GBuffer dst(std::move(src));

    EXPECT_FALSE(dst.IsValid());
    EXPECT_EQ(dst.albedoSRV,     UINT32_MAX);
    EXPECT_EQ(dst.normalSRV,     UINT32_MAX);
    EXPECT_EQ(dst.depthSRV,      UINT32_MAX);
    EXPECT_EQ(dst.width,  0u);
    EXPECT_EQ(dst.height, 0u);
}

/// Move-assigning a default GBuffer into another default GBuffer is a no-op / safe.
TEST(GBuffer, MoveAssign_DefaultIntoDefault_IsNoOp) {
    GBuffer a;
    GBuffer b;
    b = std::move(a);

    EXPECT_FALSE(b.IsValid());
    EXPECT_EQ(b.albedoSRV,     UINT32_MAX);
    EXPECT_EQ(b.normalSRV,     UINT32_MAX);
    EXPECT_EQ(b.depthSRV,      UINT32_MAX);
}

/// Move-constructing from a fully-valid GBuffer transfers all handles and SRVs.
TEST(GBuffer, MoveConstruct_FromValid_TransfersAllFields) {
    GBuffer src;
    src.albedo.index      = 1u; src.albedo.generation      = 1u;
    src.normal.index      = 2u; src.normal.generation      = 1u;
    src.depth.index       = 3u; src.depth.generation       = 1u;
    src.velocity.index    = 4u; src.velocity.generation    = 1u;
    src.positionWS.index  = 5u; src.positionWS.generation  = 1u;
    src.emissive.index    = 6u; src.emissive.generation    = 1u;
    src.albedoSRV         = 10u;
    src.normalSRV         = 11u;
    src.depthSRV          = 12u;
    src.width             = 1920u;
    src.height            = 1080u;

    GBuffer dst(std::move(src));

    EXPECT_TRUE(dst.IsValid());
    EXPECT_EQ(dst.albedoSRV,     10u);
    EXPECT_EQ(dst.normalSRV,     11u);
    EXPECT_EQ(dst.depthSRV,      12u);
    EXPECT_EQ(dst.width,         1920u);
    EXPECT_EQ(dst.height,        1080u);
}
