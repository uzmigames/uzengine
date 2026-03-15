/// Unit tests for extended Lua Scene APIs:
///   ECS iteration (get_entities, get_entities_with, has_component, clear),
///   Scene hierarchy (attach_to, get_parent, get_children, detach, get_world_position),
///   Lights extended (get_light_range, get/set_spot_angles, get_light_direction,
///                    set_cast_shadows, remove_light),
///   Mesh/Material extended (set_visible, is_visible, get_mesh, get_material).
///
/// Pattern: identical to test_lua_scene_api.cpp — real uz::World, real Lua state,
/// LuaVM::Init() / Shutdown() per test, Lua snippets checked with EXPECT_*.

#include "lua_vm.h"
#include "lua_scene_api.h"
#include "lua_entity_api.h"
#include "lua_light_api.h"
#include "world.h"
#include "name_component.h"
#include "scene_component.h"
#include "camera_component.h"
#include "light_component.h"
#include "static_mesh_component.h"
#include "../math/vector3.h"
#include "../math/quaternion.h"

#include <gtest/gtest.h>
#include <cmath>
#include <cstring>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Fixture
// ─────────────────────────────────────────────────────────────────────────────

class LuaSceneExtendedTest : public ::testing::Test {
protected:
    uz::World world_;

    void SetUp() override {
        ASSERT_TRUE(uz::LuaVM::Init());
        uz::LuaSetWorld(uz::LuaVM::GetState(), &world_);
    }

    void TearDown() override {
        uz::LuaVM::Shutdown();
    }

    /// Runs Lua code and returns true on success.
    bool DoString(const char* code) {
        lua_State* L = uz::LuaVM::GetState();
        const int rc = luaL_dostring(L, code);
        if (rc != 0) {
            const char* err = lua_tostring(L, -1);
            ADD_FAILURE() << "Lua error in DoString: " << (err ? err : "unknown");
            lua_pop(L, 1);
        }
        return rc == 0;
    }

    /// Evaluates a Lua expression and returns it as a double.
    double EvalNumber(const char* expr) {
        lua_State* L = uz::LuaVM::GetState();
        const std::string code = std::string("return ") + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            const char* err = lua_tostring(L, -1);
            ADD_FAILURE() << "Lua eval error: " << (err ? err : "unknown");
            lua_pop(L, 1);
            return -999.0;
        }
        const double r = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return r;
    }

    /// Evaluates a Lua expression and returns it as a boolean.
    bool EvalBool(const char* expr) {
        lua_State* L = uz::LuaVM::GetState();
        const std::string code = std::string("return ") + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            lua_pop(L, 1);
            return false;
        }
        const bool r = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        return r;
    }

    /// Creates a named entity with SceneComponent at the given position.
    uz::Entity CreateNamedEntity(const std::string& name,
                                  const uz::Vec3& pos = {0.f, 0.f, 0.f}) {
        auto e = world_.CreateEntity();
        world_.AddComponent<uz::CName>(e, uz::CName{name});
        auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(e.id));
        auto& sc = world_.GetRegistry().emplace<uz::SceneComponent>(enttEntity);
        sc.SetPosition(pos);
        return e;
    }

    /// Creates an entity with SceneComponent but no name.
    uz::Entity CreateUnnamedEntity(const uz::Vec3& pos = {0.f, 0.f, 0.f}) {
        auto e = world_.CreateEntity();
        auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(e.id));
        auto& sc = world_.GetRegistry().emplace<uz::SceneComponent>(enttEntity);
        sc.SetPosition(pos);
        return e;
    }

    /// Creates a named entity with a CameraComponent (and SceneComponent).
    uz::Entity CreateCameraEntity(const std::string& name) {
        auto e = CreateNamedEntity(name);
        world_.AddComponent<uz::CameraComponent>(e);
        return e;
    }

    /// Pushes an entity as lightuserdata Lua global with the given var name.
    void PushEntityAsGlobal(uz::Entity e, const char* varName) {
        lua_State* L = uz::LuaVM::GetState();
        const uint64_t packed = (static_cast<uint64_t>(e.id) << 32)
                              | static_cast<uint64_t>(e.generation);
        void* p;
        std::memcpy(&p, &packed, sizeof(p));
        lua_pushlightuserdata(L, p);
        lua_setglobal(L, varName);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// ECS Iteration — get_entities
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneExtendedTest, GetEntities_EmptyWorld_ReturnsEmptyTable) {
    EXPECT_TRUE(EvalBool(
        "(function() local t = uz.scene.get_entities(); return #t == 0 end)()"
    ));
}

