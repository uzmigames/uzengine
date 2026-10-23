/// Lua file access must stay inside the VFS root.
///
/// Scripts are untrusted project content. These tests drive `uz.vfs.*`,
/// `uz.scene.load_scene`/`save_scene` and `uz.vfx:spawn_composite` against a
/// real `LooseFileVFS` rooted in a temp
/// directory that has a secret file next to it, and check that `..`,
/// absolute and NUL-carrying paths neither read the secret nor write outside
/// the root, while ordinary paths inside the root keep working.

#include "lua_vm.h"
#include "lua_scene_api.h"
#include "lua_scene_io_api.h"
#include "world.h"
#include "name_component.h"
#include "scene_component.h"
#include "vfs.h"
#include "engine/scripting/scene/vfx_lua_bindings.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/vfx_system.h"
#include "mock_rhi_device.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

namespace fs = std::filesystem;

class LuaVfsContainmentTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_base = fs::temp_directory_path() / "uz_test_lua_vfs_containment";
        std::error_code ec;
        fs::remove_all(m_base, ec);
        m_root = m_base / "content";
        fs::create_directories(m_root / "scenes");
        WriteText(m_base / "secret", "top secret");
        WriteText(m_root / "data" / "ok.txt", "ok");

        m_vfs = std::make_unique<uz::LooseFileVFS>(m_root.string());
        ASSERT_TRUE(uz::LuaVM::Init());
        uz::LuaSetWorld(uz::LuaVM::GetState(), &m_world);
        uz::LuaSetVFS(uz::LuaVM::GetState(), m_vfs.get());
    }

    void TearDown() override {
        uz::LuaVM::Shutdown();
        m_vfs.reset();
        std::error_code ec;
        fs::remove_all(m_base, ec);
    }

    static void WriteText(const fs::path& path, const std::string& text) {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file << text;
    }

    /// Evaluates a Lua expression; a Lua error fails the test and yields false.
    bool EvalBool(const std::string& expr) {
        lua_State* L = uz::LuaVM::GetState();
        const std::string code = "return " + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            const char* err = lua_tostring(L, -1);
            ADD_FAILURE() << "Lua error: " << (err ? err : "unknown");
            lua_pop(L, 1);
            return false;
        }
        const bool result = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        return result;
    }

    /// Exposes a C++ string (may hold NUL bytes) to Lua as global `name`.
    void SetGlobalString(const char* name, const std::string& value) {
        lua_State* L = uz::LuaVM::GetState();
        lua_pushlstring(L, value.data(), value.size());
        lua_setglobal(L, name);
    }

    /// Adds an entity so `SaveScene` has something to serialize.
    void AddSaveableEntity() {
        auto e = m_world.CreateEntity();
        m_world.AddComponent<uz::CName>(e, uz::CName{"saveable"});
        auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(e.id));
        m_world.GetRegistry().emplace<uz::SceneComponent>(enttEntity);
    }

    static std::string ForwardSlashes(const fs::path& path) {
        std::string text = path.string();
        for (char& c : text) {
            if (c == '\\') c = '/';
        }
        return text;
    }

    uz::World m_world;
    std::unique_ptr<uz::LooseFileVFS> m_vfs;
    fs::path m_base;
    fs::path m_root;
};

// ─────────────────────────────────────────────────────────────────────────────
// uz.vfs.read / read_text / exists
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaVfsContainmentTest, ReadInsideRootStillWorks) {
    EXPECT_TRUE(EvalBool("uz.vfs.read('data/ok.txt') == 'ok'"));
    EXPECT_TRUE(EvalBool("uz.vfs.read_text('data/ok.txt') == 'ok'"));
    EXPECT_TRUE(EvalBool("uz.vfs.exists('data/ok.txt') == true"));
}

