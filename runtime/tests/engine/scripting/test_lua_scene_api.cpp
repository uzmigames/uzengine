/// Unit tests for Lua Scene API — uz.scene.find, get/set_position,
/// get/set_rotation, get/set_scale. Uses a real World with entities.

#include "lua_vm.h"
#include "lua_scene_api.h"
#include "../ecs/world.h"
#include "../ecs/name_component.h"
#include "../ecs/scene_component.h"
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

class LuaSceneAPITest : public ::testing::Test {
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

    /// Helper: run expression, return number.
    double EvalNumber(const char* expr) {
        lua_State* L = uz::LuaVM::GetState();
        std::string code = std::string("return ") + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            const char* err = lua_tostring(L, -1);
            ADD_FAILURE() << "Lua error: " << (err ? err : "unknown");
            lua_pop(L, 1);
            return -999.0;
        }
        double r = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return r;
    }

    /// Helper: run expression, return bool.
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

    /// Creates a named entity with SceneComponent.
    /// Uses registry.emplace directly because SceneComponent is non-copyable
    /// (Component base deletes copy ctor/assignment), and World::AddComponent
    /// uses emplace_or_replace which requires copyability.
    uz::Entity CreateNamedEntity(const std::string& name,
                                  const uz::Vec3& pos = {0, 0, 0}) {
        auto e = world_.CreateEntity();
        world_.AddComponent<uz::CName>(e, uz::CName{name});
        auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(e.id));
        auto& sc = world_.GetRegistry().emplace<uz::SceneComponent>(enttEntity);
        sc.SetPosition(pos);
        return e;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// uz.scene table existence
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneAPITest, SceneTableExists) {
    EXPECT_TRUE(EvalBool("type(uz.scene) == 'table'"));
}

TEST_F(LuaSceneAPITest, SceneFunctionsExist) {
    EXPECT_TRUE(EvalBool("type(uz.scene.find) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.scene.get_position) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.scene.set_position) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.scene.get_rotation) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.scene.set_rotation) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.scene.get_scale) == 'function'"));
    EXPECT_TRUE(EvalBool("type(uz.scene.set_scale) == 'function'"));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.scene.find
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneAPITest, FindExistingEntity) {
    CreateNamedEntity("player");
    EXPECT_TRUE(EvalBool("uz.scene.find('player') ~= nil"));
}

TEST_F(LuaSceneAPITest, FindNonexistentReturnsNil) {
    EXPECT_TRUE(EvalBool("uz.scene.find('nonexistent') == nil"));
}

TEST_F(LuaSceneAPITest, FindEmptyStringReturnsNil) {
    EXPECT_TRUE(EvalBool("uz.scene.find('') == nil"));
}

TEST_F(LuaSceneAPITest, FindReturnsLightuserdata) {
    CreateNamedEntity("test_entity");
    EXPECT_TRUE(DoString("local e = uz.scene.find('test_entity')\n"
                         "assert(type(e) == 'userdata', 'expected userdata, got ' .. type(e))"));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.scene.get/set_position
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneAPITest, GetPositionReturnsXYZ) {
    CreateNamedEntity("cube", {1.0f, 2.0f, 3.0f});

    EXPECT_TRUE(DoString(
        "local e = uz.scene.find('cube')\n"
        "px, py, pz = uz.scene.get_position(e)\n"
    ));

    EXPECT_NEAR(EvalNumber("px"), 1.0, 0.01);
    EXPECT_NEAR(EvalNumber("py"), 2.0, 0.01);
    EXPECT_NEAR(EvalNumber("pz"), 3.0, 0.01);
}

TEST_F(LuaSceneAPITest, SetPositionUpdatesComponent) {
    auto e = CreateNamedEntity("box");

    EXPECT_TRUE(DoString(
        "local e = uz.scene.find('box')\n"
        "uz.scene.set_position(e, 10, 20, 30)\n"
    ));

    auto& sc = world_.GetComponent<uz::SceneComponent>(e);
    EXPECT_NEAR(sc.GetPosition().x, 10.0f, 0.01f);
    EXPECT_NEAR(sc.GetPosition().y, 20.0f, 0.01f);
    EXPECT_NEAR(sc.GetPosition().z, 30.0f, 0.01f);
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.scene.get/set_rotation
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneAPITest, SetAndGetRotationRoundTrips) {
    CreateNamedEntity("spinner");

    EXPECT_TRUE(DoString(
        "local e = uz.scene.find('spinner')\n"
        "uz.scene.set_rotation(e, 0, 90, 0)\n"  // 90 deg yaw
        "rx, ry, rz = uz.scene.get_rotation(e)\n"
    ));

    EXPECT_NEAR(EvalNumber("ry"), 90.0, 1.0); // yaw ~90 degrees
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.scene.get/set_scale
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneAPITest, GetDefaultScaleIsOne) {
    CreateNamedEntity("scaled");

    EXPECT_TRUE(DoString(
        "local e = uz.scene.find('scaled')\n"
        "sx, sy, sz = uz.scene.get_scale(e)\n"
    ));

    EXPECT_NEAR(EvalNumber("sx"), 1.0, 0.01);
    EXPECT_NEAR(EvalNumber("sy"), 1.0, 0.01);
    EXPECT_NEAR(EvalNumber("sz"), 1.0, 0.01);
}

TEST_F(LuaSceneAPITest, SetScaleUpdatesComponent) {
    auto e = CreateNamedEntity("resizable");

    EXPECT_TRUE(DoString(
        "local e = uz.scene.find('resizable')\n"
        "uz.scene.set_scale(e, 2, 3, 4)\n"
    ));

    auto& sc = world_.GetComponent<uz::SceneComponent>(e);
    EXPECT_NEAR(sc.GetScale().x, 2.0f, 0.01f);
    EXPECT_NEAR(sc.GetScale().y, 3.0f, 0.01f);
    EXPECT_NEAR(sc.GetScale().z, 4.0f, 0.01f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Error handling — no crash on invalid inputs
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneAPITest, GetPositionWithNilEntityReturnsNil) {
    EXPECT_TRUE(EvalBool("uz.scene.get_position(nil) == nil"));
}

TEST_F(LuaSceneAPITest, GetScaleWithNilEntityReturnsNil) {
    EXPECT_TRUE(EvalBool("uz.scene.get_scale(nil) == nil"));
}

TEST_F(LuaSceneAPITest, NoWorldSetReturnsNil) {
    uz::LuaSetWorld(uz::LuaVM::GetState(), nullptr);
    EXPECT_TRUE(EvalBool("uz.scene.find('anything') == nil"));
}