TEST_F(LuaSceneExtendedTest, GetEntities_ThreeEntities_ReturnsAllThree) {
    CreateUnnamedEntity();
    CreateUnnamedEntity();
    CreateUnnamedEntity();

    EXPECT_TRUE(EvalBool(
        "(function() local t = uz.scene.get_entities(); return #t == 3 end)()"
    ));
}

TEST_F(LuaSceneExtendedTest, GetEntities_ReturnsLightuserdataElements) {
    CreateUnnamedEntity();

    EXPECT_TRUE(EvalBool(
        "(function()"
        "  local t = uz.scene.get_entities()"
        "  return type(t[1]) == 'userdata'"
        "end)()"
    ));
}

// ─────────────────────────────────────────────────────────────────────────────
// ECS Iteration — get_entities_with
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneExtendedTest, GetEntitiesWith_Transform_ReturnsAllWithSceneComponent) {
    CreateUnnamedEntity();
    CreateUnnamedEntity();

    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local t = uz.scene.get_entities_with('transform')\n"
        "  return #t == 2\n"
        "end)()"
    ));
}

TEST_F(LuaSceneExtendedTest, GetEntitiesWith_Camera_ReturnsOnlyCameraEntities) {
    CreateNamedEntity("plain1");
    CreateNamedEntity("plain2");
    CreateCameraEntity("cam1");
    CreateCameraEntity("cam2");

    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local t = uz.scene.get_entities_with('camera')\n"
        "  return #t == 2\n"
        "end)()"
    ));
}

TEST_F(LuaSceneExtendedTest, GetEntitiesWith_Camera_DoesNotIncludeNonCameraEntities) {
    // Two entities without cameras, one with.
    CreateNamedEntity("no_cam_a");
    CreateNamedEntity("no_cam_b");
    const uz::Entity camEntity = CreateCameraEntity("cam");

    // Verify that each returned entity from get_entities_with("camera")
    // actually has a camera component.
    PushEntityAsGlobal(camEntity, "cam_e");

    EXPECT_TRUE(EvalBool(
        "(function()"
        "  local t = uz.scene.get_entities_with('camera')"
        "  if #t ~= 1 then return false end"
        "  return uz.scene.has_component(t[1], 'camera')"
        "end)()"
    ));
}

TEST_F(LuaSceneExtendedTest, GetEntitiesWith_UnknownComponent_ReturnsEmptyTable) {
    CreateUnnamedEntity();
    CreateUnnamedEntity();

    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local t = uz.scene.get_entities_with('nonexistent_type')\n"
        "  return #t == 0\n"
        "end)()"
    ));
}

TEST_F(LuaSceneExtendedTest, GetEntitiesWith_Light_ReturnsEntityWithPointLight) {
    const uz::Entity plain  = CreateNamedEntity("plain");
    const uz::Entity lightE = CreateNamedEntity("light_entity");
    world_.AddComponent<uz::PointLightComponent>(lightE);

    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local t = uz.scene.get_entities_with('light')\n"
        "  return #t == 1\n"
        "end)()"
    ));
    (void)plain;
}

// ─────────────────────────────────────────────────────────────────────────────
// ECS Iteration — has_component
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneExtendedTest, HasComponent_Transform_ReturnsTrueForEntityWithSceneComponent) {
    const uz::Entity e = CreateNamedEntity("has_transform");
    PushEntityAsGlobal(e, "e");

    EXPECT_TRUE(EvalBool("uz.scene.has_component(e, 'transform')"));
}

TEST_F(LuaSceneExtendedTest, HasComponent_Camera_ReturnsFalseWhenNoCameraComponent) {
    const uz::Entity e = CreateNamedEntity("no_camera");
    PushEntityAsGlobal(e, "e");

    EXPECT_FALSE(EvalBool("uz.scene.has_component(e, 'camera')"));
}

