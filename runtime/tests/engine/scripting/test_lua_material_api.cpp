/// Unit tests for the uz.scene material API:
///   uz.scene.set_material, uz.scene.get_material, uz.scene.set_mesh,
///   uz.scene.get_mesh.
///
/// Pattern: real uz::World, real LuaVM, no SceneRenderer (texture ID
/// resolution is skipped gracefully — the functions log a warning but
/// continue applying all scalar/boolean/enum material parameters).
///
/// Entities are created in C++ and pushed to Lua as lightuserdata using
/// the same packing scheme the scene API uses internally:
///   void* = (uint64_t)(id << 32 | generation)

#include "lua_vm.h"
#include "lua_scene_api.h"
#include "lua_material_api.h"
#include "world.h"
#include "name_component.h"
#include "scene_component.h"
#include "static_mesh_component.h"
#include "material/material_asset.h"
#include "../math/vector3.h"

#include <gtest/gtest.h>
#include <cstring>
#include <string>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Fixture
// ─────────────────────────────────────────────────────────────────────────────

class LuaMaterialAPITest : public ::testing::Test {
protected:
    uz::World world_;

    void SetUp() override {
        ASSERT_TRUE(uz::LuaVM::Init());
        uz::LuaSetWorld(uz::LuaVM::GetState(), &world_);
        // SceneRenderer is intentionally NOT set — tests must remain
        // hardware-independent. Texture-ID resolution is skipped (warned),
        // but all material scalars, booleans, and enums are applied normally.
    }

    void TearDown() override {
        uz::LuaVM::Shutdown();
    }

    /// Runs Lua code; returns true on success. Fails the test on Lua error.
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

    /// Evaluates a Lua expression; returns result as double (-999 on error).
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

    /// Evaluates a Lua expression; returns result as bool.
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

    /// Evaluates a Lua expression; returns result as string ("" on error).
    std::string EvalString(const char* expr) {
        lua_State* L = uz::LuaVM::GetState();
        const std::string code = std::string("return ") + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            lua_pop(L, 1);
            return "";
        }
        std::string r;
        if (lua_isstring(L, -1)) r = lua_tostring(L, -1);
        lua_pop(L, 1);
        return r;
    }

    /// Creates a named entity with a SceneComponent and returns it.
    uz::Entity CreateNamedEntity(const std::string& name,
                                  const uz::Vec3& pos = {0.f, 0.f, 0.f}) {
        auto e = world_.CreateEntity();
        world_.AddComponent<uz::CName>(e, uz::CName{name});
        auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(e.id));
        auto& sc = world_.GetRegistry().emplace<uz::SceneComponent>(enttEntity);
        sc.SetPosition(pos);
        return e;
    }

    /// Pushes an entity handle into the Lua global `varName` as lightuserdata.
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
// Function existence
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaMaterialAPITest, SetMesh_FunctionExists) {
    EXPECT_TRUE(EvalBool("type(uz.scene.set_mesh) == 'function'"));
}

TEST_F(LuaMaterialAPITest, GetMesh_FunctionExists) {
    EXPECT_TRUE(EvalBool("type(uz.scene.get_mesh) == 'function'"));
}

TEST_F(LuaMaterialAPITest, SetMaterial_FunctionExists) {
    EXPECT_TRUE(EvalBool("type(uz.scene.set_material) == 'function'"));
}

TEST_F(LuaMaterialAPITest, GetMaterial_FunctionExists) {
    EXPECT_TRUE(EvalBool("type(uz.scene.get_material) == 'function'"));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.scene.set_mesh / uz.scene.get_mesh
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaMaterialAPITest, SetMesh_SetsComponentMeshIdAndMeshLoadedFlag) {
    const uz::Entity e = CreateNamedEntity("mesh_entity");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString("uz.scene.set_mesh(me, 123)"));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    EXPECT_EQ(mc.meshId, 123u);
    EXPECT_TRUE(mc.meshLoaded);
}

TEST_F(LuaMaterialAPITest, GetMesh_AfterSetMesh_ReturnsMeshId) {
    const uz::Entity e = CreateNamedEntity("mesh_get_entity");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString("uz.scene.set_mesh(me, 99)"));
    const double id = EvalNumber("uz.scene.get_mesh(me)");
    EXPECT_NEAR(id, 99.0, 1e-9);
}

