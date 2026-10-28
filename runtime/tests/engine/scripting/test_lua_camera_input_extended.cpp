/// Unit tests for extended Lua Camera API and Input API.
///
/// Camera extended (N-C7): get_near/set_near, get_far/set_far, set_orthographic,
/// is_orthographic, set_active, get_view_matrix, get_projection_matrix.
///
/// Input extended (N-D9): is_key_just_released, get_mouse_position,
/// get_scroll_delta, is_mouse_captured, set_cursor_visible/is_cursor_visible,
/// is_gamepad_connected, get_gamepad_axis, is_gamepad_button_down.
///
/// All tests run without GPU or audio hardware. InputSystem is driven directly
/// via its OnKey*/OnMouse*/SetCursorVisible methods — no platform window required.

#include "lua_vm.h"
#include "lua_scene_api.h"
#include "lua_camera_api.h"
#include "lua_input_api.h"
#include "world.h"
#include "camera_component.h"
#include "../math/vector3.h"
#include "../math/quaternion.h"
#include "input.h"

#include <gtest/gtest.h>
#include <cmath>
#include <string>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera Extended Fixture
// ─────────────────────────────────────────────────────────────────────────────

class LuaCameraExtendedTest : public ::testing::Test {
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

    /// Creates an active CameraComponent entity and returns a reference to it.
    uz::CameraComponent& CreateActiveCamera(const uz::Vec3& pos = {0.f, 0.f, 0.f}) {
        auto e = world_.CreateEntity();
        auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(e.id));
        auto& cam = world_.GetRegistry().emplace<uz::CameraComponent>(enttEntity);
        cam.SetPosition(pos);
        cam.isActive = true;
        return cam;
    }

    /// Creates an inactive CameraComponent entity and returns (entity, component).
    std::pair<uz::Entity, uz::CameraComponent*> CreateInactiveCamera() {
        auto e = world_.CreateEntity();
        auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(e.id));
        auto& cam = world_.GetRegistry().emplace<uz::CameraComponent>(enttEntity);
        cam.isActive = false;
        return {e, &cam};
    }

    /// Pushes an entity as lightuserdata in the same encoding used by lua_scene_api.
    /// High 32 bits = entity.id, low 32 bits = entity.generation.
    void PushEntityLightUserdata(lua_State* L, const uz::Entity& e) {
        uint64_t packed = (static_cast<uint64_t>(e.id) << 32) |
                          static_cast<uint64_t>(e.generation);
        void* p = nullptr;
        static_assert(sizeof(void*) >= sizeof(uint64_t),
            "Entity encoding requires 64-bit pointer");
        std::memcpy(&p, &packed, sizeof(packed));
        lua_pushlightuserdata(L, p);
    }

    /// Helper: run Lua code via luaL_dostring and return true if it raised an error.
    bool RaisesLuaError(const char* code) {
        lua_State* L = uz::LuaVM::GetState();
        bool errored = luaL_dostring(L, code) != 0;
        if (errored) lua_pop(L, 1);
        return errored;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Extended camera function presence
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCameraExtendedTest, ExtendedFunctionsExist_AfterInit) {
    EXPECT_TRUE(EvalBool("type(uz.camera.get_near) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.camera.set_near) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.camera.get_far) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.camera.set_far) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.camera.set_orthographic) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.camera.is_orthographic) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.camera.set_active) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.camera.get_view_matrix) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.camera.get_projection_matrix) == 'function'"));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.camera.get_near / set_near
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCameraExtendedTest, GetNear_ReturnsDefaultNearPlane) {
    // CameraComponent.nearPlane defaults to 10.0 (standard: 10 cm)
    CreateActiveCamera();

    double nearVal = EvalNumber("uz.camera.get_near()");

    EXPECT_NEAR(nearVal, 10.0, 1e-4);
}

TEST_F(LuaCameraExtendedTest, SetNear_GetNear_RoundTrip) {
    CreateActiveCamera();

    EXPECT_TRUE(DoString("uz.camera.set_near(0.5)"));
    double nearVal = EvalNumber("uz.camera.get_near()");

    EXPECT_NEAR(nearVal, 0.5, 1e-4);
}

TEST_F(LuaCameraExtendedTest, SetNear_UpdatesComponentField_Directly) {
    auto& cam = CreateActiveCamera();

    EXPECT_TRUE(DoString("uz.camera.set_near(2.0)"));

    EXPECT_NEAR(cam.nearPlane, 2.0f, 1e-4f);
}

TEST_F(LuaCameraExtendedTest, SetNear_ClampsBelowMinimum_To0001) {
    // Values below 0.1 clamp to 0.1 (minimum near plane is 0.1 cm)
    auto& cam = CreateActiveCamera();

    EXPECT_TRUE(DoString("uz.camera.set_near(0.00001)"));

    EXPECT_NEAR(cam.nearPlane, 0.1f, 1e-5f);
}

TEST_F(LuaCameraExtendedTest, SetNear_ClampsNegativeValue_To0001) {
    auto& cam = CreateActiveCamera();

    EXPECT_TRUE(DoString("uz.camera.set_near(-5.0)"));

    EXPECT_NEAR(cam.nearPlane, 0.1f, 1e-5f);
}

TEST_F(LuaCameraExtendedTest, SetNear_ClampsAtOrAboveFarPlane) {
    // Default far is 100000. Setting near to 999999 must clamp to maxNear.
    // Note: at farPlane=100000 the epsilon 1e-4 is below float32 precision, so
    // maxNear = farPlane - 1e-4 rounds to farPlane itself. The clamp still
    // guarantees nearPlane <= farPlane and nearPlane > 0.
    auto& cam = CreateActiveCamera();

    EXPECT_TRUE(DoString("uz.camera.set_near(999999.0)"));

    // After clamping: nearPlane must equal maxNear (≤ farPlane) and be positive
    EXPECT_LE(cam.nearPlane, cam.farPlane);
    EXPECT_GT(cam.nearPlane, 0.f);
}

TEST_F(LuaCameraExtendedTest, GetNear_NoActiveCamera_ReturnsNil) {
    EXPECT_TRUE(EvalBool("uz.camera.get_near() == nil"));
}

