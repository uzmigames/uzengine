/// UzEngine FSR2 Upscaler — CPU-Side Unit Tests
///
/// Tests CPU-only behavior of FSR2Upscaler that is available even when
/// UZ_FSR2_ENABLED=0 (the stub path).  The stub implements all query
/// methods (GetType, GetName, GetMinScreenPercentage, GetMaxScreenPercentage)
/// and the CVar registrations, so we can verify the contract without any GPU.
///
/// Execute(), Init(), OnResize() are NOT tested — all three require either
/// a live DX12 device (UZ_FSR2_ENABLED=1) or are no-ops (stub).
///
/// @ref AMD 2022 "FidelityFX Super Resolution 2" — temporal upscaling

#include "engine/renderer/passes/postprocess/fsr2_upscaler.h"
#include "engine/renderer/passes/postprocess/temporal_upscaler.h"
#include "cvar.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

namespace uz::test {

// ─────────────────────────────────────────────────────────────────────────────
// 1. GetType — must return UpscalerType::FSR2
// ─────────────────────────────────────────────────────────────────────────────

TEST(FSR2Upscaler, GetType_ReturnsFSR2) {
    FSR2Upscaler upscaler;
    EXPECT_EQ(upscaler.GetType(), UpscalerType::FSR2)
        << "FSR2Upscaler::GetType() must return UpscalerType::FSR2";
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. GetName — must return a non-empty string containing "FSR2"
// ─────────────────────────────────────────────────────────────────────────────

TEST(FSR2Upscaler, GetName_ReturnsNonEmpty) {
    FSR2Upscaler upscaler;
    const char* name = upscaler.GetName();

    ASSERT_NE(name, nullptr)
        << "FSR2Upscaler::GetName() must never return nullptr";
    EXPECT_GT(std::strlen(name), 0u)
        << "FSR2Upscaler::GetName() must not return an empty string";
}

TEST(FSR2Upscaler, GetName_ContainsFSR2Substring) {
    FSR2Upscaler upscaler;
    const std::string name = upscaler.GetName();

    // Both the full path (UZ_FSR2_ENABLED=1 → "FSR2") and the stub path
    // (UZ_FSR2_ENABLED=0 → "FSR2 (stub)") must contain "FSR2".
    EXPECT_NE(name.find("FSR2"), std::string::npos)
        << "FSR2Upscaler::GetName() must contain 'FSR2' in its string";
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. GetMinScreenPercentage / GetMaxScreenPercentage
// ─────────────────────────────────────────────────────────────────────────────

TEST(FSR2Upscaler, GetMinScreenPercentage_IsPositive) {
    FSR2Upscaler upscaler;
    EXPECT_GT(upscaler.GetMinScreenPercentage(), 0.0f)
        << "Min screen percentage must be positive";
}

TEST(FSR2Upscaler, GetMaxScreenPercentage_Is100) {
    FSR2Upscaler upscaler;
    // @ref AMD 2022 "FidelityFX Super Resolution 2" — NativeAA preset = 100%.
    EXPECT_FLOAT_EQ(upscaler.GetMaxScreenPercentage(), 100.0f)
        << "Max screen percentage must be 100.0f (NativeAA mode)";
}

TEST(FSR2Upscaler, MinScreenPercentage_LessThanMax) {
    FSR2Upscaler upscaler;
    EXPECT_LT(upscaler.GetMinScreenPercentage(), upscaler.GetMaxScreenPercentage())
        << "Min screen percentage must be strictly less than max";
}

TEST(FSR2Upscaler, GetMinScreenPercentage_UltraPerfPreset_Is33Point3) {
    FSR2Upscaler upscaler;
    // @ref AMD 2022 "FidelityFX Super Resolution 2" — Ultra Performance = 3x
    // upscale = render at 33.3% of display.
    EXPECT_NEAR(upscaler.GetMinScreenPercentage(), 33.3f, 0.5f)
        << "FSR2 minimum screen percentage should be approximately 33% "
           "(Ultra Performance 3x preset)";
}

TEST(FSR2Upscaler, ScreenPercentageRange_BothInValidBounds) {
    FSR2Upscaler upscaler;
    const float minPct = upscaler.GetMinScreenPercentage();
    const float maxPct = upscaler.GetMaxScreenPercentage();

    // Neither value may be zero or negative.
    EXPECT_GT(minPct,   0.0f);
    EXPECT_GT(maxPct,   0.0f);
    // Max is at most 200% (supersampling is allowed but extreme).
    EXPECT_LE(maxPct, 200.0f);
    // Min must allow meaningful upscaling (at most 67%, which is Quality preset).
    EXPECT_LE(minPct,  67.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. CVar Registration — r.FSR2, r.FSR2.Quality, r.FSR2.Sharpness
// ─────────────────────────────────────────────────────────────────────────────

// fsr2_upscaler.cpp registers three CVars at static init time.
// The test binary links fsr2_upscaler.cpp, so the statics run at startup.

TEST(FSR2UpscalerCVars, RFSR2_CVar_ExistsInRegistry) {
    const CVar* cv = CVarRegistry::Get().Find("r.FSR2");
    ASSERT_NE(cv, nullptr)
        << "r.FSR2 CVar must be registered by fsr2_upscaler.cpp";
}

TEST(FSR2UpscalerCVars, RFSR2_DefaultIsFalse) {
    const CVar* cv = CVarRegistry::Get().Find("r.FSR2");
    ASSERT_NE(cv, nullptr);

    EXPECT_FLOAT_EQ(cv->defaultValue, 0.0f)
        << "r.FSR2 default must be 0.0f (disabled by default)";
    EXPECT_FLOAT_EQ(cv->minValue, 0.0f);
    EXPECT_FLOAT_EQ(cv->maxValue, 1.0f);
    EXPECT_FALSE(CVarRegistry::Get().GetBool("r.FSR2"));
}

TEST(FSR2UpscalerCVars, RFSR2Quality_CVar_ExistsWithCorrectRange) {
    const CVar* cv = CVarRegistry::Get().Find("r.FSR2.Quality");
    ASSERT_NE(cv, nullptr)
        << "r.FSR2.Quality CVar must be registered by fsr2_upscaler.cpp";

    // @ref AMD 2022 "FidelityFX Super Resolution 2" — preset indices:
    //   0=NativeAA, 1=Quality, 2=Balanced, 3=Performance, 4=UltraPerf
    EXPECT_FLOAT_EQ(cv->defaultValue, 1.0f)
        << "r.FSR2.Quality default must be 1 (Quality preset = 1.5x upscale)";
    EXPECT_FLOAT_EQ(cv->minValue, 0.0f)
        << "r.FSR2.Quality min must be 0 (NativeAA)";
    EXPECT_FLOAT_EQ(cv->maxValue, 4.0f)
        << "r.FSR2.Quality max must be 4 (Ultra Performance)";
}

TEST(FSR2UpscalerCVars, RFSR2Quality_GetInt_Returns1) {
    // Default quality is 1 (Quality preset).
    EXPECT_EQ(CVarRegistry::Get().GetInt("r.FSR2.Quality"), 1)
        << "r.FSR2.Quality GetInt() must return 1 by default";
}

TEST(FSR2UpscalerCVars, RFSR2Sharpness_CVar_ExistsWithCorrectRange) {
    const CVar* cv = CVarRegistry::Get().Find("r.FSR2.Sharpness");
    ASSERT_NE(cv, nullptr)
        << "r.FSR2.Sharpness CVar must be registered by fsr2_upscaler.cpp";

    // @ref AMD 2022 "FidelityFX Super Resolution 2" — RCAS sharpness [0, 1].
    EXPECT_FLOAT_EQ(cv->defaultValue, 0.0f)
        << "r.FSR2.Sharpness default must be 0.0f (no sharpening by default)";
    EXPECT_FLOAT_EQ(cv->minValue, 0.0f)
        << "r.FSR2.Sharpness min must be 0.0f";
    EXPECT_FLOAT_EQ(cv->maxValue, 1.0f)
        << "r.FSR2.Sharpness max must be 1.0f";
}

TEST(FSR2UpscalerCVars, RFSR2Sharpness_GetFloat_Returns0) {
    EXPECT_FLOAT_EQ(CVarRegistry::Get().GetFloat("r.FSR2.Sharpness"), 0.0f)
        << "r.FSR2.Sharpness must default to 0.0f (no sharpening)";
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. UpscalerType — FSR2 enum value is distinct from other types
// ─────────────────────────────────────────────────────────────────────────────

TEST(FSR2Upscaler, UpscalerType_FSR2_NotNone) {
    EXPECT_NE(UpscalerType::FSR2, UpscalerType::None);
}

TEST(FSR2Upscaler, UpscalerType_FSR2_NotTAA) {
    EXPECT_NE(UpscalerType::FSR2, UpscalerType::TAA);
}

TEST(FSR2Upscaler, UpscalerType_FSR2_NotTSR) {
    EXPECT_NE(UpscalerType::FSR2, UpscalerType::TSR);
}

TEST(FSR2Upscaler, UpscalerType_FSR2_NotDLSS) {
    EXPECT_NE(UpscalerType::FSR2, UpscalerType::DLSS);
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. Lifecycle — default construction and destruction do not crash
// ─────────────────────────────────────────────────────────────────────────────

TEST(FSR2Upscaler, DefaultConstruct_DoesNotCrash) {
    { FSR2Upscaler upscaler; }
    // Reaching here means no crash.
    SUCCEED();
}

TEST(FSR2Upscaler, CallShutdownOnDefaultConstructed_IsNoOp) {
    // Shutdown() on a never-Init()'d instance must be a safe no-op.
    FSR2Upscaler upscaler;
    upscaler.Shutdown(); // must not crash
    SUCCEED();
}

TEST(FSR2Upscaler, CallShutdownTwice_IsNoOp) {
    FSR2Upscaler upscaler;
    upscaler.Shutdown();
    upscaler.Shutdown(); // second call must also be safe
    SUCCEED();
}

TEST(FSR2Upscaler, CallOnResize_BeforeInit_IsNoOp) {
    // OnResize on a never-Init()'d instance must not crash.
    FSR2Upscaler upscaler;
    upscaler.OnResize(1920, 1080);
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. ITemporalUpscaler interface — FSR2Upscaler satisfies the interface contract
// ─────────────────────────────────────────────────────────────────────────────

TEST(FSR2Upscaler, IsITemporalUpscaler_GetType_MatchesConcrete) {
    FSR2Upscaler concrete;
    ITemporalUpscaler& iface = concrete;

    EXPECT_EQ(iface.GetType(), UpscalerType::FSR2);
}

TEST(FSR2Upscaler, IsITemporalUpscaler_GetName_NonNull) {
    FSR2Upscaler concrete;
    ITemporalUpscaler& iface = concrete;

    EXPECT_NE(iface.GetName(), nullptr);
}

TEST(FSR2Upscaler, IsITemporalUpscaler_ScreenPercentageBounds_Valid) {
    FSR2Upscaler concrete;
    ITemporalUpscaler& iface = concrete;

    EXPECT_GT(iface.GetMinScreenPercentage(), 0.0f);
    EXPECT_GT(iface.GetMaxScreenPercentage(), iface.GetMinScreenPercentage());
}

} // namespace uz::test
