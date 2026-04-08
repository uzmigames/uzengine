/// Unit tests for uz.math.vec3 (Vec3 userdata) and uz.math.quat (Quat userdata).
///
/// Covers constructors, field access, all arithmetic metamethods, all instance
/// methods, Vec3 constants, Quat constants, and all error / edge-case paths.
/// Each test uses the LuaVM singleton so the uz global table and both metatables
/// are already installed — matching exactly the runtime environment.
///
/// # Coverage targets
/// - lua_math_api.cpp  — Vec3 userdata paths: 100%
/// - lua_quat_api.cpp  — Quat userdata paths: 100%

#include "lua_vm.h"

#include <gtest/gtest.h>
#include <cmath>
#include <string>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture
// ─────────────────────────────────────────────────────────────────────────────

namespace {

class LuaMathTypesTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(uz::LuaVM::Init());
    }

    void TearDown() override {
        uz::LuaVM::Shutdown();
    }

    /// Run a Lua statement and return true on success.
    bool DoString(const char* code) {
        lua_State* L = uz::LuaVM::GetState();
        const bool ok = luaL_dostring(L, code) == 0;
        if (!ok) {
            lua_pop(L, 1); // pop error message
        }
        return ok;
    }

    /// Evaluate a Lua expression and return its boolean result.
    /// Returns false if the expression raises an error.
    bool EvalBool(const char* expr) {
        lua_State* L = uz::LuaVM::GetState();
        std::string code = std::string("return ") + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            lua_pop(L, 1);
            return false;
        }
        const bool result = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        return result;
    }

    /// Evaluate a Lua expression and return its numeric result.
    /// Returns -999.0 if the expression raises an error.
    double EvalNumber(const char* expr) {
        lua_State* L = uz::LuaVM::GetState();
        std::string code = std::string("return ") + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            lua_pop(L, 1);
            return -999.0;
        }
        const double result = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return result;
    }

    /// Evaluate a Lua expression that returns a string; empty on error.
    std::string EvalString(const char* expr) {
        lua_State* L = uz::LuaVM::GetState();
        std::string code = std::string("return ") + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            lua_pop(L, 1);
            return "";
        }
        const char* s = lua_tostring(L, -1);
        std::string result = s ? s : "";
        lua_pop(L, 1);
        return result;
    }

    /// Run a Lua snippet that sets three globals x_, y_, z_ to component
    /// values and read them back as doubles.  Returns true on success.
    bool EvalVec3Components(const char* lua_expr,
                            double& outX, double& outY, double& outZ) {
        lua_State* L = uz::LuaVM::GetState();
        std::string code =
            std::string("do local v = ") + lua_expr +
            "; x_ = v.x; y_ = v.y; z_ = v.z end";
        if (luaL_dostring(L, code.c_str()) != 0) {
            lua_pop(L, 1);
            return false;
        }
        outX = EvalNumber("x_");
        outY = EvalNumber("y_");
        outZ = EvalNumber("z_");
        return true;
    }

    /// Read Quat components (x,y,z,w) from a Lua expression.
    /// The expression must evaluate to a Quat userdata.
    bool EvalQuatComponents(const char* lua_expr,
                            double& outX, double& outY,
                            double& outZ, double& outW) {
        lua_State* L = uz::LuaVM::GetState();
        // to_euler returns degrees; we read raw components via tostring parsing
        // — instead we expose them through a tiny helper script.
        std::string code =
            std::string("do\n"
                "  local q = ") + lua_expr + "\n"
            "  local p, ya, r = q:to_euler()\n"
            "  -- store as globals for reading\n"
            "  qx_ = p; qy_ = ya; qz_ = r\n"
            "end";
        // For component-level access we rely on the fact that for trivial
        // quaternions constructed from explicit values we can reconstruct via
        // quat_from_euler round-trip.  Use a different approach: call a tiny
        // Lua chunk that stashes the four components via tostring pattern match
        // is fragile, so we expose components directly through a helper that
        // stores them.
        std::string code2 =
            std::string(
                "do\n"
                "  local function qcomps(q)\n"
                "    local s = tostring(q)\n"
                // Quat(x, y, z, w) — parse the four floats
                "    local x,y,z,w = s:match('Quat%(([^,]+), ([^,]+), ([^,]+), ([^)]+)%)')\n"
                "    return tonumber(x), tonumber(y), tonumber(z), tonumber(w)\n"
                "  end\n"
                "  local q = ") + lua_expr + "\n"
            "  qcx_, qcy_, qcz_, qcw_ = qcomps(q)\n"
            "end";
        if (luaL_dostring(L, code2.c_str()) != 0) {
            lua_pop(L, 1);
            return false;
        }
        outX = EvalNumber("qcx_");
        outY = EvalNumber("qcy_");
        outZ = EvalNumber("qcz_");
        outW = EvalNumber("qcw_");
        return true;
    }

    static constexpr double kTol = 1e-4;
};

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
//  Vec3 — constructors
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Vec3_DefaultConstructor_IsZero) {
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components("uz.math.vec3()", x, y, z));
    EXPECT_NEAR(x, 0.0, kTol);
    EXPECT_NEAR(y, 0.0, kTol);
    EXPECT_NEAR(z, 0.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_ScalarConstructor_SpreadsToAllComponents) {
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components("uz.math.vec3(7)", x, y, z));
    EXPECT_NEAR(x, 7.0, kTol);
    EXPECT_NEAR(y, 7.0, kTol);
    EXPECT_NEAR(z, 7.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_ThreeArgConstructor_SetsComponents) {
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components("uz.math.vec3(1, 2, 3)", x, y, z));
    EXPECT_NEAR(x, 1.0, kTol);
    EXPECT_NEAR(y, 2.0, kTol);
    EXPECT_NEAR(z, 3.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_NegativeComponents_StoredCorrectly) {
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components("uz.math.vec3(-3.5, 0, 100)", x, y, z));
    EXPECT_NEAR(x, -3.5, kTol);
    EXPECT_NEAR(y,  0.0, kTol);
    EXPECT_NEAR(z, 100.0, kTol);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Vec3 — field access
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Vec3_FieldX_ReturnsCorrectValue) {
    EXPECT_NEAR(EvalNumber("uz.math.vec3(5, 6, 7).x"), 5.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_FieldY_ReturnsCorrectValue) {
    EXPECT_NEAR(EvalNumber("uz.math.vec3(5, 6, 7).y"), 6.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_FieldZ_ReturnsCorrectValue) {
    EXPECT_NEAR(EvalNumber("uz.math.vec3(5, 6, 7).z"), 7.0, kTol);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Vec3 — arithmetic metamethods
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Vec3_Add_ProducesComponentWiseSum) {
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components(
        "uz.math.vec3(1,2,3) + uz.math.vec3(4,5,6)", x, y, z));
    EXPECT_NEAR(x, 5.0, kTol);
    EXPECT_NEAR(y, 7.0, kTol);
    EXPECT_NEAR(z, 9.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_Sub_ProducesComponentWiseDifference) {
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components(
        "uz.math.vec3(5,7,9) - uz.math.vec3(1,2,3)", x, y, z));
    EXPECT_NEAR(x, 4.0, kTol);
    EXPECT_NEAR(y, 5.0, kTol);
    EXPECT_NEAR(z, 6.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_MulByScalar_ScalesAllComponents) {
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components(
        "uz.math.vec3(1,2,3) * 3", x, y, z));
    EXPECT_NEAR(x, 3.0, kTol);
    EXPECT_NEAR(y, 6.0, kTol);
    EXPECT_NEAR(z, 9.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_ScalarMulVec_CommutativeScaling) {
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components(
        "2 * uz.math.vec3(4,5,6)", x, y, z));
    EXPECT_NEAR(x,  8.0, kTol);
    EXPECT_NEAR(y, 10.0, kTol);
    EXPECT_NEAR(z, 12.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_MulVecVec_ComponentWiseProduct) {
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components(
        "uz.math.vec3(2,3,4) * uz.math.vec3(5,6,7)", x, y, z));
    EXPECT_NEAR(x, 10.0, kTol);
    EXPECT_NEAR(y, 18.0, kTol);
    EXPECT_NEAR(z, 28.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_DivByScalar_DividesAllComponents) {
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components(
        "uz.math.vec3(4,6,8) / 2", x, y, z));
    EXPECT_NEAR(x, 2.0, kTol);
    EXPECT_NEAR(y, 3.0, kTol);
    EXPECT_NEAR(z, 4.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_DivByZero_RaisesLuaError) {
    // Division by zero must raise a Lua error, not crash.
    EXPECT_FALSE(DoString("local v = uz.math.vec3(1,2,3) / 0"));
}

TEST_F(LuaMathTypesTest, Vec3_Unm_NegatesAllComponents) {
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components(
        "-uz.math.vec3(1,2,3)", x, y, z));
    EXPECT_NEAR(x, -1.0, kTol);
    EXPECT_NEAR(y, -2.0, kTol);
    EXPECT_NEAR(z, -3.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_Len_ReturnsMagnitude) {
    // #vec3(3,4,0) == sqrt(9+16+0) == 5
    EXPECT_NEAR(EvalNumber("#uz.math.vec3(3,4,0)"), 5.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_Len_UnitVector_ReturnsOne) {
    EXPECT_NEAR(EvalNumber("#uz.math.vec3(1,0,0)"), 1.0, kTol);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Vec3 — __tostring
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Vec3_ToString_ContainsVec3Prefix) {
    const std::string s = EvalString("tostring(uz.math.vec3(1,2,3))");
    EXPECT_NE(s.find("Vec3("), std::string::npos)
        << "Expected tostring to contain 'Vec3(' but got: " << s;
}

TEST_F(LuaMathTypesTest, Vec3_ToString_ContainsComponents) {
    const std::string s = EvalString("tostring(uz.math.vec3(1,0,0))");
    EXPECT_NE(s.find("1"), std::string::npos);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Vec3 — __eq
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Vec3_Eq_EqualVectors_ReturnsTrue) {
    EXPECT_TRUE(EvalBool("uz.math.vec3(1,2,3) == uz.math.vec3(1,2,3)"));
}

TEST_F(LuaMathTypesTest, Vec3_Eq_DifferentVectors_ReturnsFalse) {
    EXPECT_TRUE(EvalBool("uz.math.vec3(1,0,0) ~= uz.math.vec3(0,1,0)"));
}

TEST_F(LuaMathTypesTest, Vec3_Eq_ZeroVectors_AreEqual) {
    EXPECT_TRUE(EvalBool("uz.math.vec3() == uz.math.vec3(0,0,0)"));
}

// ═════════════════════════════════════════════════════════════════════════════
//  Vec3 — instance methods
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Vec3_Dot_PerpendicularVectors_IsZero) {
    EXPECT_NEAR(EvalNumber("uz.math.vec3(1,0,0):dot(uz.math.vec3(0,1,0))"),
                0.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_Dot_ParallelVectors_IsOne) {
    EXPECT_NEAR(EvalNumber("uz.math.vec3(1,0,0):dot(uz.math.vec3(1,0,0))"),
                1.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_Dot_AntiParallelVectors_IsMinusOne) {
    EXPECT_NEAR(EvalNumber("uz.math.vec3(1,0,0):dot(uz.math.vec3(-1,0,0))"),
                -1.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_Dot_GeneralCase_CorrectResult) {
    // (1,2,3)·(4,5,6) = 4+10+18 = 32
    EXPECT_NEAR(EvalNumber("uz.math.vec3(1,2,3):dot(uz.math.vec3(4,5,6))"),
                32.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_Cross_XcrossY_IsPositiveZ) {
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components(
        "uz.math.vec3(1,0,0):cross(uz.math.vec3(0,1,0))", x, y, z));
    EXPECT_NEAR(x, 0.0, kTol);
    EXPECT_NEAR(y, 0.0, kTol);
    EXPECT_NEAR(z, 1.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_Cross_YcrossX_IsNegativeZ) {
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components(
        "uz.math.vec3(0,1,0):cross(uz.math.vec3(1,0,0))", x, y, z));
    EXPECT_NEAR(x,  0.0, kTol);
    EXPECT_NEAR(y,  0.0, kTol);
    EXPECT_NEAR(z, -1.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_Cross_ParallelVectors_IsZero) {
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components(
        "uz.math.vec3(2,0,0):cross(uz.math.vec3(3,0,0))", x, y, z));
    EXPECT_NEAR(x, 0.0, kTol);
    EXPECT_NEAR(y, 0.0, kTol);
    EXPECT_NEAR(z, 0.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_Normalize_ScaledXAxis_BecomesUnitX) {
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components(
        "uz.math.vec3(3,0,0):normalize()", x, y, z));
    EXPECT_NEAR(x, 1.0, kTol);
    EXPECT_NEAR(y, 0.0, kTol);
    EXPECT_NEAR(z, 0.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_Normalize_ZeroVector_ReturnsZero) {
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components(
        "uz.math.vec3(0,0,0):normalize()", x, y, z));
    EXPECT_NEAR(x, 0.0, kTol);
    EXPECT_NEAR(y, 0.0, kTol);
    EXPECT_NEAR(z, 0.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_Normalize_ResultHasUnitLength) {
    EXPECT_NEAR(EvalNumber("uz.math.vec3(4,3,0):normalize():length()"),
                1.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_Length_3_4_0_IsFive) {
    EXPECT_NEAR(EvalNumber("uz.math.vec3(0,3,4):length()"), 5.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_LengthSquared_CorrectResult) {
    // (1,2,3): 1+4+9 = 14
    EXPECT_NEAR(EvalNumber("uz.math.vec3(1,2,3):length_squared()"),
                14.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_Distance_OriginToPoint_IsFive) {
    EXPECT_NEAR(EvalNumber(
        "uz.math.vec3(0,0,0):distance(uz.math.vec3(3,4,0))"),
        5.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_Distance_SamePoint_IsZero) {
    EXPECT_NEAR(EvalNumber(
        "uz.math.vec3(7,8,9):distance(uz.math.vec3(7,8,9))"),
        0.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_Lerp_HalfWay_IsMidpoint) {
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components(
        "uz.math.vec3(0,0,0):lerp(uz.math.vec3(10,10,10), 0.5)",
        x, y, z));
    EXPECT_NEAR(x, 5.0, kTol);
    EXPECT_NEAR(y, 5.0, kTol);
    EXPECT_NEAR(z, 5.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_Lerp_AtZero_IsStart) {
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components(
        "uz.math.vec3(1,2,3):lerp(uz.math.vec3(10,20,30), 0)",
        x, y, z));
    EXPECT_NEAR(x, 1.0, kTol);
    EXPECT_NEAR(y, 2.0, kTol);
    EXPECT_NEAR(z, 3.0, kTol);
}

TEST_F(LuaMathTypesTest, Vec3_Lerp_AtOne_IsEnd) {
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components(
        "uz.math.vec3(1,2,3):lerp(uz.math.vec3(10,20,30), 1)",
        x, y, z));
    EXPECT_NEAR(x, 10.0, kTol);
    EXPECT_NEAR(y, 20.0, kTol);
    EXPECT_NEAR(z, 30.0, kTol);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Vec3 — constants
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Vec3_ConstantZero_IsAllZero) {
    EXPECT_TRUE(EvalBool("uz.math.VEC3_ZERO == uz.math.vec3(0,0,0)"));
}

TEST_F(LuaMathTypesTest, Vec3_ConstantOne_IsAllOne) {
    EXPECT_TRUE(EvalBool("uz.math.VEC3_ONE == uz.math.vec3(1,1,1)"));
}

TEST_F(LuaMathTypesTest, Vec3_ConstantUp_IsPositiveY) {
    EXPECT_TRUE(EvalBool("uz.math.VEC3_UP == uz.math.vec3(0,1,0)"));
}

TEST_F(LuaMathTypesTest, Vec3_ConstantRight_IsPositiveX) {
    EXPECT_TRUE(EvalBool("uz.math.VEC3_RIGHT == uz.math.vec3(1,0,0)"));
}

TEST_F(LuaMathTypesTest, Vec3_ConstantForward_IsPositiveZ) {
    EXPECT_TRUE(EvalBool("uz.math.VEC3_FORWARD == uz.math.vec3(0,0,1)"));
}

// Constants must be independent values (not aliases to the same object).
TEST_F(LuaMathTypesTest, Vec3_ConstantUp_FieldYIsOne) {
    EXPECT_NEAR(EvalNumber("uz.math.VEC3_UP.y"), 1.0, kTol);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Vec3 — type identity
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Vec3_TypeIs_Userdata) {
    EXPECT_TRUE(EvalBool("type(uz.math.vec3()) == 'userdata'"));
}

// ═════════════════════════════════════════════════════════════════════════════
//  Quat — constructors
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Quat_DefaultConstructor_IsIdentity) {
    double x, y, z, w;
    ASSERT_TRUE(EvalQuatComponents("uz.math.quat()", x, y, z, w));
    EXPECT_NEAR(x, 0.0, kTol);
    EXPECT_NEAR(y, 0.0, kTol);
    EXPECT_NEAR(z, 0.0, kTol);
    EXPECT_NEAR(w, 1.0, kTol);
}

TEST_F(LuaMathTypesTest, Quat_ExplicitConstructor_SetsComponents) {
    double x, y, z, w;
    ASSERT_TRUE(EvalQuatComponents("uz.math.quat(0.1, 0.2, 0.3, 0.9274)", x, y, z, w));
    EXPECT_NEAR(x, 0.1,    1e-3);
    EXPECT_NEAR(y, 0.2,    1e-3);
    EXPECT_NEAR(z, 0.3,    1e-3);
    EXPECT_NEAR(w, 0.9274, 1e-3);
}

TEST_F(LuaMathTypesTest, Quat_TypeIs_Userdata) {
    EXPECT_TRUE(EvalBool("type(uz.math.quat()) == 'userdata'"));
}

// ═════════════════════════════════════════════════════════════════════════════
//  Quat — quat_identity
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Quat_Identity_EqualsDefaultConstructor) {
    EXPECT_TRUE(EvalBool(
        "uz.math.quat_identity() == uz.math.quat(0,0,0,1)"));
}

TEST_F(LuaMathTypesTest, Quat_IdentityConstant_EqualsIdentityFunction) {
    EXPECT_TRUE(EvalBool(
        "uz.math.QUAT_IDENTITY == uz.math.quat_identity()"));
}

// ═════════════════════════════════════════════════════════════════════════════
//  Quat — quat_from_euler
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Quat_FromEuler_ZeroAngles_IsIdentity) {
    EXPECT_TRUE(EvalBool(
        "uz.math.quat_from_euler(0,0,0) == uz.math.quat_identity()"));
}

TEST_F(LuaMathTypesTest, Quat_FromEuler_Pitch90_HasCorrectComponents) {
    // UE5 convention: pitch = rotation around Y-right axis.
    // pitch=90°, yaw=0, roll=0 → pure Y-axis quat: x=0, y=sin(45°)≈0.7071, z=0, w≈0.7071
    double x, y, z, w;
    ASSERT_TRUE(EvalQuatComponents("uz.math.quat_from_euler(90, 0, 0)", x, y, z, w));
    EXPECT_NEAR(x, 0.0,    1e-3);
    EXPECT_NEAR(y, 0.7071, 1e-3);
    EXPECT_NEAR(z, 0.0,    1e-3);
    EXPECT_NEAR(w, 0.7071, 1e-3);
}

TEST_F(LuaMathTypesTest, Quat_FromEuler_Yaw90_HasCorrectComponents) {
    // UE5 convention: yaw = rotation around Z-up axis.
    // pitch=0, yaw=90°, roll=0 → pure Z-axis quat: x=0, y=0, z=sin(45°)≈0.7071, w≈0.7071
    double x, y, z, w;
    ASSERT_TRUE(EvalQuatComponents("uz.math.quat_from_euler(0, 90, 0)", x, y, z, w));
    EXPECT_NEAR(x, 0.0,    1e-3);
    EXPECT_NEAR(y, 0.0,    1e-3);
    EXPECT_NEAR(z, 0.7071, 1e-3);
    EXPECT_NEAR(w, 0.7071, 1e-3);
}

TEST_F(LuaMathTypesTest, Quat_FromEuler_Roll90_HasCorrectComponents) {
    // UE5 convention: roll = rotation around X-forward axis.
    // pitch=0, yaw=0, roll=90° → pure X-axis quat: x=sin(45°)≈0.7071, y=0, z=0, w≈0.7071
    double x, y, z, w;
    ASSERT_TRUE(EvalQuatComponents("uz.math.quat_from_euler(0, 0, 90)", x, y, z, w));
    EXPECT_NEAR(x, 0.7071, 1e-3);
    EXPECT_NEAR(y, 0.0,    1e-3);
    EXPECT_NEAR(z, 0.0,    1e-3);
    EXPECT_NEAR(w, 0.7071, 1e-3);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Quat — quat_from_axis_angle
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Quat_FromAxisAngle_ZeroAngle_IsIdentity) {
    // Rotating 0° around any axis must give identity.
    double x, y, z, w;
    ASSERT_TRUE(EvalQuatComponents(
        "uz.math.quat_from_axis_angle(0, 1, 0, 0)", x, y, z, w));
    EXPECT_NEAR(x, 0.0, kTol);
    EXPECT_NEAR(y, 0.0, kTol);
    EXPECT_NEAR(z, 0.0, kTol);
    EXPECT_NEAR(w, 1.0, kTol);
}

TEST_F(LuaMathTypesTest, Quat_FromAxisAngle_ZeroAxis_IsIdentity) {
    // Degenerate zero-length axis must return identity.
    double x, y, z, w;
    ASSERT_TRUE(EvalQuatComponents(
        "uz.math.quat_from_axis_angle(0, 0, 0, 90)", x, y, z, w));
    EXPECT_NEAR(x, 0.0, kTol);
    EXPECT_NEAR(y, 0.0, kTol);
    EXPECT_NEAR(z, 0.0, kTol);
    EXPECT_NEAR(w, 1.0, kTol);
}

TEST_F(LuaMathTypesTest, Quat_FromAxisAngle_90DegYAxis_MatchesFromEuler) {
    // UE5 convention: yaw=90° is Z-rotation. quat_from_euler(90,0,0) = Y-rotation (pitch).
    // So FromAxisAngle(Z, 90°) should match FromEuler(pitch=0, yaw=90, roll=0).
    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local q1 = uz.math.quat_from_axis_angle(0, 0, 1, 90)\n"
        "  local q2 = uz.math.quat_from_euler(0, 90, 0)\n"
        "  local function near(a, b) return math.abs(a - b) < 0.001 end\n"
        "  local s1 = tostring(q1)\n"
        "  local s2 = tostring(q2)\n"
        "  local function parse(s)\n"
        "    local x,y,z,w = s:match('Quat%(([^,]+), ([^,]+), ([^,]+), ([^)]+)%)')\n"
        "    return tonumber(x), tonumber(y), tonumber(z), tonumber(w)\n"
        "  end\n"
        "  local x1,y1,z1,w1 = parse(s1)\n"
        "  local x2,y2,z2,w2 = parse(s2)\n"
        "  return near(x1,x2) and near(y1,y2) and near(z1,z2) and near(w1,w2)\n"
        "end)()"));
}

TEST_F(LuaMathTypesTest, Quat_FromAxisAngle_WithVec3Userdata_AcceptsVec3) {
    // The second overload takes a uz.Vec3 userdata as the axis.
    EXPECT_TRUE(DoString(
        "local axis = uz.math.vec3(0,1,0)\n"
        "local q = uz.math.quat_from_axis_angle(axis, 90)\n"
        "assert(type(q) == 'userdata')"));
}

// ═════════════════════════════════════════════════════════════════════════════
//  Quat — __tostring
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Quat_ToString_ContainsQuatPrefix) {
    const std::string s = EvalString("tostring(uz.math.quat())");
    EXPECT_NE(s.find("Quat("), std::string::npos)
        << "Expected tostring to contain 'Quat(' but got: " << s;
}

TEST_F(LuaMathTypesTest, Quat_ToString_IdentityContainsFourComponents) {
    const std::string s = EvalString("tostring(uz.math.quat_identity())");
    // lua_pushfstring in LuaJIT uses %g-style output (no forced decimal places).
    // Identity is Quat(0, 0, 0, 1) or Quat(0.0000, 0.0000, 0.0000, 1.0000).
    // Verify the w=1 component appears after the last comma inside the parens.
    const auto commaPos = s.rfind(',');
    EXPECT_NE(commaPos, std::string::npos)
        << "Expected at least one comma in tostring output: " << s;
    if (commaPos != std::string::npos) {
        // Extract the w value substring after the last comma.
        std::string wPart = s.substr(commaPos + 1);
        // Strip trailing ')' and spaces.
        while (!wPart.empty() && (wPart.back() == ')' || wPart.back() == ' ')) {
            wPart.pop_back();
        }
        // Should parse to 1.0 (the w component of identity).
        const double wVal = std::stod(wPart);
        EXPECT_NEAR(wVal, 1.0, 1e-4)
            << "Identity w component must be 1.0 in: " << s;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Quat — __eq
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Quat_Eq_IdentityEqualsIdentity) {
    EXPECT_TRUE(EvalBool(
        "uz.math.quat_identity() == uz.math.quat_identity()"));
}

TEST_F(LuaMathTypesTest, Quat_Eq_DifferentQuats_AreNotEqual) {
    EXPECT_TRUE(EvalBool(
        "uz.math.quat_from_euler(90,0,0) ~= uz.math.quat_from_euler(0,90,0)"));
}

// ═════════════════════════════════════════════════════════════════════════════
//  Quat — __mul compose
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Quat_MulCompose_IdentityTimesQ_IsQ) {
    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local q = uz.math.quat_from_euler(30, 45, 60)\n"
        "  local id = uz.math.quat_identity()\n"
        "  local r = id * q\n"
        "  local function parse(s)\n"
        "    local x,y,z,w = s:match('Quat%(([^,]+), ([^,]+), ([^,]+), ([^)]+)%)')\n"
        "    return tonumber(x), tonumber(y), tonumber(z), tonumber(w)\n"
        "  end\n"
        "  local rx,ry,rz,rw = parse(tostring(r))\n"
        "  local qx,qy,qz,qw = parse(tostring(q))\n"
        "  local function near(a,b) return math.abs(a-b) < 0.001 end\n"
        "  return near(rx,qx) and near(ry,qy) and near(rz,qz) and near(rw,qw)\n"
        "end)()"));
}

TEST_F(LuaMathTypesTest, Quat_MulCompose_QTimesIdentity_IsQ) {
    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local q = uz.math.quat_from_euler(30, 45, 60)\n"
        "  local id = uz.math.quat_identity()\n"
        "  local r = q * id\n"
        "  local function parse(s)\n"
        "    local x,y,z,w = s:match('Quat%(([^,]+), ([^,]+), ([^,]+), ([^)]+)%)')\n"
        "    return tonumber(x), tonumber(y), tonumber(z), tonumber(w)\n"
        "  end\n"
        "  local rx,ry,rz,rw = parse(tostring(r))\n"
        "  local qx,qy,qz,qw = parse(tostring(q))\n"
        "  local function near(a,b) return math.abs(a-b) < 0.001 end\n"
        "  return near(rx,qx) and near(ry,qy) and near(rz,qz) and near(rw,qw)\n"
        "end)()"));
}

