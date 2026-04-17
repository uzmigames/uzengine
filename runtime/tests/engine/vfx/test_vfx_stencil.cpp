/// Unit tests for phase 8.17.0 — VFX stencil + CustomDepth output.
///
/// Validates config defaults, validation rules, string round-trips, and
/// the post-process mask predicate.
///
/// @ref engine/vfx/modules/vfx_stencil.h
/// @ref docs/engine/25-vfx/34-stencil-customdepth.md

#include <gtest/gtest.h>

#include "engine/vfx/modules/vfx_stencil.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Default config
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxStencilConfig, DefaultIsDisabled) {
    VfxStencilConfig cfg;
    EXPECT_FALSE(cfg.IsEnabled());
    EXPECT_EQ(cfg.writeMode, VfxStencilWriteMode::Disabled);
    EXPECT_EQ(cfg.stencilRef, 0u);
    EXPECT_FALSE(cfg.customDepthEnabled);
}

TEST(VfxStencilConfig, IsEnabled_WhenWriteModeSet) {
    VfxStencilConfig cfg;
    cfg.writeMode  = VfxStencilWriteMode::Replace;
    cfg.stencilRef = 5;
    EXPECT_TRUE(cfg.IsEnabled());
}

TEST(VfxStencilConfig, IsEnabled_WhenCustomDepthOnly) {
    VfxStencilConfig cfg;
    cfg.customDepthEnabled = true;
    EXPECT_TRUE(cfg.IsEnabled());
}

// ─────────────────────────────────────────────────────────────────────────────
// Validation
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxStencilConfig, Valid_WhenDisabledWithAnyRef) {
    VfxStencilConfig cfg;
    cfg.stencilRef = 42;
    cfg.writeMode = VfxStencilWriteMode::Disabled;
    EXPECT_TRUE(cfg.IsValid());
}

TEST(VfxStencilConfig, Invalid_ReplaceWithZeroRef) {
    VfxStencilConfig cfg;
    cfg.stencilRef = 0;
    cfg.writeMode = VfxStencilWriteMode::Replace;
    EXPECT_FALSE(cfg.IsValid());
}

TEST(VfxStencilConfig, Valid_ReplaceWithNonZeroRef) {
    VfxStencilConfig cfg;
    cfg.stencilRef = 1;
    cfg.writeMode = VfxStencilWriteMode::Replace;
    EXPECT_TRUE(cfg.IsValid());
}

TEST(VfxStencilConfig, Valid_IncrementWithZeroRef) {
    // Increment mode doesn't require a non-zero stencilRef — the ref is
    // used as the PP compare threshold, not the write payload.
    VfxStencilConfig cfg;
    cfg.writeMode = VfxStencilWriteMode::Increment;
    cfg.stencilRef = 0;
    EXPECT_TRUE(cfg.IsValid());
}

// ─────────────────────────────────────────────────────────────────────────────
// String round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxStencilWriteModeString, NamesStable) {
    EXPECT_STREQ(VfxStencilWriteModeToString(VfxStencilWriteMode::Disabled),  "disabled");
    EXPECT_STREQ(VfxStencilWriteModeToString(VfxStencilWriteMode::Replace),   "replace");
    EXPECT_STREQ(VfxStencilWriteModeToString(VfxStencilWriteMode::Increment), "increment");
}

TEST(VfxStencilWriteModeString, FromStringRoundTrip) {
    VfxStencilWriteMode m;
    EXPECT_TRUE(VfxStencilWriteModeFromString("disabled", m));
    EXPECT_EQ(m, VfxStencilWriteMode::Disabled);
    EXPECT_TRUE(VfxStencilWriteModeFromString("replace", m));
    EXPECT_EQ(m, VfxStencilWriteMode::Replace);
    EXPECT_TRUE(VfxStencilWriteModeFromString("increment", m));
    EXPECT_EQ(m, VfxStencilWriteMode::Increment);
}

TEST(VfxStencilWriteModeString, UnknownRejected) {
    VfxStencilWriteMode m = VfxStencilWriteMode::Replace;
    EXPECT_FALSE(VfxStencilWriteModeFromString("unknown", m));
    EXPECT_EQ(m, VfxStencilWriteMode::Replace); // unchanged
}

TEST(VfxStencilWriteModeString, NullSafe) {
    VfxStencilWriteMode m = VfxStencilWriteMode::Replace;
    EXPECT_FALSE(VfxStencilWriteModeFromString(nullptr, m));
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxStencilMaskMatches — PP compare predicate
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxStencilMask, Disabled_AlwaysFalse) {
    VfxStencilConfig cfg;
    EXPECT_FALSE(VfxStencilMaskMatches(0,   cfg));
    EXPECT_FALSE(VfxStencilMaskMatches(100, cfg));
}

TEST(VfxStencilMask, Replace_RequiresExactMatch) {
    VfxStencilConfig cfg;
    cfg.writeMode = VfxStencilWriteMode::Replace;
    cfg.stencilRef = 42;
    EXPECT_TRUE (VfxStencilMaskMatches(42, cfg));
    EXPECT_FALSE(VfxStencilMaskMatches(41, cfg));
    EXPECT_FALSE(VfxStencilMaskMatches(43, cfg));
}

TEST(VfxStencilMask, Increment_UsesGreaterEqual) {
    VfxStencilConfig cfg;
    cfg.writeMode = VfxStencilWriteMode::Increment;
    cfg.stencilRef = 3; // threshold: at least 3 overlapping particles
    EXPECT_FALSE(VfxStencilMaskMatches(0, cfg));
    EXPECT_FALSE(VfxStencilMaskMatches(2, cfg));
    EXPECT_TRUE (VfxStencilMaskMatches(3, cfg));
    EXPECT_TRUE (VfxStencilMaskMatches(10, cfg));
    EXPECT_TRUE (VfxStencilMaskMatches(255, cfg));
}

TEST(VfxStencilMask, Increment_ZeroRefAcceptsAny) {
    // With ref=0, increment mode matches every non-zero stencil sample
    // (and even zero) — useful as a "any particle ever drew here" mask.
    VfxStencilConfig cfg;
    cfg.writeMode = VfxStencilWriteMode::Increment;
    cfg.stencilRef = 0;
    EXPECT_TRUE(VfxStencilMaskMatches(0, cfg));
    EXPECT_TRUE(VfxStencilMaskMatches(127, cfg));
}