TEST_F(LuaMaterialAPITest, GetMesh_NoComponent_ReturnsNil) {
    const uz::Entity e = CreateNamedEntity("no_mesh_entity");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(EvalBool("uz.scene.get_mesh(me) == nil"));
}

TEST_F(LuaMaterialAPITest, GetMesh_ComponentExistsButNotLoaded_ReturnsNil) {
    const uz::Entity e = CreateNamedEntity("unloaded_mesh");
    world_.AddComponent<uz::StaticMeshComponent>(e);
    auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    mc.meshId     = 77;
    mc.meshLoaded = false;
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(EvalBool("uz.scene.get_mesh(me) == nil"));
}

TEST_F(LuaMaterialAPITest, SetMesh_NegativeMeshId_IsIgnored) {
    const uz::Entity e = CreateNamedEntity("negative_mesh_id");
    PushEntityAsGlobal(e, "me");

    // Negative mesh IDs must be rejected — meshLoaded stays false.
    EXPECT_TRUE(DoString("uz.scene.set_mesh(me, -1)"));

    EXPECT_FALSE(world_.HasComponent<uz::StaticMeshComponent>(e) &&
                 world_.GetComponent<uz::StaticMeshComponent>(e).meshLoaded);
}

TEST_F(LuaMaterialAPITest, SetMesh_NilEntity_DoesNotCrash) {
    EXPECT_TRUE(DoString("uz.scene.set_mesh(nil, 1)"));
}

TEST_F(LuaMaterialAPITest, GetMesh_NilEntity_ReturnsNil) {
    EXPECT_TRUE(EvalBool("uz.scene.get_mesh(nil) == nil"));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.scene.set_material — basic PBR scalar parameters
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaMaterialAPITest, SetMaterial_RoughnessMultiplier_WrittenToComponent) {
    const uz::Entity e = CreateNamedEntity("rough_entity");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { roughness_multiplier = 0.25 })"
    ));

    ASSERT_TRUE(world_.HasComponent<uz::StaticMeshComponent>(e));
    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    ASSERT_NE(mc.GetMaterial(0), nullptr);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_NEAR(mat->roughnessMultiplier, 0.25f, 1e-5f);
}

TEST_F(LuaMaterialAPITest, SetMaterial_MetallicMultiplier_WrittenToComponent) {
    const uz::Entity e = CreateNamedEntity("metallic_entity");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { metallic_multiplier = 0.8 })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_NEAR(mat->metallicMultiplier, 0.8f, 1e-5f);
}

TEST_F(LuaMaterialAPITest, SetMaterial_NormalIntensity_WrittenToComponent) {
    const uz::Entity e = CreateNamedEntity("normal_entity");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { normal_intensity = 1.5 })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_NEAR(mat->normalIntensity, 1.5f, 1e-5f);
}

TEST_F(LuaMaterialAPITest, SetMaterial_Specular_WrittenToComponent) {
    const uz::Entity e = CreateNamedEntity("specular_entity");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { specular = 0.3 })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_NEAR(mat->specular, 0.3f, 1e-5f);
}

TEST_F(LuaMaterialAPITest, SetMaterial_EnvMultiplier_WrittenToComponent) {
    const uz::Entity e = CreateNamedEntity("env_entity");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { env_multiplier = 2.0 })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_NEAR(mat->envMultiplier, 2.0f, 1e-5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.scene.set_material — subsurface parameters
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaMaterialAPITest, SetMaterial_SubsurfaceColor_WrittenToComponent) {
    const uz::Entity e = CreateNamedEntity("sss_entity");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, {\n"
        "  subsurface_color_r = 0.9,\n"
        "  subsurface_color_g = 0.3,\n"
        "  subsurface_color_b = 0.2\n"
        "})"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_NEAR(mat->subsurfaceColorR, 0.9f, 1e-5f);
    EXPECT_NEAR(mat->subsurfaceColorG, 0.3f, 1e-5f);
    EXPECT_NEAR(mat->subsurfaceColorB, 0.2f, 1e-5f);
}

TEST_F(LuaMaterialAPITest, SetMaterial_SubsurfaceRadius_WrittenToComponent) {
    const uz::Entity e = CreateNamedEntity("sss_radius_entity");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { subsurface_radius = 3.5 })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_NEAR(mat->subsurfaceRadius, 3.5f, 1e-5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.scene.set_material — boolean flags
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaMaterialAPITest, SetMaterial_UseORM_True_SetsFlag) {
    const uz::Entity e = CreateNamedEntity("orm_entity");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { use_orm = true })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_TRUE(mat->useORM);
}