TEST_F(LuaMathTypesTest, Quat_MulCompose_180DegYTwice_IsNearIdentity) {
    // Two 180° Y rotations compose to ≈ identity (allow sign flip).
    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local half = uz.math.quat_from_euler(0, 180, 0)\n"
        "  local r = half * half\n"
        "  local function parse(s)\n"
        "    local x,y,z,w = s:match('Quat%(([^,]+), ([^,]+), ([^,]+), ([^)]+)%)')\n"
        "    return tonumber(x), tonumber(y), tonumber(z), tonumber(w)\n"
        "  end\n"
        "  local x,y,z,w = parse(tostring(r))\n"
        // Either (0,0,0,1) or (0,0,0,-1) is a valid double cover of identity.
        "  return math.abs(x) < 0.01 and math.abs(y) < 0.01 and math.abs(z) < 0.01\n"
        "         and (math.abs(math.abs(w) - 1) < 0.01)\n"
        "end)()"));
}

// ═════════════════════════════════════════════════════════════════════════════
//  Quat — __mul rotate Vec3
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Quat_MulVec3_IdentityRotation_PreservesVector) {
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components(
        "uz.math.quat_identity() * uz.math.vec3(1, 2, 3)",
        x, y, z));
    EXPECT_NEAR(x, 1.0, kTol);
    EXPECT_NEAR(y, 2.0, kTol);
    EXPECT_NEAR(z, 3.0, kTol);
}

