/// Unit tests for the uz.scene I/O Lua API:
///   uz.scene.load_scene, uz.scene.save_scene.
///
/// The functions require both a World* and an IVFS* to be stored in the Lua
/// registry. Tests cover the "no world" and "no VFS" guard paths, as well as
/// the "file not found" and "empty world" paths, using a minimal stub IVFS
/// implementation to avoid real disk I/O inside unit tests.
///
/// Pattern: LuaVM::Init() in SetUp, LuaVM::Shutdown() in TearDown,
/// Lua snippets executed via luaL_dostring, results verified with EXPECT_*.

#include "lua_vm.h"
#include "lua_scene_api.h"
#include "lua_scene_io_api.h"
#include "world.h"
#include "name_component.h"
#include "scene_component.h"
#include "vfs.h"
#include "../math/vector3.h"

#include <gtest/gtest.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

// ─────────────────────────────────────────────────────────────────────────────
// Stub IVFS — returns canned byte buffers for known paths; empty for others
// ─────────────────────────────────────────────────────────────────────────────

/// Minimal in-memory VFS stub used to test scene I/O without touching disk.
class StubVFS final : public uz::IVFS {
public:
    /// Register a named file with given content bytes.
    void AddFile(const std::string& path, const std::vector<uint8_t>& content) {
        files_[path] = content;
    }

    /// Register a named file with string content (convenience overload).
    void AddFile(const std::string& path, const std::string& content) {
        files_[path] = std::vector<uint8_t>(content.begin(), content.end());
    }

    [[nodiscard]] std::vector<uint8_t> Read(const std::string& path) override {
        const auto it = files_.find(path);
        if (it == files_.end()) return {};
        return it->second;
    }

    [[nodiscard]] bool Exists(const std::string& path) override {
        return files_.count(path) > 0;
    }

    [[nodiscard]] std::string Name() const override { return "StubVFS"; }

    /// ResolvePath returns the path unchanged (stub has no real filesystem root).
    [[nodiscard]] std::string ResolvePath(const std::string& path) const override {
        return path;
    }

private:
    std::unordered_map<std::string, std::vector<uint8_t>> files_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Test Fixture
// ─────────────────────────────────────────────────────────────────────────────

class LuaSceneIoAPITest : public ::testing::Test {
protected:
    uz::World world_;
    StubVFS   vfs_;

    void SetUp() override {
        ASSERT_TRUE(uz::LuaVM::Init());
        // Provide the world pointer but deliberately do NOT set the VFS here.
        // Individual tests choose whether to install the VFS.
        uz::LuaSetWorld(uz::LuaVM::GetState(), &world_);
    }

    void TearDown() override {
        uz::LuaVM::Shutdown();
    }

    /// Convenience: install the stub VFS into the Lua registry.
    void InstallVFS() {
        uz::LuaSetVFS(uz::LuaVM::GetState(), &vfs_);
    }

    /// Convenience: remove the VFS from the Lua registry (set to nullptr).
    void RemoveVFS() {
        uz::LuaSetVFS(uz::LuaVM::GetState(), nullptr);
    }

    /// Convenience: remove the World pointer from the Lua registry.
    void RemoveWorld() {
        uz::LuaSetWorld(uz::LuaVM::GetState(), nullptr);
    }

    /// Restore the World pointer after tests that clear it.
    void RestoreWorld() {
        uz::LuaSetWorld(uz::LuaVM::GetState(), &world_);
    }

    /// Runs Lua code; returns true on success. Reports Lua errors via ADD_FAILURE.
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

    /// Evaluates a Lua expression and returns it as a bool.
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