TEST_F(LuaMaterialAPITest, SetMaterial_TwoSided_True_SetsFlag) {
    const uz::Entity e = CreateNamedEntity("twosided_entity");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { two_sided = true })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_TRUE(mat->twoSided);
}

TEST_F(LuaMaterialAPITest, SetMaterial_AlphaTest_True_SetsFlag) {
    const uz::Entity e = CreateNamedEntity("alphatest_entity");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { alpha_test = true })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_TRUE(mat->alphaTest);
}

TEST_F(LuaMaterialAPITest, SetMaterial_Flags_DefaultToFalse_WhenNotSpecified) {
    const uz::Entity e = CreateNamedEntity("default_flags_entity");
    PushEntityAsGlobal(e, "me");

    // Supply no boolean flags — defaults must be false.
    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { roughness_multiplier = 1.0 })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_FALSE(mat->twoSided);
    EXPECT_FALSE(mat->alphaTest);
    EXPECT_FALSE(mat->useORM);
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.scene.set_material — shading_model enum string
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaMaterialAPITest, SetMaterial_ShadingModel_Subsurface) {
    const uz::Entity e = CreateNamedEntity("sm_subsurface");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { shading_model = 'subsurface' })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_EQ(mat->shadingModel, uz::EShadingModel::Subsurface);
}

TEST_F(LuaMaterialAPITest, SetMaterial_ShadingModel_Unlit) {
    const uz::Entity e = CreateNamedEntity("sm_unlit");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { shading_model = 'unlit' })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_EQ(mat->shadingModel, uz::EShadingModel::Unlit);
}

TEST_F(LuaMaterialAPITest, SetMaterial_ShadingModel_DefaultLit) {
    const uz::Entity e = CreateNamedEntity("sm_default_lit");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { shading_model = 'default_lit' })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_EQ(mat->shadingModel, uz::EShadingModel::DefaultLit);
}

TEST_F(LuaMaterialAPITest, SetMaterial_ShadingModel_ClearCoat) {
    const uz::Entity e = CreateNamedEntity("sm_clear_coat");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { shading_model = 'clear_coat' })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_EQ(mat->shadingModel, uz::EShadingModel::ClearCoat);
}

TEST_F(LuaMaterialAPITest, SetMaterial_ShadingModel_TwoSidedFoliage) {
    const uz::Entity e = CreateNamedEntity("sm_foliage");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { shading_model = 'two_sided_foliage' })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_EQ(mat->shadingModel, uz::EShadingModel::TwoSidedFoliage);
}

TEST_F(LuaMaterialAPITest, SetMaterial_ShadingModel_Hair) {
    const uz::Entity e = CreateNamedEntity("sm_hair");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { shading_model = 'hair' })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_EQ(mat->shadingModel, uz::EShadingModel::Hair);
}

TEST_F(LuaMaterialAPITest, SetMaterial_ShadingModel_Cloth) {
    const uz::Entity e = CreateNamedEntity("sm_cloth");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { shading_model = 'cloth' })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_EQ(mat->shadingModel, uz::EShadingModel::Cloth);
}

TEST_F(LuaMaterialAPITest, SetMaterial_ShadingModel_Eye) {
    const uz::Entity e = CreateNamedEntity("sm_eye");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { shading_model = 'eye' })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_EQ(mat->shadingModel, uz::EShadingModel::Eye);
}

TEST_F(LuaMaterialAPITest, SetMaterial_ShadingModel_ThinTranslucent) {
    const uz::Entity e = CreateNamedEntity("sm_thin_trans");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { shading_model = 'thin_translucent' })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_EQ(mat->shadingModel, uz::EShadingModel::ThinTranslucent);
}

TEST_F(LuaMaterialAPITest, SetMaterial_ShadingModel_UnknownString_DoesNotCrash) {
    const uz::Entity e = CreateNamedEntity("sm_unknown");
    PushEntityAsGlobal(e, "me");

    // Unknown shading model must log a warning but not Lua-error or crash.
    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { shading_model = 'totally_unknown_model' })"
    ));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.scene.set_material — blend_mode enum string
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaMaterialAPITest, SetMaterial_BlendMode_Translucent) {
    const uz::Entity e = CreateNamedEntity("bm_translucent");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { blend_mode = 'translucent' })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_EQ(mat->blendMode, uz::EBlendMode::Translucent);
}

