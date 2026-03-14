/// Unit tests for Lua Camera API — uz.camera.get_position, set_position,
/// look_at, get_fov, set_fov. Uses a real World with a CameraComponent entity.

#include "lua_vm.h"
#include "lua_scene_api.h"
#include "lua_camera_api.h"
#include "../ecs/world.h"
#include "../ecs/camera_component.h"
#include "../math/vector3.h"
#include "../math/quaternion.h"

#include <gtest/gtest.h>
#include <cmath>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Fixture
// ─────────────────────────────────────────────────────────────────────────────

class LuaCameraAPITest : public ::testing::Test {
  protected:
    uz::World world_;

    void SetUp() override {
        ASSERT_TRUE(uz::LuaVM::Init());
        uz::LuaSetWorld(uz::LuaVM::GetState(), &world_);
    }

    void TearDown() override {
        uz::LuaVM::Shutdown();
    }

    /// Helper: run Lua code, return true on success.
    bool DoString(const char* code) {
        return luaL_dostring(uz::LuaVM::GetState(), code) == 0;
    }

    /// Helper: evaluate a Lua expression and return a number.
    /// Returns -999.0 and fails the test if the expression errors.
    double EvalNumber(const char* expr) {
        lua_State* L = uz::LuaVM::GetState();
        std::string code = std::string("return ") + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            const char* err = lua_tostring(L, -1);
            ADD_FAILURE() << "Lua error in EvalNumber(\"" << expr
                          << "\"): " << (err ? err : "unknown");
            lua_pop(L, 1);
            return -999.0;
        }
        double r = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return r;
    }

    /// Helper: evaluate a Lua expression and return a bool.
    bool EvalBool(const char* expr) {
        lua_State* L = uz::LuaVM::GetState();
        std::string code = std::string("return ") + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            lua_pop(L, 1);
            return false;
        }
        bool r = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        return r;
    }

    /// Creates an active CameraComponent entity at the given position.
    ///
    /// CameraComponent is non-copyable, so we use GetRegistry().emplace<>
    /// directly (same pattern as test_lua_scene_api.cpp).
    ///
    /// Returns a reference to the emplaced component for direct inspection.
    uz::CameraComponent& CreateActiveCamera(const uz::Vec3& pos = {0.f, 0.f, 0.f}) {
        auto e = world_.CreateEntity();
        auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(e.id));
        auto& cam = world_.GetRegistry().emplace<uz::CameraComponent>(enttEntity);
        cam.SetPosition(pos);
        cam.isActive = true;
        return cam;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// uz.camera table and function existence
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCameraAPITest, CameraTableExists_AfterInit) {
    EXPECT_TRUE(EvalBool("type(uz.camera) == 'table'"));
}

TEST_F(LuaCameraAPITest, CameraFunctionsExist_AfterInit) {
    EXPECT_TRUE(EvalBool("type(uz.camera.get_position) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.camera.set_position) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.camera.look_at) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.camera.get_fov) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.camera.set_fov) == 'function'"));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.camera.get_position
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCameraAPITest, GetPosition_ReturnsDefaultCameraPosition) {
    CreateActiveCamera({0.f, 0.f, 0.f});

    EXPECT_TRUE(DoString("_cx, _cy, _cz = uz.camera.get_position()"));

    EXPECT_NEAR(EvalNumber("_cx"), 0.0, 1e-4);
    EXPECT_NEAR(EvalNumber("_cy"), 0.0, 1e-4);
    EXPECT_NEAR(EvalNumber("_cz"), 0.0, 1e-4);
}

TEST_F(LuaCameraAPITest, GetPosition_ReturnsNonZeroCameraPosition) {
    CreateActiveCamera({3.f, 7.f, -2.f});

    EXPECT_TRUE(DoString("_cx, _cy, _cz = uz.camera.get_position()"));

    EXPECT_NEAR(EvalNumber("_cx"),  3.0, 1e-4);
    EXPECT_NEAR(EvalNumber("_cy"),  7.0, 1e-4);
    EXPECT_NEAR(EvalNumber("_cz"), -2.0, 1e-4);
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.camera.set_position
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCameraAPITest, SetPosition_ChangesActiveCameraPosition) {
    auto& cam = CreateActiveCamera({0.f, 0.f, 0.f});

    EXPECT_TRUE(DoString("uz.camera.set_position(5, 10, -3)"));

    EXPECT_NEAR(cam.GetPosition().x,  5.f, 1e-4f);
    EXPECT_NEAR(cam.GetPosition().y, 10.f, 1e-4f);
    EXPECT_NEAR(cam.GetPosition().z, -3.f, 1e-4f);
}