    /// Creates a named entity with a SceneComponent at the given position.
    uz::Entity CreateNamedEntity(const std::string& name,
                                  const uz::Vec3& pos = {0.f, 0.f, 0.f}) {
        auto e = world_.CreateEntity();
        world_.AddComponent<uz::CName>(e, uz::CName{name});
        auto enttEntity = static_cast<entt::entity>(static_cast<uint32_t>(e.id));
        auto& sc = world_.GetRegistry().emplace<uz::SceneComponent>(enttEntity);
        sc.SetPosition(pos);
        return e;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Function existence
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneIoAPITest, LoadScene_FunctionExists) {
    EXPECT_TRUE(EvalBool("type(uz.scene.load_scene) == 'function'"));
}

TEST_F(LuaSceneIoAPITest, SaveScene_FunctionExists) {
    EXPECT_TRUE(EvalBool("type(uz.scene.save_scene) == 'function'"));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.scene.load_scene — guard conditions (no World / no VFS)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneIoAPITest, LoadScene_NoWorld_ReturnsFalse) {
    // VFS is not set either — but the World guard must trigger first.
    RemoveWorld();

    EXPECT_TRUE(EvalBool("uz.scene.load_scene('scenes/any.uzscene') == false"));

    RestoreWorld();
}

TEST_F(LuaSceneIoAPITest, LoadScene_NoVFS_ReturnsFalse) {
    // World is set (from SetUp), but VFS is NOT installed → returns false.
    EXPECT_TRUE(EvalBool("uz.scene.load_scene('scenes/any.uzscene') == false"));
}

TEST_F(LuaSceneIoAPITest, LoadScene_FileNotFound_ReturnsFalse) {
    InstallVFS();
    // VFS has no files → returns empty bytes → returns false.
    EXPECT_TRUE(EvalBool("uz.scene.load_scene('scenes/nonexistent.uzscene') == false"));
}

TEST_F(LuaSceneIoAPITest, LoadScene_FileNotFound_DoesNotCrash) {
    InstallVFS();
    // Must not throw or crash — just log and return false.
    EXPECT_TRUE(DoString("local ok = uz.scene.load_scene('no/such/file.uzscene')"));
}

TEST_F(LuaSceneIoAPITest, LoadScene_EmptyFileContent_ReturnsFalse) {
    InstallVFS();
    // File exists but contains zero bytes → treated as "not found".
    vfs_.AddFile("scenes/empty.uzscene", std::vector<uint8_t>{});

    EXPECT_TRUE(EvalBool("uz.scene.load_scene('scenes/empty.uzscene') == false"));
}

TEST_F(LuaSceneIoAPITest, LoadScene_MalformedContent_ReturnsNilAndMessage) {
    InstallVFS();
    // Malformed input returns a typed failure to Lua.
    vfs_.AddFile("scenes/bad.uzscene", "{ this is not valid scene data !!!");

    EXPECT_TRUE(DoString("local ok, message = uz.scene.load_scene('scenes/bad.uzscene'); "
                        "assert(ok == nil and type(message) == 'string' and #message > 0)"));
}

TEST_F(LuaSceneIoAPITest, Scene_LoadScene_Malformed_PreservesPreviousScene) {
    InstallVFS();
    const auto handle = CreateNamedEntity("previous");
    vfs_.AddFile("scenes/malformed.uzscene", "{not json");
    EXPECT_TRUE(DoString("local ok, message = uz.scene.load_scene('scenes/malformed.uzscene'); "
                        "assert(ok == nil and type(message) == 'string')"));
    EXPECT_EQ(world_.EntityCount(), 1u);
    EXPECT_TRUE(world_.IsValid(handle));
}

TEST_F(LuaSceneIoAPITest, LoadScene_EmptyEntities_ReturnsTrueAndReplacesWorld) {
    InstallVFS();
    const auto handle = CreateNamedEntity("previous");
    vfs_.AddFile("scenes/empty-valid.uzscene", R"({"entities":[]})");
    EXPECT_TRUE(EvalBool("uz.scene.load_scene('scenes/empty-valid.uzscene') == true"));
    EXPECT_EQ(world_.EntityCount(), 0u);
    EXPECT_FALSE(world_.IsValid(handle));
}

TEST_F(LuaSceneIoAPITest, LoadScene_PrefabResolutionMatchesLegacyVfslessBehavior) {
    InstallVFS();
    vfs_.AddFile("scenes/with-prefab.uzscene",
                 R"({"entities":[{"name":"parent","prefab":{"asset":"child.uzprefab"}}]})");
    vfs_.AddFile("child.uzprefab", R"({"children":[{"name":"child"}]})");
    EXPECT_TRUE(EvalBool("uz.scene.load_scene('scenes/with-prefab.uzscene') == true"));
    // The legacy Lua loader did not pass the VFS into entity construction.
    EXPECT_EQ(world_.EntityCount(), 1u);
}

TEST_F(LuaSceneIoAPITest, LoadScene_MalformedContent_DoesNotCrash) {
    InstallVFS();
    vfs_.AddFile("scenes/bad2.uzscene", "@@@ garbage @@@");

    EXPECT_TRUE(DoString("local ok = uz.scene.load_scene('scenes/bad2.uzscene')"));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.scene.load_scene — World clearing behaviour
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneIoAPITest, LoadScene_FileNotFound_PreservesPreviousScene) {
    InstallVFS();

    // Pre-populate the world with entities.
    const auto a = CreateNamedEntity("pre_existing_a");
    const auto b = CreateNamedEntity("pre_existing_b");
    ASSERT_EQ(world_.EntityCount(), 2u);

    EXPECT_TRUE(EvalBool("uz.scene.load_scene('scenes/not_here.uzscene') == false"));
    EXPECT_EQ(world_.EntityCount(), 2u);
    EXPECT_TRUE(world_.IsValid(a));
    EXPECT_TRUE(world_.IsValid(b));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.scene.save_scene — guard conditions (no World / no VFS)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneIoAPITest, SaveScene_NoWorld_ReturnsFalse) {
    RemoveWorld();

    EXPECT_TRUE(EvalBool("uz.scene.save_scene('scenes/out.uzscene') == false"));

    RestoreWorld();
}

TEST_F(LuaSceneIoAPITest, SaveScene_NoVFS_ReturnsFalse) {
    // World is set from SetUp but no VFS → returns false.
    EXPECT_TRUE(EvalBool("uz.scene.save_scene('scenes/out.uzscene') == false"));
}

TEST_F(LuaSceneIoAPITest, SaveScene_NoWorld_DoesNotCrash) {
    RemoveWorld();

    EXPECT_TRUE(DoString("local ok = uz.scene.save_scene('scenes/out.uzscene')"));

    RestoreWorld();
}

TEST_F(LuaSceneIoAPITest, SaveScene_NoVFS_DoesNotCrash) {
    EXPECT_TRUE(DoString("local ok = uz.scene.save_scene('scenes/out.uzscene')"));
}

TEST_F(LuaSceneIoAPITest, SaveScene_EmptyWorld_ReturnsFalse) {
    InstallVFS();
    // World has no entities → SaveScene returns empty data → returns false.
    ASSERT_EQ(world_.EntityCount(), 0u);

    EXPECT_TRUE(EvalBool("uz.scene.save_scene('scenes/empty_world.uzscene') == false"));
}

TEST_F(LuaSceneIoAPITest, SaveScene_EmptyWorld_DoesNotCrash) {
    InstallVFS();
    EXPECT_TRUE(DoString("local ok = uz.scene.save_scene('scenes/no_crash.uzscene')"));
}

// ─────────────────────────────────────────────────────────────────────────────
// uz.scene.save_scene — write-failure guard
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneIoAPITest, SaveScene_InvalidPath_ReturnsFalse) {
    InstallVFS();

    // Add an entity so SaveScene has data to write.
    CreateNamedEntity("saveable_entity");

    // StubVFS::ResolvePath returns the path unchanged, so fopen on a path like
    // "///:bad:::" will fail on all OS, causing the write to fail → false.
    EXPECT_TRUE(EvalBool("uz.scene.save_scene('///:bad_path:::') == false"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Stack hygiene — no Lua stack leaks after I/O calls
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneIoAPITest, LoadScene_DoesNotLeakLuaStack) {
    InstallVFS();

    lua_State* L = uz::LuaVM::GetState();
    const int topBefore = lua_gettop(L);

    DoString("local _ = uz.scene.load_scene('scenes/nofile.uzscene')");

    EXPECT_EQ(lua_gettop(L), topBefore);
}

TEST_F(LuaSceneIoAPITest, SaveScene_DoesNotLeakLuaStack) {
    lua_State* L = uz::LuaVM::GetState();
    const int topBefore = lua_gettop(L);

    // No VFS set — returns false immediately but must not leak the stack.
    DoString("local _ = uz.scene.save_scene('scenes/nofile.uzscene')");

    EXPECT_EQ(lua_gettop(L), topBefore);
}

// ─────────────────────────────────────────────────────────────────────────────
// Return type validation — must always push exactly one boolean value
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneIoAPITest, LoadScene_ReturnType_IsBoolean) {
    // Whether file exists or not the return type must be boolean.
    EXPECT_TRUE(EvalBool(
        "type(uz.scene.load_scene('anything')) == 'boolean'"
    ));
}

TEST_F(LuaSceneIoAPITest, SaveScene_ReturnType_IsBoolean) {
    EXPECT_TRUE(EvalBool(
        "type(uz.scene.save_scene('anything')) == 'boolean'"
    ));
}

// ─────────────────────────────────────────────────────────────────────────────
// Both World and VFS cleared simultaneously
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneIoAPITest, LoadScene_BothWorldAndVFSNull_ReturnsFalse) {
    RemoveWorld();
    RemoveVFS();

    EXPECT_TRUE(EvalBool("uz.scene.load_scene('scenes/x.uzscene') == false"));

    RestoreWorld();
}

TEST_F(LuaSceneIoAPITest, SaveScene_BothWorldAndVFSNull_ReturnsFalse) {
    RemoveWorld();
    RemoveVFS();

    EXPECT_TRUE(EvalBool("uz.scene.save_scene('scenes/x.uzscene') == false"));

    RestoreWorld();
}

// ─────────────────────────────────────────────────────────────────────────────
// LuaSetVFS — registry key is correctly written and read
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSceneIoAPITest, SetVFS_ToNullptr_MakesLoadReturnFalse) {
    // First install, then remove.
    InstallVFS();
    RemoveVFS();

    EXPECT_TRUE(EvalBool("uz.scene.load_scene('scenes/any.uzscene') == false"));
}

TEST_F(LuaSceneIoAPITest, SetVFS_ToNullptr_MakesSaveReturnFalse) {
    InstallVFS();
    CreateNamedEntity("some_entity");
    RemoveVFS();

    EXPECT_TRUE(EvalBool("uz.scene.save_scene('scenes/any.uzscene') == false"));
}