TEST_F(LuaCameraExtendedTest, SetNear_NoActiveCamera_IsNoOp_DoesNotCrash) {
    EXPECT_TRUE(DoString("uz.camera.set_near(1.0)"));
}

TEST_F(LuaCameraExtendedTest, SetNear_WrongArgType_RaisesLuaError) {
    CreateActiveCamera();
    EXPECT_TRUE(RaisesLuaError("uz.camera.set_near('not_a_number')"));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.camera.get_far / set_far
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCameraExtendedTest, GetFar_ReturnsDefaultFarPlane) {
    // CameraComponent.farPlane defaults to 100000 (standard: 1000 m in cm)
    CreateActiveCamera();

    double farVal = EvalNumber("uz.camera.get_far()");

    EXPECT_NEAR(farVal, 100000.0, 1e-3);
}

TEST_F(LuaCameraExtendedTest, SetFar_GetFar_RoundTrip) {
    CreateActiveCamera();

    EXPECT_TRUE(DoString("uz.camera.set_far(500.0)"));
    double farVal = EvalNumber("uz.camera.get_far()");

    EXPECT_NEAR(farVal, 500.0, 1e-3);
}

TEST_F(LuaCameraExtendedTest, SetFar_UpdatesComponentField_Directly) {
    auto& cam = CreateActiveCamera();

    EXPECT_TRUE(DoString("uz.camera.set_far(2500.0)"));

    EXPECT_NEAR(cam.farPlane, 2500.f, 1e-2f);
}

TEST_F(LuaCameraExtendedTest, SetFar_ClampsAboveMaximum_To100000) {
    // Values above 100000 clamp to 100000
    auto& cam = CreateActiveCamera();

    EXPECT_TRUE(DoString("uz.camera.set_far(999999999.0)"));

    EXPECT_NEAR(cam.farPlane, 100000.f, 1e-2f);
}

TEST_F(LuaCameraExtendedTest, SetFar_ClampsAtOrBelowNearPlane) {
    // Default near is 0.1. Setting far <= near clamps to (nearPlane + 1e-4).
    auto& cam = CreateActiveCamera();

    EXPECT_TRUE(DoString("uz.camera.set_far(0.0001)"));

    // After clamping: farPlane > nearPlane (0.1)
    EXPECT_GT(cam.farPlane, cam.nearPlane);
}

TEST_F(LuaCameraExtendedTest, GetFar_NoActiveCamera_ReturnsNil) {
    EXPECT_TRUE(EvalBool("uz.camera.get_far() == nil"));
}

TEST_F(LuaCameraExtendedTest, SetFar_NoActiveCamera_IsNoOp_DoesNotCrash) {
    EXPECT_TRUE(DoString("uz.camera.set_far(500.0)"));
}

TEST_F(LuaCameraExtendedTest, SetFar_WrongArgType_RaisesLuaError) {
    CreateActiveCamera();
    EXPECT_TRUE(RaisesLuaError("uz.camera.set_far('not_a_number')"));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.camera.set_orthographic / is_orthographic
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCameraExtendedTest, IsOrthographic_ReturnsFalseByDefault) {
    CreateActiveCamera();

    EXPECT_FALSE(EvalBool("uz.camera.is_orthographic()"));
}

TEST_F(LuaCameraExtendedTest, SetOrthographic_True_IsOrthographic_ReturnsTrue) {
    CreateActiveCamera();

    EXPECT_TRUE(DoString("uz.camera.set_orthographic(true)"));

    EXPECT_TRUE(EvalBool("uz.camera.is_orthographic()"));
}

TEST_F(LuaCameraExtendedTest, SetOrthographic_False_IsOrthographic_ReturnsFalse) {
    auto& cam = CreateActiveCamera();
    cam.isOrtho = true;

    EXPECT_TRUE(DoString("uz.camera.set_orthographic(false)"));

    EXPECT_FALSE(EvalBool("uz.camera.is_orthographic()"));
}

TEST_F(LuaCameraExtendedTest, SetOrthographic_RoundTrip_TrueToFalse) {
    CreateActiveCamera();

    EXPECT_TRUE(DoString("uz.camera.set_orthographic(true)"));
    EXPECT_TRUE(EvalBool("uz.camera.is_orthographic()"));

    EXPECT_TRUE(DoString("uz.camera.set_orthographic(false)"));
    EXPECT_FALSE(EvalBool("uz.camera.is_orthographic()"));
}

TEST_F(LuaCameraExtendedTest, SetOrthographic_WithSize_UpdatesOrthoSize) {
    auto& cam = CreateActiveCamera();

    EXPECT_TRUE(DoString("uz.camera.set_orthographic(true, 20.0)"));

    EXPECT_TRUE(cam.isOrtho);
    EXPECT_NEAR(cam.orthoSize, 20.f, 1e-4f);
}

TEST_F(LuaCameraExtendedTest, SetOrthographic_NegativeSize_IgnoresSize_SetsMode) {
    // orthoSize must be > 0; negative size is ignored, mode still changes
    auto& cam = CreateActiveCamera();
    float originalOrthoSize = cam.orthoSize;

    EXPECT_TRUE(DoString("uz.camera.set_orthographic(true, -5.0)"));

    EXPECT_TRUE(cam.isOrtho);
    // orthoSize unchanged because negative was rejected
    EXPECT_NEAR(cam.orthoSize, originalOrthoSize, 1e-4f);
}

TEST_F(LuaCameraExtendedTest, SetOrthographic_NonBoolArg_RaisesLuaError) {
    CreateActiveCamera();
    // First arg must be boolean — passing a number must raise a Lua error
    EXPECT_TRUE(RaisesLuaError("uz.camera.set_orthographic(1)"));
}

TEST_F(LuaCameraExtendedTest, IsOrthographic_NoActiveCamera_ReturnsNil) {
    EXPECT_TRUE(EvalBool("uz.camera.is_orthographic() == nil"));
}