TEST_F(LuaCameraAPITest, SetPosition_RoundTripsViaGetPosition) {
    CreateActiveCamera({0.f, 0.f, 0.f});

    EXPECT_TRUE(DoString(
        "uz.camera.set_position(12.5, -4.0, 99.0)\n"
        "_rx, _ry, _rz = uz.camera.get_position()\n"
    ));

    EXPECT_NEAR(EvalNumber("_rx"),  12.5, 1e-3);
    EXPECT_NEAR(EvalNumber("_ry"),  -4.0, 1e-3);
    EXPECT_NEAR(EvalNumber("_rz"),  99.0, 1e-3);
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.camera.get_fov
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCameraAPITest, GetFov_ReturnsDefaultFov) {
    // CameraComponent.fov defaults to 60.f
    CreateActiveCamera();

    double fov = EvalNumber("uz.camera.get_fov()");

    EXPECT_NEAR(fov, 60.0, 1e-4);
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.camera.set_fov
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCameraAPITest, SetFov_ChangesActiveCameraFov) {
    auto& cam = CreateActiveCamera();

    EXPECT_TRUE(DoString("uz.camera.set_fov(90)"));

    EXPECT_NEAR(cam.fov, 90.f, 1e-4f);
}

TEST_F(LuaCameraAPITest, SetFov_RoundTripsViaGetFov) {
    CreateActiveCamera();

    EXPECT_TRUE(DoString("uz.camera.set_fov(75)"));
    double fov = EvalNumber("uz.camera.get_fov()");

    EXPECT_NEAR(fov, 75.0, 1e-4);
}

TEST_F(LuaCameraAPITest, SetFov_ClampsToOneWhenGivenZero) {
    auto& cam = CreateActiveCamera();

    EXPECT_TRUE(DoString("uz.camera.set_fov(0)"));

    // Implementation uses fmax(1, fmin(179, degrees)) — 0 clamps to 1
    EXPECT_NEAR(cam.fov, 1.f, 1e-4f);
}

TEST_F(LuaCameraAPITest, SetFov_ClampsToOneWhenGivenNegative) {
    auto& cam = CreateActiveCamera();

    EXPECT_TRUE(DoString("uz.camera.set_fov(-45)"));

    EXPECT_NEAR(cam.fov, 1.f, 1e-4f);
}

TEST_F(LuaCameraAPITest, SetFov_ClampsTo179WhenGiven180) {
    auto& cam = CreateActiveCamera();

    EXPECT_TRUE(DoString("uz.camera.set_fov(180)"));

    EXPECT_NEAR(cam.fov, 179.f, 1e-4f);
}

TEST_F(LuaCameraAPITest, SetFov_ClampsTo179WhenGivenVeryLargeValue) {
    auto& cam = CreateActiveCamera();

    EXPECT_TRUE(DoString("uz.camera.set_fov(999)"));

    EXPECT_NEAR(cam.fov, 179.f, 1e-4f);
}

TEST_F(LuaCameraAPITest, SetFov_AcceptsBoundaryValue1) {
    auto& cam = CreateActiveCamera();

    EXPECT_TRUE(DoString("uz.camera.set_fov(1)"));

    EXPECT_NEAR(cam.fov, 1.f, 1e-4f);
}

TEST_F(LuaCameraAPITest, SetFov_AcceptsBoundaryValue179) {
    auto& cam = CreateActiveCamera();

    EXPECT_TRUE(DoString("uz.camera.set_fov(179)"));

    EXPECT_NEAR(cam.fov, 179.f, 1e-4f);
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.camera.look_at
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCameraAPITest, LookAt_FacingForward_ProducesNearIdentityRotation) {
    // Camera at origin looking toward +Z (forward in left-handed space).
    // Expected: identity quaternion (0, 0, 0, 1).
    auto& cam = CreateActiveCamera({0.f, 0.f, 0.f});

    EXPECT_TRUE(DoString("uz.camera.look_at(0, 0, 1)"));

    const uz::Quat& rot = cam.GetRotation();
    // Identity quaternion: x=0, y=0, z=0, w=1
    EXPECT_NEAR(rot.x, 0.f, 1e-4f);
    EXPECT_NEAR(rot.y, 0.f, 1e-4f);
    EXPECT_NEAR(rot.z, 0.f, 1e-4f);
    EXPECT_NEAR(std::abs(rot.w), 1.f, 1e-4f);
}

TEST_F(LuaCameraAPITest, LookAt_FacingRight_ProducesNegative90DegYaw) {
    // Camera at origin looking toward +X (right).
    // In left-handed coords, rotating from +Z to +X is a -90 deg yaw around Y.
    // Quaternion: x=0, y=-sin(45deg)≈-0.7071, z=0, w=cos(45deg)≈0.7071
    auto& cam = CreateActiveCamera({0.f, 0.f, 0.f});

    EXPECT_TRUE(DoString("uz.camera.look_at(1, 0, 0)"));

    const uz::Quat& rot = cam.GetRotation();
    // The rotation must be a pure yaw around Y — x and z components near 0.
    EXPECT_NEAR(rot.x, 0.f, 1e-3f);
    EXPECT_NEAR(rot.z, 0.f, 1e-3f);
    // |w| and |y| must satisfy w^2 + y^2 ≈ 1 (unit quaternion)
    float len = std::sqrt(rot.x * rot.x + rot.y * rot.y + rot.z * rot.z + rot.w * rot.w);
    EXPECT_NEAR(len, 1.f, 1e-4f);
    // The y component must be non-zero — a yaw happened
    EXPECT_GT(std::abs(rot.y), 0.5f);
}

