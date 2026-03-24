/// UzEngine CVarRegistry — Unit Tests
///
/// Tests the CVarRegistry singleton: registration (float, bool, int),
/// clamping on set, missing-CVar fallbacks, Reset/ResetAll, GetAll
/// enumeration, idempotent duplicate registration, and macro-based
/// auto-registration.
///
/// Design notes:
///   - Every test uses unique CVar names to avoid cross-test pollution
///     against the Meyer's singleton that persists for the process lifetime.
///   - No GPU, no RHI, no threading.  Pure CPU logic.

#include "cvar.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>

namespace uz::test {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Register a float CVar with the given name and return a pointer to it.
/// Wraps the verbose Register() call.
static CVar* RegisterFloat(const char* name, float defaultVal,
                            float minVal = -1e30f, float maxVal = 1e30f)
{
    return CVarRegistry::Get().Register(name, defaultVal, "test float cvar", minVal, maxVal);
}

// ─────────────────────────────────────────────────────────────────────────────
// 1. RegisterFloat — name, value, default, min, max stored correctly
// ─────────────────────────────────────────────────────────────────────────────

TEST(CVarRegistry, RegisterFloat_NameAndFieldsAreCorrect) {
    auto* cv = CVarRegistry::Get().Register(
        "test.CVarTest.Float1", 42.0f, "float test", 0.0f, 100.0f);

    ASSERT_NE(cv, nullptr);
    EXPECT_EQ(cv->name, "test.CVarTest.Float1");
    EXPECT_FLOAT_EQ(cv->value, 42.0f);
    EXPECT_FLOAT_EQ(cv->defaultValue, 42.0f);
    EXPECT_FLOAT_EQ(cv->minValue, 0.0f);
    EXPECT_FLOAT_EQ(cv->maxValue, 100.0f);
    EXPECT_EQ(cv->description, "float test");
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. RegisterBool — stored as float, GetBool interprets via >= 0.5
// ─────────────────────────────────────────────────────────────────────────────

TEST(CVarRegistry, RegisterBool_TrueDefault_GetBoolReturnsTrue) {
    // UZ_CVAR_BOOL stores 1.0f for true, clamped to [0, 1].
    auto* cv = CVarRegistry::Get().Register(
        "test.CVarTest.Bool1", 1.0f, "bool test", 0.0f, 1.0f);

    ASSERT_NE(cv, nullptr);
    EXPECT_FLOAT_EQ(cv->value, 1.0f);
    EXPECT_TRUE(CVarRegistry::Get().GetBool("test.CVarTest.Bool1"));
}

TEST(CVarRegistry, RegisterBool_FalseDefault_GetBoolReturnsFalse) {
    auto* cv = CVarRegistry::Get().Register(
        "test.CVarTest.Bool2", 0.0f, "bool test false", 0.0f, 1.0f);

    ASSERT_NE(cv, nullptr);
    EXPECT_FLOAT_EQ(cv->value, 0.0f);
    EXPECT_FALSE(CVarRegistry::Get().GetBool("test.CVarTest.Bool2"));
}

TEST(CVarRegistry, GetBool_Threshold_ExactlyHalfIsTrue) {
    // The spec says: value >= 0.5f → true.
    CVarRegistry::Get().Register(
        "test.CVarTest.BoolThresh", 0.5f, "threshold test", 0.0f, 1.0f);

    EXPECT_TRUE(CVarRegistry::Get().GetBool("test.CVarTest.BoolThresh"));
}

TEST(CVarRegistry, GetBool_Threshold_JustBelowHalfIsFalse) {
    CVarRegistry::Get().Register(
        "test.CVarTest.BoolThresh2", 0.49f, "threshold2 test", 0.0f, 1.0f);

    EXPECT_FALSE(CVarRegistry::Get().GetBool("test.CVarTest.BoolThresh2"));
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. RegisterInt — stored as float, GetInt truncates via static_cast
// ─────────────────────────────────────────────────────────────────────────────

TEST(CVarRegistry, RegisterInt_Default3_GetIntReturns3) {
    // UZ_CVAR_INT stores static_cast<float>(3) = 3.0f.
    auto* cv = CVarRegistry::Get().Register(
        "test.CVarTest.Int1", 3.0f, "int test", 0.0f, 10.0f);

    ASSERT_NE(cv, nullptr);
    EXPECT_FLOAT_EQ(cv->value, 3.0f);
    EXPECT_EQ(CVarRegistry::Get().GetInt("test.CVarTest.Int1"), 3);
}

TEST(CVarRegistry, GetInt_FloatValue_Truncates) {
    // Register 7.9 — GetInt should truncate to 7, not round.
    CVarRegistry::Get().Register(
        "test.CVarTest.Int2", 7.9f, "int truncation test", 0.0f, 100.0f);

    EXPECT_EQ(CVarRegistry::Get().GetInt("test.CVarTest.Int2"), 7);
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. Set_ClampsToRange
// ─────────────────────────────────────────────────────────────────────────────

TEST(CVarRegistry, Set_AboveMax_IsClamped) {
    RegisterFloat("test.CVarTest.Clamp1", 50.0f, 0.0f, 100.0f);

    CVarRegistry::Get().Set("test.CVarTest.Clamp1", 9999.0f);

    EXPECT_FLOAT_EQ(CVarRegistry::Get().GetFloat("test.CVarTest.Clamp1"), 100.0f);
}

TEST(CVarRegistry, Set_BelowMin_IsClamped) {
    RegisterFloat("test.CVarTest.Clamp2", 50.0f, 0.0f, 100.0f);

    CVarRegistry::Get().Set("test.CVarTest.Clamp2", -9999.0f);

    EXPECT_FLOAT_EQ(CVarRegistry::Get().GetFloat("test.CVarTest.Clamp2"), 0.0f);
}

TEST(CVarRegistry, Set_ExactlyAtMax_NotClamped) {
    RegisterFloat("test.CVarTest.Clamp3", 50.0f, 0.0f, 100.0f);

    CVarRegistry::Get().Set("test.CVarTest.Clamp3", 100.0f);

    EXPECT_FLOAT_EQ(CVarRegistry::Get().GetFloat("test.CVarTest.Clamp3"), 100.0f);
}

TEST(CVarRegistry, Set_ExactlyAtMin_NotClamped) {
    RegisterFloat("test.CVarTest.Clamp4", 50.0f, 10.0f, 100.0f);

    CVarRegistry::Get().Set("test.CVarTest.Clamp4", 10.0f);

    EXPECT_FLOAT_EQ(CVarRegistry::Get().GetFloat("test.CVarTest.Clamp4"), 10.0f);
}

TEST(CVarRegistry, Set_InRange_Accepted) {
    RegisterFloat("test.CVarTest.Clamp5", 50.0f, 0.0f, 100.0f);

    CVarRegistry::Get().Set("test.CVarTest.Clamp5", 73.5f);

    EXPECT_FLOAT_EQ(CVarRegistry::Get().GetFloat("test.CVarTest.Clamp5"), 73.5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. Set_UnknownCVar_IsNoOp
// ─────────────────────────────────────────────────────────────────────────────

TEST(CVarRegistry, Set_UnknownName_DoesNothing) {
    // Setting a non-existent CVar must not crash, and an unrelated CVar
    // that we do own must remain unchanged.
    RegisterFloat("test.CVarTest.Sentinel", 1.0f, 0.0f, 10.0f);
    CVarRegistry::Get().Set("test.CVarTest.DoesNotExist", 99.0f);

    // The sentinel must still be 1.0f.
    EXPECT_FLOAT_EQ(CVarRegistry::Get().GetFloat("test.CVarTest.Sentinel"), 1.0f);
    // Find on the unknown name must still be nullptr.
    EXPECT_EQ(CVarRegistry::Get().Find("test.CVarTest.DoesNotExist"), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. Find — existing and nonexistent
// ─────────────────────────────────────────────────────────────────────────────

TEST(CVarRegistry, Find_ExistingCVar_ReturnsNonNull) {
    RegisterFloat("test.CVarTest.Find1", 5.0f);

    CVar* found = CVarRegistry::Get().Find("test.CVarTest.Find1");

    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "test.CVarTest.Find1");
    EXPECT_FLOAT_EQ(found->value, 5.0f);
}

TEST(CVarRegistry, Find_NonexistentCVar_ReturnsNullptr) {
    CVar* found = CVarRegistry::Get().Find("test.CVarTest.TotallyAbsent");

    EXPECT_EQ(found, nullptr);
}

TEST(CVarRegistry, Find_Const_ExistingCVar_ReturnsNonNull) {
    RegisterFloat("test.CVarTest.FindConst1", 7.0f);

    const CVarRegistry& constReg = CVarRegistry::Get();
    const CVar* found = constReg.Find("test.CVarTest.FindConst1");

    ASSERT_NE(found, nullptr);
    EXPECT_FLOAT_EQ(found->value, 7.0f);
}

TEST(CVarRegistry, Find_Const_NonexistentCVar_ReturnsNullptr) {
    const CVarRegistry& constReg = CVarRegistry::Get();
    EXPECT_EQ(constReg.Find("test.CVarTest.ConstAbsent"), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. GetFloat_MissingCVar_ReturnsDefault
// ─────────────────────────────────────────────────────────────────────────────

TEST(CVarRegistry, GetFloat_MissingCVar_ReturnsProvidedDefault) {
    float result = CVarRegistry::Get().GetFloat("test.CVarTest.NeverRegistered", 99.5f);

    EXPECT_FLOAT_EQ(result, 99.5f);
}

TEST(CVarRegistry, GetFloat_MissingCVar_ReturnsZeroWhenNoDefault) {
    float result = CVarRegistry::Get().GetFloat("test.CVarTest.AlsoNeverRegistered");

    EXPECT_FLOAT_EQ(result, 0.0f);
}

TEST(CVarRegistry, GetInt_MissingCVar_ReturnsProvidedDefault) {
    int result = CVarRegistry::Get().GetInt("test.CVarTest.MissingInt", 42);

    EXPECT_EQ(result, 42);
}

TEST(CVarRegistry, GetBool_MissingCVar_ReturnsProvidedDefault) {
    EXPECT_TRUE(CVarRegistry::Get().GetBool("test.CVarTest.MissingBoolTrue", true));
    EXPECT_FALSE(CVarRegistry::Get().GetBool("test.CVarTest.MissingBoolFalse", false));
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. Reset_RestoresToDefault
// ─────────────────────────────────────────────────────────────────────────────

TEST(CVarRegistry, Reset_AfterSet_RestoresDefault) {
    RegisterFloat("test.CVarTest.Reset1", 10.0f, 0.0f, 100.0f);

    CVarRegistry::Get().Set("test.CVarTest.Reset1", 77.0f);
    ASSERT_FLOAT_EQ(CVarRegistry::Get().GetFloat("test.CVarTest.Reset1"), 77.0f);

    CVarRegistry::Get().Reset("test.CVarTest.Reset1");

    EXPECT_FLOAT_EQ(CVarRegistry::Get().GetFloat("test.CVarTest.Reset1"), 10.0f);
}

TEST(CVarRegistry, Reset_OnMissingCVar_IsNoOp) {
    // Must not crash.
    CVarRegistry::Get().Reset("test.CVarTest.ResetMissing");
}

TEST(CVarRegistry, Reset_ValueAndDefaultFieldsDiverged_DefaultUnchanged) {
    // After Set() and Reset(), the defaultValue field must still be
    // the original default — not the value we set.
    RegisterFloat("test.CVarTest.Reset2", 25.0f, 0.0f, 100.0f);

    CVarRegistry::Get().Set("test.CVarTest.Reset2", 80.0f);
    CVarRegistry::Get().Reset("test.CVarTest.Reset2");

    CVar* cv = CVarRegistry::Get().Find("test.CVarTest.Reset2");
    ASSERT_NE(cv, nullptr);
    EXPECT_FLOAT_EQ(cv->defaultValue, 25.0f);
    EXPECT_FLOAT_EQ(cv->value, 25.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// 9. ResetAll_RestoresAllCVars
// ─────────────────────────────────────────────────────────────────────────────

TEST(CVarRegistry, ResetAll_AllThreeCVarsReturnsToDefault) {
    RegisterFloat("test.CVarTest.ResetAll1", 1.0f, 0.0f, 10.0f);
    RegisterFloat("test.CVarTest.ResetAll2", 2.0f, 0.0f, 10.0f);
    RegisterFloat("test.CVarTest.ResetAll3", 3.0f, 0.0f, 10.0f);

    CVarRegistry::Get().Set("test.CVarTest.ResetAll1", 9.0f);
    CVarRegistry::Get().Set("test.CVarTest.ResetAll2", 8.0f);
    CVarRegistry::Get().Set("test.CVarTest.ResetAll3", 7.0f);

    ASSERT_FLOAT_EQ(CVarRegistry::Get().GetFloat("test.CVarTest.ResetAll1"), 9.0f);
    ASSERT_FLOAT_EQ(CVarRegistry::Get().GetFloat("test.CVarTest.ResetAll2"), 8.0f);
    ASSERT_FLOAT_EQ(CVarRegistry::Get().GetFloat("test.CVarTest.ResetAll3"), 7.0f);

    CVarRegistry::Get().ResetAll();

    EXPECT_FLOAT_EQ(CVarRegistry::Get().GetFloat("test.CVarTest.ResetAll1"), 1.0f);
    EXPECT_FLOAT_EQ(CVarRegistry::Get().GetFloat("test.CVarTest.ResetAll2"), 2.0f);
    EXPECT_FLOAT_EQ(CVarRegistry::Get().GetFloat("test.CVarTest.ResetAll3"), 3.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// 10. GetAll_ReturnsAllRegistered
// ─────────────────────────────────────────────────────────────────────────────

TEST(CVarRegistry, GetAll_ContainsAllRegisteredNames) {
    RegisterFloat("test.CVarTest.GetAll1", 1.0f);
    RegisterFloat("test.CVarTest.GetAll2", 2.0f);
    RegisterFloat("test.CVarTest.GetAll3", 3.0f);

    const auto& all = CVarRegistry::Get().GetAll();

    EXPECT_NE(all.find("test.CVarTest.GetAll1"), all.end());
    EXPECT_NE(all.find("test.CVarTest.GetAll2"), all.end());
    EXPECT_NE(all.find("test.CVarTest.GetAll3"), all.end());
}

TEST(CVarRegistry, GetAll_ReturnsNonEmptyMap) {
    // At minimum the CVars registered by this test file must be present.
    RegisterFloat("test.CVarTest.GetAllCheck", 0.0f);

    const auto& all = CVarRegistry::Get().GetAll();

    EXPECT_FALSE(all.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// 11. DuplicateRegistration — idempotent: returns existing, value unchanged
// ─────────────────────────────────────────────────────────────────────────────

TEST(CVarRegistry, DuplicateRegistration_ReturnsSamePointer) {
    CVar* first = RegisterFloat("test.CVarTest.Dup1", 5.0f, 0.0f, 10.0f);
    ASSERT_NE(first, nullptr);

    CVar* second = RegisterFloat("test.CVarTest.Dup1", 99.0f, -100.0f, 200.0f);
    ASSERT_NE(second, nullptr);

    // Must be the exact same object (same address in the map).
    EXPECT_EQ(first, second);
}

TEST(CVarRegistry, DuplicateRegistration_OriginalValueUnchanged) {
    RegisterFloat("test.CVarTest.Dup2", 5.0f, 0.0f, 10.0f);

    // Attempt to re-register with a different default.
    RegisterFloat("test.CVarTest.Dup2", 99.0f, 0.0f, 10.0f);

    // Value and default must still be from the first registration.
    CVar* cv = CVarRegistry::Get().Find("test.CVarTest.Dup2");
    ASSERT_NE(cv, nullptr);
    EXPECT_FLOAT_EQ(cv->value, 5.0f);
    EXPECT_FLOAT_EQ(cv->defaultValue, 5.0f);
}

TEST(CVarRegistry, DuplicateRegistration_AfterSetValuePreserved) {
    RegisterFloat("test.CVarTest.Dup3", 5.0f, 0.0f, 10.0f);
    CVarRegistry::Get().Set("test.CVarTest.Dup3", 8.0f);

    // Re-register must not reset the in-flight value.
    RegisterFloat("test.CVarTest.Dup3", 5.0f, 0.0f, 10.0f);

    EXPECT_FLOAT_EQ(CVarRegistry::Get().GetFloat("test.CVarTest.Dup3"), 8.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// 12. DefaultValue clamped to [min, max] at registration time
// ─────────────────────────────────────────────────────────────────────────────

TEST(CVarRegistry, Register_DefaultAboveMax_DefaultClamped) {
    // Registering a default of 200.0f with max=100.0f must clamp to 100.0f.
    CVar* cv = CVarRegistry::Get().Register(
        "test.CVarTest.DefaultClamp1", 200.0f, "clamp default", 0.0f, 100.0f);

    ASSERT_NE(cv, nullptr);
    EXPECT_FLOAT_EQ(cv->defaultValue, 100.0f);
    EXPECT_FLOAT_EQ(cv->value, 100.0f);
}

TEST(CVarRegistry, Register_DefaultBelowMin_DefaultClamped) {
    CVar* cv = CVarRegistry::Get().Register(
        "test.CVarTest.DefaultClamp2", -50.0f, "clamp default below", 0.0f, 100.0f);

    ASSERT_NE(cv, nullptr);
    EXPECT_FLOAT_EQ(cv->defaultValue, 0.0f);
    EXPECT_FLOAT_EQ(cv->value, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// 13. Pointer stability — Register returns a stable pointer
// ─────────────────────────────────────────────────────────────────────────────

TEST(CVarRegistry, Register_ReturnedPointer_IsStableAfterMoreRegistrations) {
    CVar* cv = RegisterFloat("test.CVarTest.Stable1", 1.0f);
    ASSERT_NE(cv, nullptr);

    // Trigger more insertions — unordered_map may rehash, but the spec says
    // pointers are stable (the map stores CVar by value; the address used
    // here must remain valid for the registry lifetime).
    // Register 20 more CVars to trigger potential rehash.
    for (int i = 0; i < 20; ++i) {
        RegisterFloat(("test.CVarTest.Stable1_extra_" + std::to_string(i)).c_str(),
                      static_cast<float>(i));
    }

    // Re-find via map — must return the same storage location.
    CVar* found = CVarRegistry::Get().Find("test.CVarTest.Stable1");
    EXPECT_EQ(cv, found);
}

// ─────────────────────────────────────────────────────────────────────────────
// 14. Macro Registration — UZ_CVAR_FLOAT, UZ_CVAR_BOOL, UZ_CVAR_INT
// ─────────────────────────────────────────────────────────────────────────────

// These statics simulate file-scope macro registration.  They register at
// static init time, before any TEST runs, which is the intended usage pattern.
static auto* s_macroFloat = UZ_CVAR_FLOAT("test.CVarTest.MacroFloat", 3.14f,
                                           "macro float", 0.0f, 10.0f);
static auto* s_macroBool  = UZ_CVAR_BOOL("test.CVarTest.MacroBool", true,
                                          "macro bool");
static auto* s_macroInt   = UZ_CVAR_INT("test.CVarTest.MacroInt", 7,
                                         "macro int", 0, 100);

TEST(CVarRegistry, Macro_FloatRegistered_CorrectValue) {
    ASSERT_NE(s_macroFloat, nullptr);
    EXPECT_EQ(s_macroFloat->name, "test.CVarTest.MacroFloat");
    EXPECT_FLOAT_EQ(s_macroFloat->value, 3.14f);
    EXPECT_FLOAT_EQ(s_macroFloat->defaultValue, 3.14f);
    EXPECT_FLOAT_EQ(s_macroFloat->minValue, 0.0f);
    EXPECT_FLOAT_EQ(s_macroFloat->maxValue, 10.0f);
}

TEST(CVarRegistry, Macro_BoolRegistered_StoredAsOneAndZero) {
    ASSERT_NE(s_macroBool, nullptr);
    // UZ_CVAR_BOOL passes (defaultVal) ? 1.f : 0.f
    EXPECT_FLOAT_EQ(s_macroBool->value, 1.0f);
    EXPECT_FLOAT_EQ(s_macroBool->minValue, 0.0f);
    EXPECT_FLOAT_EQ(s_macroBool->maxValue, 1.0f);
    EXPECT_TRUE(CVarRegistry::Get().GetBool("test.CVarTest.MacroBool"));
}

TEST(CVarRegistry, Macro_IntRegistered_StoredAsFloat) {
    ASSERT_NE(s_macroInt, nullptr);
    // UZ_CVAR_INT passes static_cast<float>(7)
    EXPECT_FLOAT_EQ(s_macroInt->value, 7.0f);
    EXPECT_FLOAT_EQ(s_macroInt->minValue, 0.0f);
    EXPECT_FLOAT_EQ(s_macroInt->maxValue, 100.0f);
    EXPECT_EQ(CVarRegistry::Get().GetInt("test.CVarTest.MacroInt"), 7);
}

TEST(CVarRegistry, Macro_PointerFromRegistry_MatchesMacroPointer) {
    // The macro returns the same CVar* as Find().
    CVar* found = CVarRegistry::Get().Find("test.CVarTest.MacroFloat");
    EXPECT_EQ(s_macroFloat, found);
}

// ─────────────────────────────────────────────────────────────────────────────
// 15. GetFloat on registered CVar returns current value (not default)
// ─────────────────────────────────────────────────────────────────────────────

TEST(CVarRegistry, GetFloat_AfterSet_ReturnsNewValue) {
    RegisterFloat("test.CVarTest.GetAfterSet", 1.0f, 0.0f, 100.0f);
    CVarRegistry::Get().Set("test.CVarTest.GetAfterSet", 55.0f);

    EXPECT_FLOAT_EQ(CVarRegistry::Get().GetFloat("test.CVarTest.GetAfterSet"), 55.0f);
}

TEST(CVarRegistry, GetFloat_WithoutSet_ReturnsDefault) {
    RegisterFloat("test.CVarTest.GetDefault", 17.0f);

    EXPECT_FLOAT_EQ(CVarRegistry::Get().GetFloat("test.CVarTest.GetDefault"), 17.0f);
}

} // namespace uz::test