TEST_F(LuaMathTypesTest, Quat_MulVec3_90DegYRotation_RotatesXIntoNegZ) {
    // UE5-parity: pitch=Y-rotation. pitch=90° rotates forward [1,0,0] → [0,0,-1].
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components(
        "uz.math.quat_from_euler(90, 0, 0) * uz.math.vec3(1, 0, 0)",
        x, y, z));
    EXPECT_NEAR(x,  0.0, 1e-3);
    EXPECT_NEAR(y,  0.0, 1e-3);
    EXPECT_NEAR(z, -1.0, 1e-3);
}

TEST_F(LuaMathTypesTest, Quat_MulVec3_90DegXRotation_RotatesYIntoZ) {
    // UE5-parity: roll=X-rotation. roll=90° rotates right [0,1,0] → [0,0,1].
    double x, y, z;
    ASSERT_TRUE(EvalVec3Components(
        "uz.math.quat_from_euler(0, 0, 90) * uz.math.vec3(0, 1, 0)",
        x, y, z));
    EXPECT_NEAR(x, 0.0, 1e-3);
    EXPECT_NEAR(y, 0.0, 1e-3);
    EXPECT_NEAR(z, 1.0, 1e-3);
}

TEST_F(LuaMathTypesTest, Quat_MulVec3_PreservesVectorLength) {
    EXPECT_NEAR(EvalNumber(
        "(uz.math.quat_from_euler(30, 45, 60) * uz.math.vec3(1,2,3)):length()"),
        std::sqrt(1.0 + 4.0 + 9.0), 1e-3);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Quat — slerp
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Quat_Slerp_TZero_ReturnsStartQuat) {
    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local id = uz.math.quat_identity()\n"
        "  local q  = uz.math.quat_from_euler(0, 90, 0)\n"
        "  local r  = id:slerp(q, 0)\n"
        "  return r == id\n"
        "end)()"));
}