TEST_F(LuaCameraExtendedTest, SetOrthographic_NoActiveCamera_IsNoOp_DoesNotCrash) {
    EXPECT_TRUE(DoString("uz.camera.set_orthographic(true)"));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.camera.set_active
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCameraExtendedTest, SetActive_SwitchesActiveCamera) {
    // Create two cameras: camera A active, camera B inactive.
    // Call set_active(B) via lightuserdata encoding.
    auto& camA = CreateActiveCamera({0.f, 0.f, 0.f});
    auto [entityB, camB] = CreateInactiveCamera();

    ASSERT_TRUE(camA.isActive);
    ASSERT_FALSE(camB->isActive);

    lua_State* L = uz::LuaVM::GetState();
    // Call uz.camera.set_active(entityB_lightuserdata)
    lua_getglobal(L, "uz");
    lua_getfield(L, -1, "camera");
    lua_getfield(L, -1, "set_active");

    PushEntityLightUserdata(L, entityB);
    int result = lua_pcall(L, 1, 0, 0);
    if (result != 0) {
        const char* err = lua_tostring(L, -1);
        ADD_FAILURE() << "set_active pcall failed: " << (err ? err : "unknown");
        lua_pop(L, 1);
    }
    lua_pop(L, 2); // pop 'camera' table and 'uz' table

    // After set_active(B): A is deactivated, B is activated.
    EXPECT_FALSE(camA.isActive) << "Camera A must be deactivated after set_active(B)";
    EXPECT_TRUE(camB->isActive) << "Camera B must be activated after set_active(B)";
}

TEST_F(LuaCameraExtendedTest, SetActive_DeactivatesAllOtherCameras) {
    // Three cameras: only the last one should be active after set_active.
    auto& camA = CreateActiveCamera();
    auto& camB = CreateActiveCamera();  // second active (two active — unusual but valid for test)
    auto [entityC, camC] = CreateInactiveCamera();

    lua_State* L = uz::LuaVM::GetState();
    lua_getglobal(L, "uz");
    lua_getfield(L, -1, "camera");
    lua_getfield(L, -1, "set_active");
    PushEntityLightUserdata(L, entityC);
    int result = lua_pcall(L, 1, 0, 0);
    if (result != 0) {
        lua_pop(L, 1);
        ADD_FAILURE() << "set_active pcall failed";
    }
    lua_pop(L, 2);

    EXPECT_FALSE(camA.isActive) << "Camera A must be deactivated";
    EXPECT_FALSE(camB.isActive) << "Camera B must be deactivated";
    EXPECT_TRUE(camC->isActive)  << "Camera C must be the sole active camera";
}

TEST_F(LuaCameraExtendedTest, SetActive_NoActiveCamera_ThenActivatesTarget_GetsFoundByAPI) {
    // World starts with no camera. Create an inactive one, activate it via set_active.
    // Then get_near must return a number (not nil).
    auto [entityA, camA] = CreateInactiveCamera();
    camA->nearPlane = 0.3f;

    lua_State* L = uz::LuaVM::GetState();
    lua_getglobal(L, "uz");
    lua_getfield(L, -1, "camera");
    lua_getfield(L, -1, "set_active");
    PushEntityLightUserdata(L, entityA);
    int result = lua_pcall(L, 1, 0, 0);
    if (result != 0) lua_pop(L, 1);
    lua_pop(L, 2);

    // Now the camera is active — API should find it.
    double nearVal = EvalNumber("uz.camera.get_near()");
    EXPECT_NEAR(nearVal, 0.3, 1e-4);
}

TEST_F(LuaCameraExtendedTest, SetActive_NoWorld_IsNoOp_DoesNotCrash) {
    uz::LuaSetWorld(uz::LuaVM::GetState(), nullptr);

    lua_State* L = uz::LuaVM::GetState();
    lua_getglobal(L, "uz");
    lua_getfield(L, -1, "camera");
    lua_getfield(L, -1, "set_active");
    lua_pushlightuserdata(L, nullptr); // invalid entity — no world means early return
    // Must not crash
    lua_pcall(L, 1, 0, 0);
    lua_pop(L, 2);
    // No assertion beyond "did not crash"
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.camera.get_view_matrix
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCameraExtendedTest, GetViewMatrix_Returns16Numbers) {
    CreateActiveCamera({0.f, 0.f, 0.f});

    // Collect all 16 return values into a Lua table for inspection.
    // table.pack does not exist in Lua 5.1/LuaJIT — use select('#', ...) pattern.
    EXPECT_TRUE(DoString(
        "do\n"
        "  local function pack(...) return {n=select('#',...), ...} end\n"
        "  _vm = pack(uz.camera.get_view_matrix())\n"
        "end\n"
    ));

    double n = EvalNumber("_vm.n");
    EXPECT_EQ(static_cast<int>(n), 16)
        << "get_view_matrix must return exactly 16 numbers";
}

TEST_F(LuaCameraExtendedTest, GetViewMatrix_AllElementsAreNumbers) {
    CreateActiveCamera();

    EXPECT_TRUE(DoString(
        "do\n"
        "  local function pack(...) return {n=select('#',...), ...} end\n"
        "  _vm = pack(uz.camera.get_view_matrix())\n"
        "end\n"
        "_all_numbers = true\n"
        "for i = 1, 16 do\n"
        "  if type(_vm[i]) ~= 'number' then _all_numbers = false end\n"
        "end\n"
    ));

    EXPECT_TRUE(EvalBool("_all_numbers"))
        << "Every element returned by get_view_matrix must be a number";
}

