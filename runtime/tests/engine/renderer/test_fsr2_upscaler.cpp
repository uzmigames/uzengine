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

TEST(FSR2Upscaler, GetMinScreenPercentage_UltraPerfPreset_ApproxOneThird) {
    FSR2Upscaler upscaler;
    // @ref AMD 2022 "FidelityFX Super Resolution 2" — Ultra Performance = 3x
    // upscale = render at ~33% of display.
    // Full build: 33.3f; stub build: 33.f — accept either (within 1%).
    EXPECT_NEAR(upscaler.GetMinScreenPercentage(), 33.0f, 1.0f)
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
// 4. CVar Behavior — stub build (UZ_FSR2_ENABLED=0)
//
// When UZ_FSR2_ENABLED=0 (the default), fsr2_upscaler.cpp compiles as a stub
// that does NOT register any CVars.  The static CVar initializers (r.FSR2,
// r.FSR2.Quality, r.FSR2.Sharpness) live inside the #if UZ_FSR2_ENABLED block
// and are never executed.
//
// These tests verify the correct behavior for the stub build:
//   - The CVar names are absent from the registry (Find returns nullptr).
//   - GetFloat/GetInt/GetBool return their provided defaultIfMissing values.
//
// If this codebase is ever built with UZ_FSR2_ENABLED=1, a separate integration
// test suite should verify the CVar presence and default values.
// ─────────────────────────────────────────────────────────────────────────────

TEST(FSR2UpscalerCVars, StubBuild_RFSR2_NotInRegistry) {
    // fsr2_upscaler.cpp stub path registers no CVars.
    // Find must return nullptr.
    const CVar* cv = CVarRegistry::Get().Find("r.FSR2");
    EXPECT_EQ(cv, nullptr)
        << "r.FSR2 must NOT be registered in the stub build (UZ_FSR2_ENABLED=0)";
}

TEST(FSR2UpscalerCVars, StubBuild_RFSR2Quality_NotInRegistry) {
    const CVar* cv = CVarRegistry::Get().Find("r.FSR2.Quality");
    EXPECT_EQ(cv, nullptr)
        << "r.FSR2.Quality must NOT be registered in the stub build";
}

TEST(FSR2UpscalerCVars, StubBuild_RFSR2Sharpness_NotInRegistry) {
    const CVar* cv = CVarRegistry::Get().Find("r.FSR2.Sharpness");
    EXPECT_EQ(cv, nullptr)
        << "r.FSR2.Sharpness must NOT be registered in the stub build";
}

TEST(FSR2UpscalerCVars, StubBuild_GetFloat_ReturnsProvidedDefault) {
    // Since the CVar is absent, GetFloat must return the caller's fallback.
    EXPECT_FLOAT_EQ(CVarRegistry::Get().GetFloat("r.FSR2.Sharpness", -1.0f), -1.0f)
        << "GetFloat on absent r.FSR2.Sharpness must return the provided default";
}

TEST(FSR2UpscalerCVars, StubBuild_GetBool_ReturnsFalse) {
    // Since r.FSR2 is absent, GetBool with default false returns false.
    EXPECT_FALSE(CVarRegistry::Get().GetBool("r.FSR2", false))
        << "GetBool on absent r.FSR2 must return the provided default (false)";
}

TEST(FSR2UpscalerCVars, StubBuild_GetInt_ReturnsZero) {
    // Since r.FSR2.Quality is absent, GetInt returns defaultIfMissing=0.
    EXPECT_EQ(CVarRegistry::Get().GetInt("r.FSR2.Quality", 0), 0)
        << "GetInt on absent r.FSR2.Quality must return 0 (defaultIfMissing)";
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