TEST_F(LuaMathTypesTest, Quat_Slerp_TOne_ReturnsEndQuat) {
    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local id = uz.math.quat_identity()\n"
        "  local q  = uz.math.quat_from_euler(0, 90, 0)\n"
        "  local r  = id:slerp(q, 1)\n"
        "  local function parse(s)\n"
        "    local x,y,z,w = s:match('Quat%(([^,]+), ([^,]+), ([^,]+), ([^)]+)%)')\n"
        "    return tonumber(x), tonumber(y), tonumber(z), tonumber(w)\n"
        "  end\n"
        "  local rx,ry,rz,rw = parse(tostring(r))\n"
        "  local qx,qy,qz,qw = parse(tostring(q))\n"
        "  local function near(a,b) return math.abs(a-b) < 0.001 end\n"
        "  return near(rx,qx) and near(ry,qy) and near(rz,qz) and near(rw,qw)\n"
        "end)()"));
}

TEST_F(LuaMathTypesTest, Quat_Slerp_SameQuat_ReturnsUnchanged) {
    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local q = uz.math.quat_from_euler(30, 45, 60)\n"
        "  local r = q:slerp(q, 0.5)\n"
        "  local function parse(s)\n"
        "    local x,y,z,w = s:match('Quat%(([^,]+), ([^,]+), ([^,]+), ([^)]+)%)')\n"
        "    return tonumber(x), tonumber(y), tonumber(z), tonumber(w)\n"
        "  end\n"
        "  local rx,ry,rz,rw = parse(tostring(r))\n"
        "  local qx,qy,qz,qw = parse(tostring(q))\n"
        "  local function near(a,b) return math.abs(a-b) < 0.001 end\n"
        "  return near(rx,qx) and near(ry,qy) and near(rz,qz) and near(rw,qw)\n"
        "end)()"));
}