TEST_F(LuaMaterialAPITest, SetMaterial_BlendMode_Opaque) {
    const uz::Entity e = CreateNamedEntity("bm_opaque");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { blend_mode = 'opaque' })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_EQ(mat->blendMode, uz::EBlendMode::Opaque);
}

TEST_F(LuaMaterialAPITest, SetMaterial_BlendMode_Masked) {
    const uz::Entity e = CreateNamedEntity("bm_masked");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { blend_mode = 'masked' })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_EQ(mat->blendMode, uz::EBlendMode::Masked);
}

TEST_F(LuaMaterialAPITest, SetMaterial_BlendMode_Additive) {
    const uz::Entity e = CreateNamedEntity("bm_additive");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { blend_mode = 'additive' })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_EQ(mat->blendMode, uz::EBlendMode::Additive);
}

TEST_F(LuaMaterialAPITest, SetMaterial_BlendMode_Modulate) {
    const uz::Entity e = CreateNamedEntity("bm_modulate");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { blend_mode = 'modulate' })"
    ));

    const auto& mc = world_.GetComponent<uz::StaticMeshComponent>(e);
    const auto* mat = static_cast<const uz::Material*>(mc.GetMaterial(0));
    EXPECT_EQ(mat->blendMode, uz::EBlendMode::Modulate);
}

TEST_F(LuaMaterialAPITest, SetMaterial_BlendMode_UnknownString_DoesNotCrash) {
    const uz::Entity e = CreateNamedEntity("bm_unknown");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { blend_mode = 'totally_unknown_blend' })"
    ));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.scene.set_material — error handling
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaMaterialAPITest, SetMaterial_NilEntity_DoesNotCrash) {
    EXPECT_TRUE(DoString("uz.scene.set_material(nil, { roughness_multiplier = 0.5 })"));
}

TEST_F(LuaMaterialAPITest, SetMaterial_NonTableArg2_DoesNotCrash) {
    const uz::Entity e = CreateNamedEntity("non_table_arg");
    PushEntityAsGlobal(e, "me");

    // Passing a string instead of a table must warn and be a no-op.
    EXPECT_TRUE(DoString("uz.scene.set_material(me, 'not_a_table')"));
}

TEST_F(LuaMaterialAPITest, SetMaterial_NoWorld_DoesNotCrash) {
    // Temporarily remove the world pointer.
    uz::LuaSetWorld(uz::LuaVM::GetState(), nullptr);

    const uz::Entity e = CreateNamedEntity("no_world_mat");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString("uz.scene.set_material(me, { roughness_multiplier = 0.5 })"));

    // Restore world for TearDown.
    uz::LuaSetWorld(uz::LuaVM::GetState(), &world_);
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.scene.get_material — reading back material parameters
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaMaterialAPITest, GetMaterial_NoComponent_ReturnsNil) {
    const uz::Entity e = CreateNamedEntity("get_mat_none");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(EvalBool("uz.scene.get_material(me) == nil"));
}

TEST_F(LuaMaterialAPITest, GetMaterial_ComponentNotLoaded_ReturnsNil) {
    const uz::Entity e = CreateNamedEntity("get_mat_not_loaded");
    world_.AddComponent<uz::StaticMeshComponent>(e);
    world_.GetComponent<uz::StaticMeshComponent>(e).materialLoaded = false;
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(EvalBool("uz.scene.get_material(me) == nil"));
}

TEST_F(LuaMaterialAPITest, GetMaterial_AfterSetMaterial_ReturnsTable) {
    const uz::Entity e = CreateNamedEntity("get_mat_table");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { roughness_multiplier = 0.5 })"
    ));

    EXPECT_TRUE(EvalBool("type(uz.scene.get_material(me)) == 'table'"));
}

TEST_F(LuaMaterialAPITest, GetMaterial_RoughnessMultiplier_RoundTrips) {
    const uz::Entity e = CreateNamedEntity("round_rough");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { roughness_multiplier = 0.42 })"
    ));

    const double val = EvalNumber("uz.scene.get_material(me).roughness_multiplier");
    EXPECT_NEAR(val, 0.42, 1e-5);
}