TEST_F(LuaVfsContainmentTest, ReadParentEscapeReturnsNil) {
    ASSERT_TRUE(fs::exists(m_base / "secret"));
    EXPECT_TRUE(EvalBool("uz.vfs.read('../../secret') == nil"));
    EXPECT_TRUE(EvalBool("uz.vfs.read('../secret') == nil"));
    EXPECT_TRUE(EvalBool("uz.vfs.read('data/../../secret') == nil"));
    EXPECT_TRUE(EvalBool("uz.vfs.read_text('../secret') == nil"));
    EXPECT_TRUE(EvalBool("uz.vfs.exists('../secret') == false"));
}

TEST_F(LuaVfsContainmentTest, ReadAbsoluteAndDrivePathsReturnNil) {
    SetGlobalString("_secret_abs", ForwardSlashes(m_base / "secret"));
    EXPECT_TRUE(EvalBool("uz.vfs.read(_secret_abs) == nil"));
    EXPECT_TRUE(EvalBool("uz.vfs.read_text(_secret_abs) == nil"));
    EXPECT_TRUE(EvalBool("uz.vfs.exists(_secret_abs) == false"));
    EXPECT_TRUE(EvalBool("uz.vfs.read('C:secret') == nil"));
    EXPECT_TRUE(EvalBool("uz.vfs.read('\\\\\\\\server\\\\share\\\\secret') == nil"));
}

TEST_F(LuaVfsContainmentTest, EmbeddedNulCannotTruncatePath) {
    // Lua strings carry their length: "data/ok.txt\0..." must be rejected as
    // a whole, not shortened to "data/ok.txt" at the C boundary.
    SetGlobalString("_nul_path", std::string("data/ok.txt\0/../../secret", 25));
    EXPECT_TRUE(EvalBool("uz.vfs.read(_nul_path) == nil"));
    EXPECT_TRUE(EvalBool("uz.vfs.read_text(_nul_path) == nil"));
    EXPECT_TRUE(EvalBool("uz.vfs.exists(_nul_path) == false"));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.scene.save_scene
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaVfsContainmentTest, SaveSceneInsideRootWritesFile) {
    AddSaveableEntity();
    EXPECT_TRUE(EvalBool("uz.scene.save_scene('scenes/saved.uzscene') == true"));
    EXPECT_TRUE(fs::is_regular_file(m_root / "scenes" / "saved.uzscene"));
}

TEST_F(LuaVfsContainmentTest, SaveSceneParentEscapeIsRefused) {
    AddSaveableEntity();
    EXPECT_TRUE(EvalBool("uz.scene.save_scene('../evil.uzscene') == false"));
    EXPECT_TRUE(EvalBool("uz.scene.save_scene('scenes/../../evil.uzscene') == false"));
    EXPECT_FALSE(fs::exists(m_base / "evil.uzscene"));
}

TEST_F(LuaVfsContainmentTest, SaveSceneCannotOverwriteFileOutsideRoot) {
    AddSaveableEntity();
    SetGlobalString("_secret_abs", ForwardSlashes(m_base / "secret"));
    EXPECT_TRUE(EvalBool("uz.scene.save_scene(_secret_abs) == false"));
    EXPECT_TRUE(EvalBool("uz.scene.save_scene('../secret') == false"));

    std::ifstream secret(m_base / "secret", std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(secret)), std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, "top secret");
}

TEST_F(LuaVfsContainmentTest, SaveSceneNulPathIsRefused) {
    AddSaveableEntity();
    SetGlobalString("_nul_save", std::string("scenes/a.uzscene\0/../../evil", 28));
    EXPECT_TRUE(EvalBool("uz.scene.save_scene(_nul_save) == false"));
    EXPECT_FALSE(fs::exists(m_root / "scenes" / "a.uzscene"));
}