TEST_F(LuaMathTypesTest, Quat_Slerp_HalfWay_IsHalfAngle) {
    // slerp(identity, 90°Y, 0.5) should give ≈ 45°Y rotation.
    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local id  = uz.math.quat_identity()\n"
        "  local q90 = uz.math.quat_from_euler(0, 90, 0)\n"
        "  local q45 = uz.math.quat_from_euler(0, 45, 0)\n"
        "  local r   = id:slerp(q90, 0.5)\n"
        "  local function parse(s)\n"
        "    local x,y,z,w = s:match('Quat%(([^,]+), ([^,]+), ([^,]+), ([^)]+)%)')\n"
        "    return tonumber(x), tonumber(y), tonumber(z), tonumber(w)\n"
        "  end\n"
        "  local rx,ry,rz,rw = parse(tostring(r))\n"
        "  local ex,ey,ez,ew = parse(tostring(q45))\n"
        "  local function near(a,b) return math.abs(a-b) < 0.002 end\n"
        "  return near(rx,ex) and near(ry,ey) and near(rz,ez) and near(rw,ew)\n"
        "end)()"));
}

// ═════════════════════════════════════════════════════════════════════════════
//  Quat — inverse
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Quat_Inverse_TimesOriginal_IsNearIdentity) {
    // q * q:inverse() ≈ identity
    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local q  = uz.math.quat_from_euler(30, 45, 60)\n"
        "  local qi = q:inverse()\n"
        "  local r  = q * qi\n"
        "  local function parse(s)\n"
        "    local x,y,z,w = s:match('Quat%(([^,]+), ([^,]+), ([^,]+), ([^)]+)%)')\n"
        "    return tonumber(x), tonumber(y), tonumber(z), tonumber(w)\n"
        "  end\n"
        "  local rx,ry,rz,rw = parse(tostring(r))\n"
        "  local function near(a, b) return math.abs(a-b) < 0.001 end\n"
        "  return near(rx, 0) and near(ry, 0) and near(rz, 0)\n"
        "         and (near(rw, 1) or near(rw, -1))\n"
        "end)()"));
}

