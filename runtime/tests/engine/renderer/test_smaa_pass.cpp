/// UzEngine SMAA Pass — CPU-Side Unit Tests
///
/// Tests CPU-only aspects of the SMAA pass: CVar registration, SMAASettings
/// default construction, and the lifecycle state of a default-constructed
/// SMAAPass object.
///
/// The 3-pass GPU pipeline (edge detection, blend weight, neighborhood blend)
/// is NOT tested here — it requires an IRHIDevice and command list (GPU).
///
/// @ref Jimenez 2012 "SMAA: Enhanced Subpixel Morphological Antialiasing"
///   — http://www.iryoku.com/smaa/

#include "engine/renderer/passes/postprocess/smaa_pass.h"
#include "cvar.h"

#include <gtest/gtest.h>

namespace uz::test {

// ─────────────────────────────────────────────────────────────────────────────
// 1. CVar Registration — r.SMAA must exist after the translation unit is linked
// ─────────────────────────────────────────────────────────────────────────────

// smaa_pass.cpp registers `r.SMAA` via UZ_CVAR_BOOL at static init time.
// This TU links smaa_pass.cpp, so the statics run before any TEST body.

TEST(SMAAPassCVars, RSMAA_CVar_ExistsInRegistry) {
    const CVar* cv = CVarRegistry::Get().Find("r.SMAA");
    ASSERT_NE(cv, nullptr)
        << "r.SMAA CVar must be registered by smaa_pass.cpp static initializer";
}

TEST(SMAAPassCVars, RSMAA_DefaultIsFalse_StoredAs0) {
    const CVar* cv = CVarRegistry::Get().Find("r.SMAA");
    ASSERT_NE(cv, nullptr);

    // UZ_CVAR_BOOL with defaultVal=false stores 0.0f.
    EXPECT_FLOAT_EQ(cv->defaultValue, 0.0f)
        << "r.SMAA default must be 0.0f (SMAA disabled by default)";
    EXPECT_FLOAT_EQ(cv->value, 0.0f)
        << "r.SMAA current value must start at 0.0f (false)";
}

TEST(SMAAPassCVars, RSMAA_ClampedToBoolRange) {
    const CVar* cv = CVarRegistry::Get().Find("r.SMAA");
    ASSERT_NE(cv, nullptr);

    // UZ_CVAR_BOOL fixes range to [0, 1].
    EXPECT_FLOAT_EQ(cv->minValue, 0.0f)
        << "r.SMAA min must be 0.0f (bool CVar)";
    EXPECT_FLOAT_EQ(cv->maxValue, 1.0f)
        << "r.SMAA max must be 1.0f (bool CVar)";
}

TEST(SMAAPassCVars, RSMAA_GetBool_DefaultIsFalse) {
    EXPECT_FALSE(CVarRegistry::Get().GetBool("r.SMAA"))
        << "r.SMAA must default to false (SMAA off by default)";
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. SMAASettings — default construction
// ─────────────────────────────────────────────────────────────────────────────

TEST(SMAASettings, DefaultConstructed_EnabledIsFalse) {
    SMAASettings s{};
    // From header: `bool enabled = false;`
    EXPECT_FALSE(s.enabled)
        << "SMAASettings::enabled must default to false";
}

TEST(SMAASettings, SetEnabled_True_FieldIsTrue) {
    SMAASettings s{};
    s.enabled = true;
    EXPECT_TRUE(s.enabled);
}

TEST(SMAASettings, SetEnabled_False_FieldIsFalse) {
    SMAASettings s{};
    s.enabled = true;
    s.enabled = false;
    EXPECT_FALSE(s.enabled);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. SMAAPass — default construction does not crash, IsInitialized is false
// ─────────────────────────────────────────────────────────────────────────────

TEST(SMAAPass, DefaultConstructed_IsNotInitialized) {
    SMAAPass pass;
    EXPECT_FALSE(pass.IsInitialized())
        << "SMAAPass must not report initialized before Init() is called";
}

TEST(SMAAPass, DefaultConstructed_SettingsEnabledIsFalse) {
    SMAAPass pass;
    EXPECT_FALSE(pass.settings.enabled)
        << "SMAAPass::settings.enabled must default to false";
}

TEST(SMAAPass, DefaultConstructed_CanBeDestroyedSafely) {
    // Destructor must not crash on a never-Init()'d instance.
    { SMAAPass pass; }
    // Reaching here means no crash or assert.
    SUCCEED();
}

} // namespace uz::test