TEST_F(LuaCameraAPITest, LookAt_SamePositionAsTarget_DoesNotCrash_ReturnsIdentity) {
    // Camera at (5, 5, 5) looking at (5, 5, 5) — distance < 1e-6 edge case.
    // Implementation returns identity quaternion in this case.
    auto& cam = CreateActiveCamera({5.f, 5.f, 5.f});

    // Must not crash or error
    EXPECT_TRUE(DoString("uz.camera.look_at(5, 5, 5)"));

    const uz::Quat& rot = cam.GetRotation();
    // Identity quaternion (default after emplace on SceneComponent)
    float len = std::sqrt(rot.x * rot.x + rot.y * rot.y + rot.z * rot.z + rot.w * rot.w);
    EXPECT_NEAR(len, 1.f, 1e-4f);
}

// ─────────────────────────────────────────────────────────────────────────────
// No active camera — all functions return nil or are no-ops
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCameraAPITest, GetPosition_NoActiveCamera_ReturnsNil) {
    // World has no camera entity at all — FindActiveCamera returns nullptr.
    EXPECT_TRUE(EvalBool("uz.camera.get_position() == nil"));
}

TEST_F(LuaCameraAPITest, SetPosition_NoActiveCamera_IsNoOp_DoesNotCrash) {
    // No camera exists — set_position must silently do nothing.
    EXPECT_TRUE(DoString("uz.camera.set_position(1, 2, 3)"));
}

TEST_F(LuaCameraAPITest, GetFov_NoActiveCamera_ReturnsNil) {
    EXPECT_TRUE(EvalBool("uz.camera.get_fov() == nil"));
}

TEST_F(LuaCameraAPITest, SetFov_NoActiveCamera_IsNoOp_DoesNotCrash) {
    EXPECT_TRUE(DoString("uz.camera.set_fov(90)"));
}

TEST_F(LuaCameraAPITest, LookAt_NoActiveCamera_IsNoOp_DoesNotCrash) {
    EXPECT_TRUE(DoString("uz.camera.look_at(0, 0, 1)"));
}

// ─────────────────────────────────────────────────────────────────────────────
// No World set — all functions return nil or are no-ops
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCameraAPITest, GetPosition_NoWorldSet_ReturnsNil) {
    uz::LuaSetWorld(uz::LuaVM::GetState(), nullptr);
    EXPECT_TRUE(EvalBool("uz.camera.get_position() == nil"));
}

TEST_F(LuaCameraAPITest, SetPosition_NoWorldSet_IsNoOp_DoesNotCrash) {
    uz::LuaSetWorld(uz::LuaVM::GetState(), nullptr);
    EXPECT_TRUE(DoString("uz.camera.set_position(1, 2, 3)"));
}

TEST_F(LuaCameraAPITest, GetFov_NoWorldSet_ReturnsNil) {
    uz::LuaSetWorld(uz::LuaVM::GetState(), nullptr);
    EXPECT_TRUE(EvalBool("uz.camera.get_fov() == nil"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Wrong argument count / type errors
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCameraAPITest, SetFov_WrongArgCount_RaisesLuaError) {
    CreateActiveCamera();
    // set_fov requires exactly 1 number — passing none must raise a Lua error
    // (luaL_checknumber raises a Lua error, not a C++ exception)
    lua_State* L = uz::LuaVM::GetState();
    bool errored = luaL_dostring(L, "uz.camera.set_fov()") != 0;
    if (errored) lua_pop(L, 1); // pop error message
    EXPECT_TRUE(errored) << "Expected Lua error when set_fov() called with no args";
}

TEST_F(LuaCameraAPITest, SetPosition_WrongArgCount_RaisesLuaError) {
    CreateActiveCamera();
    // set_position requires 3 numbers — passing 1 must raise a Lua error
    lua_State* L = uz::LuaVM::GetState();
    bool errored = luaL_dostring(L, "uz.camera.set_position(1)") != 0;
    if (errored) lua_pop(L, 1);
    EXPECT_TRUE(errored) << "Expected Lua error when set_position() called with 1 arg";
}

TEST_F(LuaCameraAPITest, SetFov_WrongArgType_RaisesLuaError) {
    CreateActiveCamera();
    // set_fov with a string argument — luaL_checknumber will raise a Lua error
    lua_State* L = uz::LuaVM::GetState();
    bool errored = luaL_dostring(L, "uz.camera.set_fov('not_a_number')") != 0;
    if (errored) lua_pop(L, 1);
    EXPECT_TRUE(errored) << "Expected Lua error when set_fov() called with string arg";
}