TEST_F(LuaMathTypesTest, Quat_Inverse_OfIdentity_IsIdentity) {
    EXPECT_TRUE(EvalBool(
        "uz.math.quat_identity():inverse() == uz.math.quat_identity()"));
}

// ═════════════════════════════════════════════════════════════════════════════
//  Quat — normalize
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Quat_Normalize_UnitQuat_StaysUnit) {
    // identity is already unit; normalizing it must stay identity.
    EXPECT_TRUE(EvalBool(
        "uz.math.quat_identity():normalize() == uz.math.quat_identity()"));
}

TEST_F(LuaMathTypesTest, Quat_Normalize_ResultHasUnitLength) {
    EXPECT_NEAR(EvalNumber(
        "uz.math.quat(0.1, 0.2, 0.3, 0.9):normalize():length()"),
        1.0, kTol);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Quat — to_euler round-trip
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Quat_ToEuler_IdentityGivesZeroAngles) {
    EXPECT_TRUE(DoString(
        "local p, y, r = uz.math.quat_identity():to_euler()\n"
        "assert(math.abs(p) < 0.01, 'pitch not zero')\n"
        "assert(math.abs(y) < 0.01, 'yaw not zero')\n"
        "assert(math.abs(r) < 0.01, 'roll not zero')"));
}