TEST_F(LuaSceneExtendedTest, HasComponent_Camera_ReturnsTrueAfterCameraAdded) {
    const uz::Entity e = CreateNamedEntity("with_camera");
    world_.AddComponent<uz::CameraComponent>(e);
    PushEntityAsGlobal(e, "e");

    EXPECT_TRUE(EvalBool("uz.scene.has_component(e, 'camera')"));
}

TEST_F(LuaSceneExtendedTest, HasComponent_Mesh_ReturnsFalseWhenNoMesh) {
    const uz::Entity e = CreateNamedEntity("no_mesh");
    PushEntityAsGlobal(e, "e");

    EXPECT_FALSE(EvalBool("uz.scene.has_component(e, 'mesh')"));
}

TEST_F(LuaSceneExtendedTest, HasComponent_Light_ReturnsTrueForPointLight) {
    const uz::Entity e = CreateNamedEntity("point_light_entity");
    world_.AddComponent<uz::PointLightComponent>(e);
    PushEntityAsGlobal(e, "e");

    EXPECT_TRUE(EvalBool("uz.scene.has_component(e, 'light')"));
}

TEST_F(LuaSceneExtendedTest, HasComponent_Unknown_ReturnsFalse) {
    const uz::Entity e = CreateNamedEntity("entity");
    PushEntityAsGlobal(e, "e");

    EXPECT_FALSE(EvalBool("uz.scene.has_component(e, 'completely_unknown')"));
}

// ─────────────────────────────────────────────────────────────────────────────
// ECS Iteration — clear
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneExtendedTest, Clear_DestroysAllEntities_EntityCountBecomesZero) {
    CreateNamedEntity("entity_a");
    CreateNamedEntity("entity_b");
    CreateNamedEntity("entity_c");

    ASSERT_EQ(world_.EntityCount(), 3u);

    EXPECT_TRUE(DoString("uz.scene.clear()"));

    EXPECT_EQ(world_.EntityCount(), 0u);
}

TEST_F(LuaSceneExtendedTest, Clear_AfterClear_GetEntitiesReturnsEmptyTable) {
    CreateUnnamedEntity();
    CreateUnnamedEntity();

    EXPECT_TRUE(DoString("uz.scene.clear()"));

    EXPECT_TRUE(EvalBool(
        "(function() local t = uz.scene.get_entities(); return #t == 0 end)()"
    ));
}

// ─────────────────────────────────────────────────────────────────────────────
// Hierarchy — get_world_position
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneExtendedTest, GetWorldPosition_RootEntity_ReturnsLocalPosition) {
    CreateNamedEntity("root_node", {7.f, 8.f, 9.f});

    EXPECT_TRUE(DoString(
        "local e = uz.scene.find('root_node')\n"
        "wp_x, wp_y, wp_z = uz.scene.get_world_position(e)\n"
    ));

    EXPECT_NEAR(EvalNumber("wp_x"), 7.0, 1e-4);
    EXPECT_NEAR(EvalNumber("wp_y"), 8.0, 1e-4);
    EXPECT_NEAR(EvalNumber("wp_z"), 9.0, 1e-4);
}

TEST_F(LuaSceneExtendedTest, GetWorldPosition_NilEntityReturnsNil) {
    EXPECT_TRUE(EvalBool("uz.scene.get_world_position(nil) == nil"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Hierarchy — attach_to / get_parent
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneExtendedTest, AttachTo_SetsParentChildRelationship) {
    CreateNamedEntity("parent_node");
    CreateNamedEntity("child_node");

    EXPECT_TRUE(DoString(
        "local parent = uz.scene.find('parent_node')\n"
        "local child  = uz.scene.find('child_node')\n"
        "uz.scene.attach_to(child, parent)\n"
    ));

    // Verify hierarchy via Lua get_parent.
    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local parent = uz.scene.find('parent_node')\n"
        "  local child  = uz.scene.find('child_node')\n"
        "  local p = uz.scene.get_parent(child)\n"
        "  return p ~= nil\n"
        "end)()"
    ));
}

TEST_F(LuaSceneExtendedTest, GetParent_RootEntity_ReturnsNil) {
    CreateNamedEntity("lone_root");

    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local e = uz.scene.find('lone_root')\n"
        "  return uz.scene.get_parent(e) == nil\n"
        "end)()"
    ));
}

