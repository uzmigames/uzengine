/// Tests for GI resource validity gates (task phase1_fix-sponza-intel-lighting-parity).
///
/// The renderer must never bind GI result textures/buffers that have never been
/// written: SSGI binds a sentinel SRV while disabled or before its first
/// completed render; DDGI atlas SRVs stay sentinel until the first probe update;
/// the Radiance Cache SH buffer stays hidden until its first full probe update.

#include <gtest/gtest.h>

#include "ssgi_renderer.h"
#include "ddgi_system.h"
#include "radiance_cache.h"

// ─────────────────────────────────────────────────────────────────────────────
// SSGI
// ─────────────────────────────────────────────────────────────────────────────

TEST(GIResourceValidity, SSGISettings_DisabledByDefault) {
    // Must match the r.SSGI CVar default (false) — a true default made
    // IsReady() true before Render() ever synchronized settings from the CVar.
    uz::SSGISettings settings;
    EXPECT_FALSE(settings.enabled);
}

TEST(GIResourceValidity, SSGIRenderer_NoProducedResultBeforeFirstRender) {
    uz::SSGIRenderer renderer;
    EXPECT_FALSE(renderer.HasProducedResult());
    EXPECT_FALSE(renderer.IsReady());
}

// ─────────────────────────────────────────────────────────────────────────────
// DDGI
// ─────────────────────────────────────────────────────────────────────────────

TEST(GIResourceValidity, DDGISystem_NoCompletedUpdateBeforeFirstDispatch) {
    uz::DDGISystem ddgi;
    EXPECT_FALSE(ddgi.HasCompletedFirstUpdate());
    EXPECT_FALSE(ddgi.IsInitialized());
}

// ─────────────────────────────────────────────────────────────────────────────
// Radiance Cache
// ─────────────────────────────────────────────────────────────────────────────

TEST(GIResourceValidity, RadianceCache_NotInitializedBeforeCreateResources) {
    uz::RadianceCache rc;
    EXPECT_FALSE(rc.IsInitialized());
    EXPECT_FALSE(rc.HasProducedResult());
    EXPECT_EQ(rc.GetProbeSHBufferSrv(), UINT32_MAX);
    EXPECT_EQ(rc.GetProbeSHBufferUav(), UINT32_MAX);
    rc.DestroyResources();
    EXPECT_FALSE(rc.HasProducedResult());
    EXPECT_EQ(rc.GetProbeSHBufferSrv(), UINT32_MAX);
}