TEST_F(LuaMathTypesTest, Quat_ToEuler_Yaw45_RoundTrip) {
    // quat_from_euler(0,45,0) → to_euler() must recover ≈ (0,45,0).
    EXPECT_TRUE(DoString(
        "local q = uz.math.quat_from_euler(0, 45, 0)\n"
        "local p, y, r = q:to_euler()\n"
        "assert(math.abs(p) < 0.1, 'pitch should be ~0 got ' .. tostring(p))\n"
        "assert(math.abs(y - 45) < 0.1, 'yaw should be ~45 got ' .. tostring(y))\n"
        "assert(math.abs(r) < 0.1, 'roll should be ~0 got ' .. tostring(r))"));
}

TEST_F(LuaMathTypesTest, Quat_ToEuler_Pitch30_RoundTrip) {
    EXPECT_TRUE(DoString(
        "local q = uz.math.quat_from_euler(30, 0, 0)\n"
        "local p, y, r = q:to_euler()\n"
        "assert(math.abs(p - 30) < 0.1, 'pitch should be ~30 got ' .. tostring(p))"));
}

TEST_F(LuaMathTypesTest, Quat_ToEuler_Roll60_RoundTrip) {
    EXPECT_TRUE(DoString(
        "local q = uz.math.quat_from_euler(0, 0, 60)\n"
        "local p, y, r = q:to_euler()\n"
        "assert(math.abs(r - 60) < 0.1, 'roll should be ~60 got ' .. tostring(r))"));
}