TEST_F(LuaSceneExtendedTest, GetParent_AfterAttach_ReturnsNonNil) {
    CreateNamedEntity("p_node");
    CreateNamedEntity("c_node");

    EXPECT_TRUE(DoString(
        "local p = uz.scene.find('p_node')\n"
        "local c = uz.scene.find('c_node')\n"
        "uz.scene.attach_to(c, p)\n"
    ));

    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local c = uz.scene.find('c_node')\n"
        "  return uz.scene.get_parent(c) ~= nil\n"
        "end)()"
    ));
}

// ─────────────────────────────────────────────────────────────────────────────
// Hierarchy — get_children
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneExtendedTest, GetChildren_LeafEntity_ReturnsEmptyTable) {
    CreateNamedEntity("leaf");

    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local e = uz.scene.find('leaf')\n"
        "  local t = uz.scene.get_children(e)\n"
        "  return #t == 0\n"
        "end)()"
    ));
}

TEST_F(LuaSceneExtendedTest, GetChildren_AfterTwoAttaches_ReturnsTwoChildren) {
    CreateNamedEntity("parent_with_kids");
    CreateNamedEntity("kid_a");
    CreateNamedEntity("kid_b");

    EXPECT_TRUE(DoString(
        "local par  = uz.scene.find('parent_with_kids')\n"
        "local ka   = uz.scene.find('kid_a')\n"
        "local kb   = uz.scene.find('kid_b')\n"
        "uz.scene.attach_to(ka, par)\n"
        "uz.scene.attach_to(kb, par)\n"
    ));

    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local par = uz.scene.find('parent_with_kids')\n"
        "  local kids = uz.scene.get_children(par)\n"
        "  return #kids == 2\n"
        "end)()"
    ));
}

TEST_F(LuaSceneExtendedTest, GetChildren_ReturnedHandlesAreUserdata) {
    CreateNamedEntity("parent_ck");
    CreateNamedEntity("child_ck");

    EXPECT_TRUE(DoString(
        "local par = uz.scene.find('parent_ck')\n"
        "local chi = uz.scene.find('child_ck')\n"
        "uz.scene.attach_to(chi, par)\n"
    ));

    EXPECT_TRUE(EvalBool(
        "(function()"
        "  local par = uz.scene.find('parent_ck')"
        "  local kids = uz.scene.get_children(par)"
        "  return type(kids[1]) == 'userdata'"
        "end)()"
    ));
}

// ─────────────────────────────────────────────────────────────────────────────
// Hierarchy — detach
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneExtendedTest, Detach_RemovesParentRelationship) {
    CreateNamedEntity("detach_parent");
    CreateNamedEntity("detach_child");

    EXPECT_TRUE(DoString(
        "local par = uz.scene.find('detach_parent')\n"
        "local chi = uz.scene.find('detach_child')\n"
        "uz.scene.attach_to(chi, par)\n"
        "uz.scene.detach(chi)\n"
    ));

    // After detach the child should report nil parent.
    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local chi = uz.scene.find('detach_child')\n"
        "  return uz.scene.get_parent(chi) == nil\n"
        "end)()"
    ));
}

TEST_F(LuaSceneExtendedTest, Detach_AfterDetach_ParentChildrenListIsEmpty) {
    CreateNamedEntity("dp2");
    CreateNamedEntity("dc2");

    EXPECT_TRUE(DoString(
        "local par = uz.scene.find('dp2')\n"
        "local chi = uz.scene.find('dc2')\n"
        "uz.scene.attach_to(chi, par)\n"
        "uz.scene.detach(chi)\n"
    ));

    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local par = uz.scene.find('dp2')\n"
        "  local kids = uz.scene.get_children(par)\n"
        "  return #kids == 0\n"
        "end)()"
    ));
}

// ─────────────────────────────────────────────────────────────────────────────
// Lights Extended — get_light_range
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneExtendedTest, GetLightRange_PointLight_ReturnsSetRange) {
    const uz::Entity e = CreateNamedEntity("point_range_test");
    world_.AddComponent<uz::PointLightComponent>(e);
    world_.GetComponent<uz::PointLightComponent>(e).range = 25.0f;
    PushEntityAsGlobal(e, "e");

    const double range = EvalNumber("uz.scene.get_light_range(e)");
    EXPECT_NEAR(range, 25.0, 1e-4);
}