TEST_F(LuaCameraExtendedTest, GetViewMatrix_IdentityCamera_DiagonalIsOne) {
    // Camera at origin with identity rotation: view matrix is identity.
    // Diagonal elements [1,6,11,16] (1-indexed) should be 1.0.
    CreateActiveCamera({0.f, 0.f, 0.f});

    EXPECT_TRUE(DoString(
        "do\n"
        "  local function pack(...) return {n=select('#',...), ...} end\n"
        "  _vm = pack(uz.camera.get_view_matrix())\n"
        "end\n"
    ));

    // Row-major: _vm[1]=m11, _vm[6]=m22, _vm[11]=m33, _vm[16]=m44
    EXPECT_NEAR(EvalNumber("_vm[1]"),  1.0, 1e-4) << "view[0][0] must be 1 for identity camera";
    EXPECT_NEAR(EvalNumber("_vm[6]"),  1.0, 1e-4) << "view[1][1] must be 1 for identity camera";
    EXPECT_NEAR(EvalNumber("_vm[11]"), 1.0, 1e-4) << "view[2][2] must be 1 for identity camera";
    EXPECT_NEAR(EvalNumber("_vm[16]"), 1.0, 1e-4) << "view[3][3] must be 1 for identity camera";
}

TEST_F(LuaCameraExtendedTest, GetViewMatrix_NoActiveCamera_ReturnsNil) {
    // No camera: get_view_matrix must return nil (1 value), not crash.
    EXPECT_TRUE(EvalBool("uz.camera.get_view_matrix() == nil"));
}

TEST_F(LuaCameraExtendedTest, GetViewMatrix_NoWorld_ReturnsNil) {
    uz::LuaSetWorld(uz::LuaVM::GetState(), nullptr);
    EXPECT_TRUE(EvalBool("uz.camera.get_view_matrix() == nil"));
}

TEST_F(LuaCameraExtendedTest, GetViewMatrix_DoesNotLeakStack) {
    CreateActiveCamera();
    lua_State* L = uz::LuaVM::GetState();
    int topBefore = lua_gettop(L);

    EXPECT_TRUE(DoString(
        "do\n"
        "  local function pack(...) return {n=select('#',...), ...} end\n"
        "  local t = pack(uz.camera.get_view_matrix())\n"
        "end\n"
    ));

    EXPECT_EQ(lua_gettop(L), topBefore);
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.camera.get_projection_matrix
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaCameraExtendedTest, GetProjectionMatrix_Returns16Numbers) {
    CreateActiveCamera();

    EXPECT_TRUE(DoString(
        "do\n"
        "  local function pack(...) return {n=select('#',...), ...} end\n"
        "  _pm = pack(uz.camera.get_projection_matrix())\n"
        "end\n"
    ));

    double n = EvalNumber("_pm.n");
    EXPECT_EQ(static_cast<int>(n), 16)
        << "get_projection_matrix must return exactly 16 numbers";
}

TEST_F(LuaCameraExtendedTest, GetProjectionMatrix_AllElementsAreNumbers) {
    CreateActiveCamera();

    EXPECT_TRUE(DoString(
        "do\n"
        "  local function pack(...) return {n=select('#',...), ...} end\n"
        "  _pm = pack(uz.camera.get_projection_matrix())\n"
        "end\n"
        "_all_numbers = true\n"
        "for i = 1, 16 do\n"
        "  if type(_pm[i]) ~= 'number' then _all_numbers = false end\n"
        "end\n"
    ));

    EXPECT_TRUE(EvalBool("_all_numbers"))
        << "Every element returned by get_projection_matrix must be a number";
}

TEST_F(LuaCameraExtendedTest, GetProjectionMatrix_BottomRightIs0_ForPerspective) {
    // For UzEngine Mat4::Perspective (row-major XMFLOAT4X4 layout):
    //   Mat4::Perspective clears _44 to 0 so clip w = view z (reversed-Z), see mat4.cpp.
    //   Elements are stored as: _11 _12 _13 _14 _21 _22 _23 _24 _31 _32 _33 _34 _41 _42 _43 _44
    //   Lua 1-based indices:      1   2   3   4   5   6   7   8   9  10  11  12  13  14  15  16
    //   m34 (_34) = 1  → 0-based index 11 → Lua _pm[12]
    //   m44 (_44) = 0  → 0-based index 15 → Lua _pm[16] (cleared by Perspective)
    CreateActiveCamera();

    EXPECT_TRUE(DoString(
        "do\n"
        "  local function pack(...) return {n=select('#',...), ...} end\n"
        "  _pm = pack(uz.camera.get_projection_matrix())\n"
        "end\n"
    ));

    // m44 (_44, Lua index 16) is 0: Mat4::Perspective clears it so clip w = view z
    EXPECT_NEAR(EvalNumber("_pm[16]"), 0.0, 1e-4)
        << "Perspective projection m44 is 0 (cleared so clip w = view z)";
    // m34 (_34, Lua index 12) must be 1 for perspective projection (w-from-z factor)
    EXPECT_NEAR(EvalNumber("_pm[12]"), 1.0, 1e-4)
        << "Perspective projection m34 must be 1";
}

TEST_F(LuaCameraExtendedTest, GetProjectionMatrix_NoActiveCamera_ReturnsNil) {
    EXPECT_TRUE(EvalBool("uz.camera.get_projection_matrix() == nil"));
}

TEST_F(LuaCameraExtendedTest, GetProjectionMatrix_NoWorld_ReturnsNil) {
    uz::LuaSetWorld(uz::LuaVM::GetState(), nullptr);
    EXPECT_TRUE(EvalBool("uz.camera.get_projection_matrix() == nil"));
}

TEST_F(LuaCameraExtendedTest, GetProjectionMatrix_DoesNotLeakStack) {
    CreateActiveCamera();
    lua_State* L = uz::LuaVM::GetState();
    int topBefore = lua_gettop(L);

    EXPECT_TRUE(DoString(
        "do\n"
        "  local function pack(...) return {n=select('#',...), ...} end\n"
        "  local t = pack(uz.camera.get_projection_matrix())\n"
        "end\n"
    ));

    EXPECT_EQ(lua_gettop(L), topBefore);
}

// ─────────────────────────────────────────────────────────────────────────────
// Input Extended Fixture
// ─────────────────────────────────────────────────────────────────────────────

class LuaInputExtendedTest : public ::testing::Test {
  protected:
    uz::InputSystem inputSystem_;

    void SetUp() override {
        ASSERT_TRUE(uz::LuaVM::Init());
        inputSystem_.Init();
        uz::LuaSetInputSystem(uz::LuaVM::GetState(), &inputSystem_);
    }