TEST_F(LuaVfsContainmentTest, SaveSceneWithoutFilesystemMappingIsRefused) {
    // A layered/pak VFS has no single directory to write into.
    AddSaveableEntity();
    uz::LayeredVFS layered;
    layered.Mount(std::make_shared<uz::LooseFileVFS>(m_root.string()), 0);
    uz::LuaSetVFS(uz::LuaVM::GetState(), &layered);

    EXPECT_TRUE(EvalBool("uz.scene.save_scene('scenes/layered.uzscene') == false"));
    EXPECT_FALSE(fs::exists(m_root / "scenes" / "layered.uzscene"));

    uz::LuaSetVFS(uz::LuaVM::GetState(), m_vfs.get());
}

TEST_F(LuaVfsContainmentTest, LoadSceneParentEscapeIsRefused) {
    WriteText(m_base / "outside.uzscene", R"({"entities":[{"name":"outside"}]})");
    EXPECT_TRUE(EvalBool("uz.scene.load_scene('../outside.uzscene') == false"));
    EXPECT_EQ(m_world.EntityCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.vfx:spawn_composite
// ─────────────────────────────────────────────────────────────────────────────

/// Adds the uz.vfx bindings and one CPU-only emitter whose graph path matches
/// the composite written by each test, so a composite that loads activates it.
class LuaVfxCompositeContainmentTest : public LuaVfsContainmentTest {
protected:
    void SetUp() override {
        LuaVfsContainmentTest::SetUp();
        m_vfx.Initialize(&m_device, 100000u);
        uz::RegisterVFXLuaBindings(uz::LuaVM::GetState(), &m_vfx);

        m_fire.graphPath = "vfx/fire.uzvfx";
        m_fire.maxParticles = 1000u;
        m_fire.isEmitting = false;
        m_fire.initialized = true;  // CPU-only test: no GPU resources
        m_vfx.RegisterEmitter(&m_fire);
    }

    void TearDown() override {
        m_vfx.UnregisterEmitter(&m_fire);
        uz::UnregisterVFXLuaBindings(uz::LuaVM::GetState());
        m_vfx.Shutdown(&m_device);
        LuaVfsContainmentTest::TearDown();
    }

    static constexpr const char* kComposite = R"({
  "format": "uzvfx_composite",
  "version": 1,
  "name": "burst",
  "emitters": [
    { "name": "fire", "graph": "vfx/fire.uzvfx", "offset": [0,0,0], "delay": 0.0 }
  ]
})";

    uz::test::MockRHIDevice m_device;
    uz::VFXSystem m_vfx;
    uz::ParticleEmitter m_fire;
};

TEST_F(LuaVfxCompositeContainmentTest, CompositeInsideRootActivatesEmitter) {
    WriteText(m_root / "vfx" / "burst.uzvfx_composite", kComposite);
    EXPECT_TRUE(EvalBool("#uz.vfx:spawn_composite('vfx/burst.uzvfx_composite', {0,0,0}) == 1"));
    EXPECT_TRUE(m_fire.isEmitting);
}

TEST_F(LuaVfxCompositeContainmentTest, CompositeOutsideRootIsRefused) {
    WriteText(m_base / "outside.uzvfx_composite", kComposite);
    SetGlobalString("_outside_abs", ForwardSlashes(m_base / "outside.uzvfx_composite"));

    EXPECT_TRUE(EvalBool("#uz.vfx:spawn_composite('../outside.uzvfx_composite', {0,0,0}) == 0"));
    EXPECT_TRUE(EvalBool("#uz.vfx:spawn_composite(_outside_abs, {0,0,0}) == 0"));
    EXPECT_FALSE(m_fire.isEmitting);
}

TEST_F(LuaVfxCompositeContainmentTest, CompositeWithoutVfsIsNotReadFromDisk) {
    WriteText(m_root / "vfx" / "burst.uzvfx_composite", kComposite);
    SetGlobalString("_inside_abs", ForwardSlashes(m_root / "vfx" / "burst.uzvfx_composite"));
    uz::LuaSetVFS(uz::LuaVM::GetState(), nullptr);

    EXPECT_TRUE(EvalBool("#uz.vfx:spawn_composite(_inside_abs, {0,0,0}) == 0"));
    EXPECT_FALSE(m_fire.isEmitting);
}