TEST_F(LuaMathTypesTest, Quat_ToEuler_ReturnsThreeValues) {
    EXPECT_TRUE(DoString(
        "local p, y, r = uz.math.quat_identity():to_euler()\n"
        "assert(type(p) == 'number')\n"
        "assert(type(y) == 'number')\n"
        "assert(type(r) == 'number')"));
}

// ═════════════════════════════════════════════════════════════════════════════
//  Quat — dot
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Quat_Dot_IdentityWithItself_IsOne) {
    EXPECT_NEAR(EvalNumber(
        "uz.math.quat_identity():dot(uz.math.quat_identity())"),
        1.0, kTol);
}

TEST_F(LuaMathTypesTest, Quat_Dot_SameQuat_IsOne) {
    // Wrap in DoString/EvalNumber split to avoid "return local ..." syntax error.
    ASSERT_TRUE(DoString(
        "local q = uz.math.quat_from_euler(30, 45, 60)\n"
        "_dot_same = q:dot(q)"));
    EXPECT_NEAR(EvalNumber("_dot_same"), 1.0, kTol);
}

TEST_F(LuaMathTypesTest, Quat_Dot_OrthogonaQuats_IsSmall) {
    // 180° Y and identity: dot = cos(90°) = 0
    ASSERT_TRUE(DoString(
        "local id  = uz.math.quat_identity()\n"
        "local q90 = uz.math.quat_from_euler(0, 180, 0)\n"
        "_dot_ortho = id:dot(q90)"));
    const double d = EvalNumber("_dot_ortho");
    EXPECT_NEAR(d, 0.0, 1e-3);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Quat — length
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Quat_Length_IdentityIsOne) {
    EXPECT_NEAR(EvalNumber("uz.math.quat_identity():length()"), 1.0, kTol);
}

TEST_F(LuaMathTypesTest, Quat_Length_FromEuler_IsAlwaysOne) {
    // Constructed quaternions from Euler angles must be unit quaternions.
    EXPECT_NEAR(EvalNumber("uz.math.quat_from_euler(30, 45, 60):length()"),
                1.0, kTol);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Quat — stack balance (no leaks)
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, Quat_OperationsDoNotLeakStack) {
    lua_State* L = uz::LuaVM::GetState();
    const int topBefore = lua_gettop(L);

    ASSERT_TRUE(DoString(
        "local q1 = uz.math.quat_from_euler(10, 20, 30)\n"
        "local q2 = uz.math.quat_from_euler(40, 50, 60)\n"
        "local r  = q1 * q2\n"
        "local ri = r:inverse()\n"
        "local _  = r:slerp(q1, 0.5)\n"
        "local p, y, roll = r:to_euler()\n"));

    EXPECT_EQ(lua_gettop(L), topBefore);
}

TEST_F(LuaMathTypesTest, Vec3_OperationsDoNotLeakStack) {
    lua_State* L = uz::LuaVM::GetState();
    const int topBefore = lua_gettop(L);

    ASSERT_TRUE(DoString(
        "local a = uz.math.vec3(1, 2, 3)\n"
        "local b = uz.math.vec3(4, 5, 6)\n"
        "local _ = a + b\n"
        "local _ = a:cross(b)\n"
        "local _ = a:normalize()\n"
        "local _ = a:dot(b)\n"
        "local _ = a:lerp(b, 0.5)\n"
        "local _ = a:length()\n"
        "local _ = a:distance(b)\n"));

    EXPECT_EQ(lua_gettop(L), topBefore);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Cross-type — Quat * Vec3 when both metatables are registered
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LuaMathTypesTest, QuatMulVec3_ResultIsVec3Userdata) {
    // When both metatables are registered the result must be a Vec3 userdata,
    // not three flat numbers.
    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local r = uz.math.quat_identity() * uz.math.vec3(1, 0, 0)\n"
        "  return type(r) == 'userdata'\n"
        "end)()"));
}

TEST_F(LuaMathTypesTest, QuatMulVec3_ResultHasXYZFields) {
    EXPECT_TRUE(DoString(
        "local r = uz.math.quat_identity() * uz.math.vec3(3, 4, 5)\n"
        "assert(math.abs(r.x - 3) < 0.01)\n"
        "assert(math.abs(r.y - 4) < 0.01)\n"
        "assert(math.abs(r.z - 5) < 0.01)"));
}