TEST_F(LuaMaterialAPITest, GetMaterial_MetallicMultiplier_RoundTrips) {
    const uz::Entity e = CreateNamedEntity("round_metal");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { metallic_multiplier = 0.77 })"
    ));

    const double val = EvalNumber("uz.scene.get_material(me).metallic_multiplier");
    EXPECT_NEAR(val, 0.77, 1e-5);
}

TEST_F(LuaMaterialAPITest, GetMaterial_Specular_RoundTrips) {
    const uz::Entity e = CreateNamedEntity("round_specular");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { specular = 0.35 })"
    ));

    const double val = EvalNumber("uz.scene.get_material(me).specular");
    EXPECT_NEAR(val, 0.35, 1e-5);
}

TEST_F(LuaMaterialAPITest, GetMaterial_EnvMultiplier_RoundTrips) {
    const uz::Entity e = CreateNamedEntity("round_env");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { env_multiplier = 1.75 })"
    ));

    const double val = EvalNumber("uz.scene.get_material(me).env_multiplier");
    EXPECT_NEAR(val, 1.75, 1e-5);
}

TEST_F(LuaMaterialAPITest, GetMaterial_SubsurfaceFields_PresentInTable) {
    const uz::Entity e = CreateNamedEntity("round_sss");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { subsurface_radius = 2.5 })"
    ));

    EXPECT_TRUE(EvalBool(
        "(function()\n"
        "  local m = uz.scene.get_material(me)\n"
        "  return m ~= nil\n"
        "     and m.subsurface_color_r ~= nil\n"
        "     and m.subsurface_color_g ~= nil\n"
        "     and m.subsurface_color_b ~= nil\n"
        "     and m.subsurface_radius ~= nil\n"
        "end)()"
    ));
}

TEST_F(LuaMaterialAPITest, GetMaterial_SubsurfaceRadius_RoundTrips) {
    const uz::Entity e = CreateNamedEntity("round_sss_r");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { subsurface_radius = 4.2 })"
    ));

    const double val = EvalNumber("uz.scene.get_material(me).subsurface_radius");
    EXPECT_NEAR(val, 4.2, 1e-5);
}

TEST_F(LuaMaterialAPITest, GetMaterial_UseORM_RoundTrips) {
    const uz::Entity e = CreateNamedEntity("round_orm");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { use_orm = true })"
    ));

    EXPECT_TRUE(EvalBool("uz.scene.get_material(me).use_orm == true"));
}

TEST_F(LuaMaterialAPITest, GetMaterial_ShadingModel_RoundTrips) {
    const uz::Entity e = CreateNamedEntity("round_sm");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { shading_model = 'subsurface' })"
    ));

    const std::string sm = EvalString("uz.scene.get_material(me).shading_model");
    EXPECT_EQ(sm, "Subsurface");
}

TEST_F(LuaMaterialAPITest, GetMaterial_BlendMode_RoundTrips) {
    const uz::Entity e = CreateNamedEntity("round_bm");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { blend_mode = 'translucent' })"
    ));

    const std::string bm = EvalString("uz.scene.get_material(me).blend_mode");
    EXPECT_EQ(bm, "Translucent");
}

TEST_F(LuaMaterialAPITest, GetMaterial_MaterialDomain_RoundTrips) {
    const uz::Entity e = CreateNamedEntity("round_md");
    PushEntityAsGlobal(e, "me");

    // Default domain is "surface" — verify without changing it.
    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { material_domain = 'surface' })"
    ));

    const std::string md = EvalString("uz.scene.get_material(me).material_domain");
    EXPECT_EQ(md, "Surface");
}

TEST_F(LuaMaterialAPITest, GetMaterial_NilEntity_ReturnsNil) {
    EXPECT_TRUE(EvalBool("uz.scene.get_material(nil) == nil"));
}

// ─────────────────────────────────────────────────────────────────────────────
// materialLoaded flag
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaMaterialAPITest, SetMaterial_SetsMaterialLoadedFlagOnComponent) {
    const uz::Entity e = CreateNamedEntity("mat_loaded_flag");
    PushEntityAsGlobal(e, "me");

    EXPECT_TRUE(DoString(
        "uz.scene.set_material(me, { roughness_multiplier = 1.0 })"
    ));

    ASSERT_TRUE(world_.HasComponent<uz::StaticMeshComponent>(e));
    EXPECT_TRUE(world_.GetComponent<uz::StaticMeshComponent>(e).materialLoaded);
}