TEST_F(LuaSceneExtendedTest, GetLightRange_DirectionalLight_ReturnsNil) {
    const uz::Entity e = CreateNamedEntity("dir_no_range");
    world_.AddComponent<uz::DirectionalLightComponent>(e);
    PushEntityAsGlobal(e, "e");

    EXPECT_TRUE(EvalBool("uz.scene.get_light_range(e) == nil"));
}

TEST_F(LuaSceneExtendedTest, GetLightRange_EntityWithNoLight_ReturnsNil) {
    const uz::Entity e = CreateNamedEntity("no_light_range");
    PushEntityAsGlobal(e, "e");

    EXPECT_TRUE(EvalBool("uz.scene.get_light_range(e) == nil"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Lights Extended — get_spot_angles / set_spot_angles
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneExtendedTest, SetAndGetSpotAngles_RoundTrip) {
    const uz::Entity e = CreateNamedEntity("spot_angles_entity");
    // add_spot_light via Lua so the SpotLightComponent exists
    PushEntityAsGlobal(e, "spot_e");

    EXPECT_TRUE(DoString(
        "uz.scene.add_spot_light(spot_e, 1, 1, 1, 2.0, 15.0, 20.0, 40.0)\n"
        "uz.scene.set_spot_angles(spot_e, 20.0, 40.0)\n"
        "inner, outer = uz.scene.get_spot_angles(spot_e)\n"
    ));

    EXPECT_NEAR(EvalNumber("inner"), 20.0, 0.5);
    EXPECT_NEAR(EvalNumber("outer"), 40.0, 0.5);
}

TEST_F(LuaSceneExtendedTest, GetSpotAngles_NonSpotLight_ReturnsNil) {
    const uz::Entity e = CreateNamedEntity("point_for_spot");
    world_.AddComponent<uz::PointLightComponent>(e);
    PushEntityAsGlobal(e, "e");

    EXPECT_TRUE(EvalBool("uz.scene.get_spot_angles(e) == nil"));
}

TEST_F(LuaSceneExtendedTest, SetSpotAngles_NoSpotComponent_NoOp_DoesNotCrash) {
    const uz::Entity e = CreateNamedEntity("no_spot_comp");
    PushEntityAsGlobal(e, "e");

    // Should not raise a Lua error — just logs a warning.
    EXPECT_TRUE(DoString("uz.scene.set_spot_angles(e, 10.0, 30.0)"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Lights Extended — get_light_direction
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneExtendedTest, GetLightDirection_DirectionalLight_ReturnsThreeNumbers) {
    const uz::Entity e = CreateNamedEntity("sun");
    world_.AddComponent<uz::DirectionalLightComponent>(e);
    PushEntityAsGlobal(e, "e");

    EXPECT_TRUE(DoString(
        "dx, dy, dz = uz.scene.get_light_direction(e)\n"
    ));

    // Direction vector must be a unit vector — length ≈ 1.
    const double dx = EvalNumber("dx");
    const double dy = EvalNumber("dy");
    const double dz = EvalNumber("dz");
    const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
    EXPECT_NEAR(len, 1.0, 1e-3);
}

TEST_F(LuaSceneExtendedTest, GetLightDirection_PointLight_ReturnsNil) {
    const uz::Entity e = CreateNamedEntity("point_dir");
    world_.AddComponent<uz::PointLightComponent>(e);
    PushEntityAsGlobal(e, "e");

    EXPECT_TRUE(EvalBool("uz.scene.get_light_direction(e) == nil"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Lights Extended — set_cast_shadows
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneExtendedTest, SetCastShadows_DirectionalLight_DisablesThenEnables) {
    const uz::Entity e = CreateNamedEntity("shadow_dir");
    world_.AddComponent<uz::DirectionalLightComponent>(e);
    PushEntityAsGlobal(e, "e");

    // Default is true — disable shadows first.
    EXPECT_TRUE(DoString("uz.scene.set_cast_shadows(e, false)"));
    EXPECT_FALSE(world_.GetComponent<uz::DirectionalLightComponent>(e).castShadows);

    // Re-enable.
    EXPECT_TRUE(DoString("uz.scene.set_cast_shadows(e, true)"));
    EXPECT_TRUE(world_.GetComponent<uz::DirectionalLightComponent>(e).castShadows);
}

TEST_F(LuaSceneExtendedTest, SetCastShadows_PointLight_TogglesFlag) {
    const uz::Entity e = CreateNamedEntity("shadow_point");
    world_.AddComponent<uz::PointLightComponent>(e);
    PushEntityAsGlobal(e, "e");

    EXPECT_TRUE(DoString("uz.scene.set_cast_shadows(e, true)"));
    EXPECT_TRUE(world_.GetComponent<uz::PointLightComponent>(e).castShadows);

    EXPECT_TRUE(DoString("uz.scene.set_cast_shadows(e, false)"));
    EXPECT_FALSE(world_.GetComponent<uz::PointLightComponent>(e).castShadows);
}

TEST_F(LuaSceneExtendedTest, SetCastShadows_SpotLight_DisablesShadows) {
    const uz::Entity e = CreateNamedEntity("shadow_spot");
    PushEntityAsGlobal(e, "e");
    EXPECT_TRUE(DoString(
        "uz.scene.add_spot_light(e, 1, 1, 1, 1.0, 10.0, 15.0, 30.0)\n"
        "uz.scene.set_cast_shadows(e, false)\n"
    ));

    EXPECT_FALSE(world_.GetComponent<uz::SpotLightComponent>(e).castShadows);
}

TEST_F(LuaSceneExtendedTest, SetCastShadows_EntityWithNoLight_DoesNotCrash) {
    const uz::Entity e = CreateNamedEntity("no_light_shadow");
    PushEntityAsGlobal(e, "e");

    // Should not crash — logs a warning.
    EXPECT_TRUE(DoString("uz.scene.set_cast_shadows(e, true)"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Lights Extended — remove_light
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneExtendedTest, RemoveLight_PointLight_RemovesComponent) {
    const uz::Entity e = CreateNamedEntity("rm_point");
    world_.AddComponent<uz::PointLightComponent>(e);
    PushEntityAsGlobal(e, "e");

    ASSERT_TRUE(world_.HasComponent<uz::PointLightComponent>(e));

    EXPECT_TRUE(DoString("uz.scene.remove_light(e)"));

    EXPECT_FALSE(world_.HasComponent<uz::PointLightComponent>(e));
}

TEST_F(LuaSceneExtendedTest, RemoveLight_DirectionalLight_RemovesComponent) {
    const uz::Entity e = CreateNamedEntity("rm_dir");
    world_.AddComponent<uz::DirectionalLightComponent>(e);
    PushEntityAsGlobal(e, "e");

    EXPECT_TRUE(DoString("uz.scene.remove_light(e)"));

    EXPECT_FALSE(world_.HasComponent<uz::DirectionalLightComponent>(e));
}

TEST_F(LuaSceneExtendedTest, RemoveLight_NoLight_IsNoOp_DoesNotCrash) {
    const uz::Entity e = CreateNamedEntity("rm_no_light");
    PushEntityAsGlobal(e, "e");

    // No light present — remove_light must be silent.
    EXPECT_TRUE(DoString("uz.scene.remove_light(e)"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Lights Extended — has_component "light" after remove_light
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneExtendedTest, HasComponent_Light_ReturnsFalseAfterRemoveLight) {
    const uz::Entity e = CreateNamedEntity("has_then_remove");
    world_.AddComponent<uz::PointLightComponent>(e);
    PushEntityAsGlobal(e, "e");

    EXPECT_TRUE(EvalBool("uz.scene.has_component(e, 'light')"));

    EXPECT_TRUE(DoString("uz.scene.remove_light(e)"));

    EXPECT_FALSE(EvalBool("uz.scene.has_component(e, 'light')"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Mesh/Material Extended — set_visible / is_visible
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneExtendedTest, IsVisible_EntityWithNoMesh_ReturnsTrue) {
    const uz::Entity e = CreateNamedEntity("no_mesh_vis");
    PushEntityAsGlobal(e, "e");

    // is_visible returns true when no StaticMeshComponent exists.
    EXPECT_TRUE(EvalBool("uz.scene.is_visible(e)"));
}

TEST_F(LuaSceneExtendedTest, SetVisible_False_MakesEntityInvisible) {
    const uz::Entity e = CreateNamedEntity("hide_me");
    world_.AddComponent<uz::StaticMeshComponent>(e);
    PushEntityAsGlobal(e, "e");

    EXPECT_TRUE(DoString("uz.scene.set_visible(e, false)"));

    EXPECT_FALSE(EvalBool("uz.scene.is_visible(e)"));
}

TEST_F(LuaSceneExtendedTest, SetVisible_TrueAfterFalse_EntityBecomesVisible) {
    const uz::Entity e = CreateNamedEntity("show_again");
    world_.AddComponent<uz::StaticMeshComponent>(e);
    world_.GetComponent<uz::StaticMeshComponent>(e).visible = false;
    PushEntityAsGlobal(e, "e");

    EXPECT_TRUE(DoString("uz.scene.set_visible(e, true)"));

    EXPECT_TRUE(EvalBool("uz.scene.is_visible(e)"));
}

TEST_F(LuaSceneExtendedTest, SetVisible_NoMeshComponent_DoesNotCrash) {
    // set_visible warns and no-ops when there is no StaticMeshComponent.
    const uz::Entity e = CreateNamedEntity("no_mesh_set_vis");
    PushEntityAsGlobal(e, "e");

    EXPECT_TRUE(DoString("uz.scene.set_visible(e, false)"));
    // is_visible should still return true (no mesh = default visible).
    EXPECT_TRUE(EvalBool("uz.scene.is_visible(e)"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Mesh/Material Extended — get_mesh
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneExtendedTest, GetMesh_NoMeshComponent_ReturnsNil) {
    const uz::Entity e = CreateNamedEntity("no_mesh_get");
    PushEntityAsGlobal(e, "e");

    EXPECT_TRUE(EvalBool("uz.scene.get_mesh(e) == nil"));
}

TEST_F(LuaSceneExtendedTest, GetMesh_MeshNotLoaded_ReturnsNil) {
    const uz::Entity e = CreateNamedEntity("mesh_not_loaded");
    world_.AddComponent<uz::StaticMeshComponent>(e);
    auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    mc.meshId     = 42;
    mc.meshLoaded = false;  // meshLoaded must be true for get_mesh to return the id.
    PushEntityAsGlobal(e, "e");

    EXPECT_TRUE(EvalBool("uz.scene.get_mesh(e) == nil"));
}

TEST_F(LuaSceneExtendedTest, GetMesh_MeshLoaded_ReturnsMeshId) {
    const uz::Entity e = CreateNamedEntity("mesh_loaded");
    // set_mesh via Lua sets meshLoaded = true.
    PushEntityAsGlobal(e, "e");

    EXPECT_TRUE(DoString("uz.scene.set_mesh(e, 99)"));

    const double meshId = EvalNumber("uz.scene.get_mesh(e)");
    EXPECT_NEAR(meshId, 99.0, 1e-4);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mesh/Material Extended — get_material
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneExtendedTest, GetMaterial_NoMeshComponent_ReturnsNil) {
    const uz::Entity e = CreateNamedEntity("no_mesh_mat");
    PushEntityAsGlobal(e, "e");

    EXPECT_TRUE(EvalBool("uz.scene.get_material(e) == nil"));
}

TEST_F(LuaSceneExtendedTest, GetMaterial_MaterialNotLoaded_ReturnsNil) {
    const uz::Entity e = CreateNamedEntity("mat_not_loaded");
    world_.AddComponent<uz::StaticMeshComponent>(e);
    auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    mc.materialId     = 7;
    mc.materialLoaded = false;
    PushEntityAsGlobal(e, "e");

    EXPECT_TRUE(EvalBool("uz.scene.get_material(e) == nil"));
}

TEST_F(LuaSceneExtendedTest, GetMaterial_AfterSetMaterial_ReturnsTableWithAlbedo) {
    const uz::Entity e = CreateNamedEntity("mat_loaded");
    PushEntityAsGlobal(e, "e");

    // set_material with albedo=5 sets materialLoaded = true.
    EXPECT_TRUE(DoString("uz.scene.set_material(e, { albedo = 5 })"));

    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local mat = uz.scene.get_material(e)\n"
        "  return type(mat) == 'table' and mat.albedo == 5\n"
        "end)()"
    ));
}