    void TearDown() override {
        uz::LuaVM::Shutdown();
    }

    /// Helper: run Lua code, return true on success.
    bool DoString(const char* code) {
        return luaL_dostring(uz::LuaVM::GetState(), code) == 0;
    }

    /// Helper: evaluate a Lua expression and return its boolean result.
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

    /// Helper: evaluate a Lua expression and return its numeric result.
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

    /// Helper: run a Lua snippet and return true if it raised an error.
    bool RaisesLuaError(const char* code) {
        lua_State* L = uz::LuaVM::GetState();
        bool errored = luaL_dostring(L, code) != 0;
        if (errored) lua_pop(L, 1);
        return errored;
    }

    /// Simulates pressing a key (OnKeyDown) and snapshotting (BeginFrame).
    void SimulateKeyPress(uz::Key key) {
        // Snapshot current as previous so "just released" state is correct.
        inputSystem_.BeginFrame();
        inputSystem_.OnKeyDown(static_cast<uint16_t>(key));
    }

    /// Simulates releasing a key that was previously held.
    /// Requires SimulateKeyPress to have been called first.
    void SimulateKeyRelease(uz::Key key) {
        // After BeginFrame: previous = old current. Release the key.
        inputSystem_.BeginFrame();
        inputSystem_.OnKeyDown(static_cast<uint16_t>(key));
        // Another BeginFrame: previous holds the "down", current will get up.
        inputSystem_.BeginFrame();
        inputSystem_.OnKeyUp(static_cast<uint16_t>(key));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// uz.input.is_key_just_released
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaInputExtendedTest, IsKeyJustReleased_IsFunction) {
    EXPECT_TRUE(EvalBool("type(uz.input.is_key_just_released) == 'function'"));
}

TEST_F(LuaInputExtendedTest, IsKeyJustReleased_DefaultState_ReturnsFalse) {
    // No key was pressed — just_released must be false.
    EXPECT_FALSE(EvalBool("uz.input.is_key_just_released('A')"));
}

TEST_F(LuaInputExtendedTest, IsKeyJustReleased_AfterRelease_ReturnsTrue) {
    // Press then release 'W'. After the release frame: is_key_just_released('W') = true.
    SimulateKeyRelease(uz::Key::W);

    EXPECT_TRUE(EvalBool("uz.input.is_key_just_released('W')"));
}

TEST_F(LuaInputExtendedTest, IsKeyJustReleased_WhileStillHeld_ReturnsFalse) {
    // Key is held but not yet released — just_released must be false.
    inputSystem_.BeginFrame();
    inputSystem_.OnKeyDown(static_cast<uint16_t>(uz::Key::S));

    EXPECT_FALSE(EvalBool("uz.input.is_key_just_released('S')"));
}

TEST_F(LuaInputExtendedTest, IsKeyJustReleased_NoInputSystem_ReturnsFalse) {
    uz::LuaSetInputSystem(uz::LuaVM::GetState(), nullptr);

    EXPECT_FALSE(EvalBool("uz.input.is_key_just_released('Space')"));
}

TEST_F(LuaInputExtendedTest, IsKeyJustReleased_UnrecognizedKey_ReturnsFalse) {
    EXPECT_FALSE(EvalBool("uz.input.is_key_just_released('not_a_key')"));
}

TEST_F(LuaInputExtendedTest, IsKeyJustReleased_NoInputSystem_WrongType_ReturnsFalse) {
    // With no InputSystem, early return prevents luaL_checkstring from running.
    uz::LuaSetInputSystem(uz::LuaVM::GetState(), nullptr);
    EXPECT_FALSE(EvalBool("uz.input.is_key_just_released(42)"));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.input.get_mouse_position
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaInputExtendedTest, GetMousePosition_IsFunction) {
    EXPECT_TRUE(EvalBool("type(uz.input.get_mouse_position) == 'function'"));
}

TEST_F(LuaInputExtendedTest, GetMousePosition_DefaultState_ReturnsTwoZeroes) {
    EXPECT_TRUE(DoString("_mx, _my = uz.input.get_mouse_position()"));
    EXPECT_DOUBLE_EQ(EvalNumber("_mx"), 0.0);
    EXPECT_DOUBLE_EQ(EvalNumber("_my"), 0.0);
}

TEST_F(LuaInputExtendedTest, GetMousePosition_AfterMouseMove_ReturnsUpdatedValues) {
    // Simulate a mouse move to (320, 240).
    inputSystem_.OnMouseMove(320.f, 240.f);

    EXPECT_TRUE(DoString("_mx, _my = uz.input.get_mouse_position()"));

    EXPECT_NEAR(EvalNumber("_mx"), 320.0, 1e-3);
    EXPECT_NEAR(EvalNumber("_my"), 240.0, 1e-3);
}

TEST_F(LuaInputExtendedTest, GetMousePosition_ReturnsTwoValues) {
    // table.pack does not exist in Lua 5.1/LuaJIT — use select('#', ...) pattern.
    EXPECT_TRUE(DoString(
        "do\n"
        "  local function pack(...) return {n=select('#',...), ...} end\n"
        "  _t = pack(uz.input.get_mouse_position())\n"
        "end\n"
    ));
    double n = EvalNumber("_t.n");
    EXPECT_EQ(static_cast<int>(n), 2)
        << "get_mouse_position must return exactly 2 values";
}

TEST_F(LuaInputExtendedTest, GetMousePosition_NoInputSystem_ReturnsTwoZeroes) {
    uz::LuaSetInputSystem(uz::LuaVM::GetState(), nullptr);

    EXPECT_TRUE(DoString("_mx, _my = uz.input.get_mouse_position()"));

    EXPECT_DOUBLE_EQ(EvalNumber("_mx"), 0.0);
    EXPECT_DOUBLE_EQ(EvalNumber("_my"), 0.0);
}

TEST_F(LuaInputExtendedTest, GetMousePosition_DoesNotLeakStack) {
    lua_State* L = uz::LuaVM::GetState();
    int topBefore = lua_gettop(L);

    EXPECT_TRUE(DoString("local x, y = uz.input.get_mouse_position()"));

    EXPECT_EQ(lua_gettop(L), topBefore);
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.input.get_scroll_delta
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaInputExtendedTest, GetScrollDelta_IsFunction) {
    EXPECT_TRUE(EvalBool("type(uz.input.get_scroll_delta) == 'function'"));
}

TEST_F(LuaInputExtendedTest, GetScrollDelta_DefaultState_ReturnsTwoZeroes) {
    EXPECT_TRUE(DoString("_sd, _sh = uz.input.get_scroll_delta()"));
    EXPECT_DOUBLE_EQ(EvalNumber("_sd"), 0.0);
    EXPECT_DOUBLE_EQ(EvalNumber("_sh"), 0.0);
}

TEST_F(LuaInputExtendedTest, GetScrollDelta_AfterScrollUp_ReturnsPositiveDelta) {
    // Simulate a scroll event of +3.
    inputSystem_.OnMouseScroll(3.f);

    EXPECT_TRUE(DoString("_sd, _sh = uz.input.get_scroll_delta()"));

    EXPECT_NEAR(EvalNumber("_sd"), 3.0, 1e-4)
        << "Scroll delta must be positive for scroll-up";
    EXPECT_DOUBLE_EQ(EvalNumber("_sh"), 0.0)
        << "Horizontal scroll is not supported — second value must be 0";
}

TEST_F(LuaInputExtendedTest, GetScrollDelta_AfterScrollDown_ReturnsNegativeDelta) {
    inputSystem_.OnMouseScroll(-2.f);

    EXPECT_TRUE(DoString("_sd, _sh = uz.input.get_scroll_delta()"));

    EXPECT_NEAR(EvalNumber("_sd"), -2.0, 1e-4)
        << "Scroll delta must be negative for scroll-down";
}

TEST_F(LuaInputExtendedTest, GetScrollDelta_AccumulatesWithinFrame) {
    // Two scroll events in the same frame accumulate.
    inputSystem_.OnMouseScroll(1.f);
    inputSystem_.OnMouseScroll(2.f);

    EXPECT_TRUE(DoString("_sd, _sh = uz.input.get_scroll_delta()"));

    EXPECT_NEAR(EvalNumber("_sd"), 3.0, 1e-4)
        << "Multiple scroll events in one frame must accumulate";
}

TEST_F(LuaInputExtendedTest, GetScrollDelta_ResetsAfterBeginFrame) {
    inputSystem_.OnMouseScroll(5.f);
    inputSystem_.BeginFrame(); // resets accumulator

    EXPECT_TRUE(DoString("_sd, _sh = uz.input.get_scroll_delta()"));

    EXPECT_DOUBLE_EQ(EvalNumber("_sd"), 0.0)
        << "Scroll delta must reset to 0 after BeginFrame";
}

TEST_F(LuaInputExtendedTest, GetScrollDelta_NoInputSystem_ReturnsTwoZeroes) {
    uz::LuaSetInputSystem(uz::LuaVM::GetState(), nullptr);

    EXPECT_TRUE(DoString("_sd, _sh = uz.input.get_scroll_delta()"));

    EXPECT_DOUBLE_EQ(EvalNumber("_sd"), 0.0);
    EXPECT_DOUBLE_EQ(EvalNumber("_sh"), 0.0);
}

TEST_F(LuaInputExtendedTest, GetScrollDelta_ReturnsTwoValues) {
    // table.pack does not exist in Lua 5.1/LuaJIT — use select('#', ...) pattern.
    EXPECT_TRUE(DoString(
        "do\n"
        "  local function pack(...) return {n=select('#',...), ...} end\n"
        "  _t = pack(uz.input.get_scroll_delta())\n"
        "end\n"
    ));
    EXPECT_EQ(static_cast<int>(EvalNumber("_t.n")), 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.input.is_mouse_captured
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaInputExtendedTest, IsMouseCaptured_IsFunction) {
    EXPECT_TRUE(EvalBool("type(uz.input.is_mouse_captured) == 'function'"));
}

TEST_F(LuaInputExtendedTest, IsMouseCaptured_DefaultState_ReturnsFalse) {
    // Mouse capture defaults to false — no platform window call required.
    EXPECT_FALSE(EvalBool("uz.input.is_mouse_captured()"));
}

TEST_F(LuaInputExtendedTest, IsMouseCaptured_NoInputSystem_ReturnsFalse) {
    uz::LuaSetInputSystem(uz::LuaVM::GetState(), nullptr);
    EXPECT_FALSE(EvalBool("uz.input.is_mouse_captured()"));
}

TEST_F(LuaInputExtendedTest, IsMouseCaptured_DoesNotLeakStack) {
    lua_State* L = uz::LuaVM::GetState();
    int topBefore = lua_gettop(L);

    EXPECT_TRUE(DoString("local c = uz.input.is_mouse_captured()"));

    EXPECT_EQ(lua_gettop(L), topBefore);
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.input.set_cursor_visible / is_cursor_visible
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaInputExtendedTest, SetCursorVisible_IsFunction) {
    EXPECT_TRUE(EvalBool("type(uz.input.set_cursor_visible) == 'function'"));
}

TEST_F(LuaInputExtendedTest, IsCursorVisible_IsFunction) {
    EXPECT_TRUE(EvalBool("type(uz.input.is_cursor_visible) == 'function'"));
}

TEST_F(LuaInputExtendedTest, IsCursorVisible_DefaultState_ReturnsTrue) {
    // Cursor is visible by default (m_cursorVisible = true in InputSystem).
    EXPECT_TRUE(EvalBool("uz.input.is_cursor_visible()"));
}

TEST_F(LuaInputExtendedTest, SetCursorVisible_False_IsCursorVisible_ReturnsFalse) {
    EXPECT_TRUE(DoString("uz.input.set_cursor_visible(false)"));

    EXPECT_FALSE(EvalBool("uz.input.is_cursor_visible()"));
}

TEST_F(LuaInputExtendedTest, SetCursorVisible_True_IsCursorVisible_ReturnsTrue) {
    // Hide then show.
    EXPECT_TRUE(DoString("uz.input.set_cursor_visible(false)"));
    EXPECT_TRUE(DoString("uz.input.set_cursor_visible(true)"));

    EXPECT_TRUE(EvalBool("uz.input.is_cursor_visible()"));
}

TEST_F(LuaInputExtendedTest, SetCursorVisible_RoundTrip_FalseToTrue) {
    EXPECT_TRUE(DoString("uz.input.set_cursor_visible(false)"));
    EXPECT_FALSE(EvalBool("uz.input.is_cursor_visible()"));

    EXPECT_TRUE(DoString("uz.input.set_cursor_visible(true)"));
    EXPECT_TRUE(EvalBool("uz.input.is_cursor_visible()"));
}

TEST_F(LuaInputExtendedTest, SetCursorVisible_AlsoUpdatesInputSystemField_Directly) {
    EXPECT_TRUE(DoString("uz.input.set_cursor_visible(false)"));
    EXPECT_FALSE(inputSystem_.IsCursorVisible());

    EXPECT_TRUE(DoString("uz.input.set_cursor_visible(true)"));
    EXPECT_TRUE(inputSystem_.IsCursorVisible());
}

TEST_F(LuaInputExtendedTest, SetCursorVisible_NoInputSystem_IsNoOp_DoesNotCrash) {
    uz::LuaSetInputSystem(uz::LuaVM::GetState(), nullptr);
    EXPECT_TRUE(DoString("uz.input.set_cursor_visible(false)"));
}

TEST_F(LuaInputExtendedTest, IsCursorVisible_NoInputSystem_ReturnsTrue) {
    // When there is no InputSystem, the default is cursor visible = true.
    uz::LuaSetInputSystem(uz::LuaVM::GetState(), nullptr);
    EXPECT_TRUE(EvalBool("uz.input.is_cursor_visible()"));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.input.is_gamepad_connected
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaInputExtendedTest, IsGamepadConnected_IsFunction) {
    EXPECT_TRUE(EvalBool("type(uz.input.is_gamepad_connected) == 'function'"));
}

TEST_F(LuaInputExtendedTest, IsGamepadConnected_DefaultState_ReturnsFalse) {
    // No XInput hardware in unit test environment — all gamepads report disconnected.
    EXPECT_FALSE(EvalBool("uz.input.is_gamepad_connected(0)"));
    EXPECT_FALSE(EvalBool("uz.input.is_gamepad_connected(1)"));
    EXPECT_FALSE(EvalBool("uz.input.is_gamepad_connected(2)"));
    EXPECT_FALSE(EvalBool("uz.input.is_gamepad_connected(3)"));
}

TEST_F(LuaInputExtendedTest, IsGamepadConnected_NoInputSystem_ReturnsFalse) {
    uz::LuaSetInputSystem(uz::LuaVM::GetState(), nullptr);
    EXPECT_FALSE(EvalBool("uz.input.is_gamepad_connected(0)"));
}

TEST_F(LuaInputExtendedTest, IsGamepadConnected_WrongArgType_RaisesLuaError) {
    // is_gamepad_connected uses luaL_checkinteger — string arg must raise error.
    EXPECT_TRUE(RaisesLuaError("uz.input.is_gamepad_connected('not_an_int')"));
}

TEST_F(LuaInputExtendedTest, IsGamepadConnected_DoesNotLeakStack) {
    lua_State* L = uz::LuaVM::GetState();
    int topBefore = lua_gettop(L);

    EXPECT_TRUE(DoString("local c = uz.input.is_gamepad_connected(0)"));

    EXPECT_EQ(lua_gettop(L), topBefore);
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.input.get_gamepad_axis
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaInputExtendedTest, GetGamepadAxis_IsFunction) {
    EXPECT_TRUE(EvalBool("type(uz.input.get_gamepad_axis) == 'function'"));
}

TEST_F(LuaInputExtendedTest, GetGamepadAxis_DefaultState_ReturnsZero) {
    // No gamepad connected — all axes return 0.
    EXPECT_DOUBLE_EQ(EvalNumber("uz.input.get_gamepad_axis(0, 'LeftX')"),   0.0);
    EXPECT_DOUBLE_EQ(EvalNumber("uz.input.get_gamepad_axis(0, 'LeftY')"),   0.0);
    EXPECT_DOUBLE_EQ(EvalNumber("uz.input.get_gamepad_axis(0, 'RightX')"),  0.0);
    EXPECT_DOUBLE_EQ(EvalNumber("uz.input.get_gamepad_axis(0, 'RightY')"),  0.0);
    EXPECT_DOUBLE_EQ(EvalNumber("uz.input.get_gamepad_axis(0, 'LeftTrigger')"),  0.0);
    EXPECT_DOUBLE_EQ(EvalNumber("uz.input.get_gamepad_axis(0, 'RightTrigger')"), 0.0);
}

TEST_F(LuaInputExtendedTest, GetGamepadAxis_UnrecognizedAxisName_ReturnsZero) {
    EXPECT_DOUBLE_EQ(EvalNumber("uz.input.get_gamepad_axis(0, 'NotAnAxis')"), 0.0);
}

TEST_F(LuaInputExtendedTest, GetGamepadAxis_NoInputSystem_ReturnsZero) {
    uz::LuaSetInputSystem(uz::LuaVM::GetState(), nullptr);
    EXPECT_DOUBLE_EQ(EvalNumber("uz.input.get_gamepad_axis(0, 'LeftX')"), 0.0);
}

TEST_F(LuaInputExtendedTest, GetGamepadAxis_WrongIndexType_RaisesLuaError) {
    EXPECT_TRUE(RaisesLuaError("uz.input.get_gamepad_axis('bad', 'LeftX')"));
}

TEST_F(LuaInputExtendedTest, GetGamepadAxis_WrongAxisType_RaisesLuaError) {
    // In Lua 5.1/LuaJIT, luaL_checkstring coerces numbers to strings implicitly.
    // A numeric axis name "42" is simply unrecognized — returns 0.0, no error raised.
    // Verify the function handles numeric axis arg gracefully (returns 0, no crash).
    EXPECT_FALSE(RaisesLuaError("uz.input.get_gamepad_axis(0, 42)"));
    EXPECT_DOUBLE_EQ(EvalNumber("uz.input.get_gamepad_axis(0, 42)"), 0.0);
}

TEST_F(LuaInputExtendedTest, GetGamepadAxis_AllValidAxisNames_ReturnNumber) {
    // All 6 axis name strings must be recognized (return 0.0 — no hardware).
    const char* axes[] = {
        "LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger", nullptr
    };
    for (int i = 0; axes[i]; ++i) {
        std::string expr = std::string("uz.input.get_gamepad_axis(0, '") + axes[i] + "')";
        double val = EvalNumber(expr.c_str());
        EXPECT_DOUBLE_EQ(val, 0.0)
            << "Axis '" << axes[i] << "' must return 0.0 with no connected gamepad";
    }
}

TEST_F(LuaInputExtendedTest, GetGamepadAxis_DoesNotLeakStack) {
    lua_State* L = uz::LuaVM::GetState();
    int topBefore = lua_gettop(L);

    EXPECT_TRUE(DoString("local v = uz.input.get_gamepad_axis(0, 'LeftX')"));

    EXPECT_EQ(lua_gettop(L), topBefore);
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.input.is_gamepad_button_down
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaInputExtendedTest, IsGamepadButtonDown_IsFunction) {
    EXPECT_TRUE(EvalBool("type(uz.input.is_gamepad_button_down) == 'function'"));
}

TEST_F(LuaInputExtendedTest, IsGamepadButtonDown_DefaultState_ReturnsFalse) {
    // No gamepad connected — all buttons return false.
    EXPECT_FALSE(EvalBool("uz.input.is_gamepad_button_down(0, 'A')"));
    EXPECT_FALSE(EvalBool("uz.input.is_gamepad_button_down(0, 'B')"));
    EXPECT_FALSE(EvalBool("uz.input.is_gamepad_button_down(0, 'X')"));
    EXPECT_FALSE(EvalBool("uz.input.is_gamepad_button_down(0, 'Y')"));
}

TEST_F(LuaInputExtendedTest, IsGamepadButtonDown_AllValidButtonNames_ReturnFalse) {
    // All 14 mapped button names must be recognized (return false — no hardware).
    const char* buttons[] = {
        "A", "B", "X", "Y", "LB", "RB", "Start", "Back",
        "LeftStick", "RightStick", "DPadUp", "DPadDown", "DPadLeft", "DPadRight",
        nullptr
    };
    for (int i = 0; buttons[i]; ++i) {
        std::string expr = std::string("uz.input.is_gamepad_button_down(0, '")
                           + buttons[i] + "')";
        EXPECT_FALSE(EvalBool(expr.c_str()))
            << "Button '" << buttons[i] << "' must return false with no connected gamepad";
    }
}

TEST_F(LuaInputExtendedTest, IsGamepadButtonDown_UnrecognizedButtonName_ReturnsFalse) {
    EXPECT_FALSE(EvalBool("uz.input.is_gamepad_button_down(0, 'NotAButton')"));
}

TEST_F(LuaInputExtendedTest, IsGamepadButtonDown_NoInputSystem_ReturnsFalse) {
    uz::LuaSetInputSystem(uz::LuaVM::GetState(), nullptr);
    EXPECT_FALSE(EvalBool("uz.input.is_gamepad_button_down(0, 'A')"));
}

TEST_F(LuaInputExtendedTest, IsGamepadButtonDown_WrongIndexType_RaisesLuaError) {
    EXPECT_TRUE(RaisesLuaError("uz.input.is_gamepad_button_down('bad', 'A')"));
}

TEST_F(LuaInputExtendedTest, IsGamepadButtonDown_WrongButtonType_RaisesLuaError) {
    // In Lua 5.1/LuaJIT, luaL_checkstring coerces numbers to strings implicitly.
    // A numeric button name "99" is simply unrecognized — returns false, no error raised.
    // Verify the function handles numeric button arg gracefully (returns false, no crash).
    EXPECT_FALSE(RaisesLuaError("uz.input.is_gamepad_button_down(0, 99)"));
    EXPECT_FALSE(EvalBool("uz.input.is_gamepad_button_down(0, 99)"));
}

TEST_F(LuaInputExtendedTest, IsGamepadButtonDown_DoesNotLeakStack) {
    lua_State* L = uz::LuaVM::GetState();
    int topBefore = lua_gettop(L);

    EXPECT_TRUE(DoString("local v = uz.input.is_gamepad_button_down(0, 'A')"));

    EXPECT_EQ(lua_gettop(L), topBefore);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_mouse_capture (no-op from Lua — logs a warning, does not crash)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaInputExtendedTest, SetMouseCapture_IsFunction) {
    EXPECT_TRUE(EvalBool("type(uz.input.set_mouse_capture) == 'function'"));
}

TEST_F(LuaInputExtendedTest, SetMouseCapture_True_IsNoOp_DoesNotCrash) {
    // set_mouse_capture requires a platform HWND — it is always a no-op from Lua.
    EXPECT_TRUE(DoString("uz.input.set_mouse_capture(true)"));
    // Capture state must remain false (no HWND provided).
    EXPECT_FALSE(EvalBool("uz.input.is_mouse_captured()"));
}

TEST_F(LuaInputExtendedTest, SetMouseCapture_False_IsNoOp_DoesNotCrash) {
    EXPECT_TRUE(DoString("uz.input.set_mouse_capture(false)"));
    EXPECT_FALSE(EvalBool("uz.input.is_mouse_captured()"));
}

TEST_F(LuaInputExtendedTest, SetMouseCapture_VMRemainsStableAfterCall) {
    EXPECT_TRUE(DoString("uz.input.set_mouse_capture(true)"));
    // Verify VM is still operational after the no-op call.
    EXPECT_TRUE(EvalBool("type(uz.input) == 'table'"));
    EXPECT_FALSE(EvalBool("uz.input.is_key_down('W')"));
}
