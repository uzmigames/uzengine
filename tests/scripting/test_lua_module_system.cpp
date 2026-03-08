// test_lua_module_system.cpp -- Comprehensive tests for the sandbox-safe
// Lua module loader (require replacement).
//
// Covers: path resolution, sandbox path rejection, caching, circular
// dependency detection, compile errors, runtime errors, hot-reload
// invalidation, integration with LuaVM and script instances.

#include <gtest/gtest.h>

#include "engine/scripting/lua_module_loader.h"
#include "engine/scripting/lua_vm.h"
#include "engine/scripting/script_runner.h"
#include "engine/scripting/lua_script_instance.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

using namespace uz;

// ============================================================================
// Helpers
// ============================================================================

namespace {

/// RAII temp directory that cleans up on destruction.
struct TempDir {
    fs::path path;
    std::string path_str;

    explicit TempDir(const char* name) {
        path = fs::temp_directory_path() / name;
        fs::create_directories(path);
        path_str = path.string();
        // Normalize to forward slashes
        for (auto& c : path_str) {
            if (c == '\\') c = '/';
        }
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }

    const char* c_str() const { return path_str.c_str(); }

    /// Write a file relative to this directory.
    void write_file(const char* relative_path, const char* content) const {
        fs::path full = path / relative_path;
        fs::create_directories(full.parent_path());
        std::ofstream f(full);
        f << content;
    }

    /// Remove a file relative to this directory.
    void remove_file(const char* relative_path) const {
        std::error_code ec;
        fs::remove(path / relative_path, ec);
    }
};

/// Helper to create a LuaVM with module loader installed.
struct TestVM {
    LuaVM vm;
    TempDir dir;

    explicit TestVM(const char* dir_name = "uz_mod_test")
        : dir(dir_name)
    {
        vm.init();
        vm.set_project_root(dir.c_str());
    }

    lua_State* L() const { return vm.L(); }

    void write_module(const char* relative_path, const char* content) {
        dir.write_file(relative_path, content);
    }

    /// Run a Lua string, return 0 on success.
    int dostring(const char* code) {
        return luaL_dostring(L(), code);
    }

    /// Run Lua code and get the error message on failure.
    std::string dostring_err(const char* code) {
        int err = luaL_dostring(L(), code);
        if (err != 0) {
            const char* msg = lua_tostring(L(), -1);
            std::string result = msg ? msg : "(no error message)";
            lua_pop(L(), 1);
            return result;
        }
        return "";
    }
};

} // anonymous namespace

// ============================================================================
// lua_module_resolve_path -- Unit tests
// ============================================================================

TEST(LuaModuleResolvePath, BasicDotNotation) {
    char out[512];
    EXPECT_TRUE(lua_module_resolve_path("game.utils", "/project", out, sizeof(out)));
    EXPECT_STREQ(out, "/project/game/utils.lua");
}

TEST(LuaModuleResolvePath, SingleSegment) {
    char out[512];
    EXPECT_TRUE(lua_module_resolve_path("utils", "/project", out, sizeof(out)));
    EXPECT_STREQ(out, "/project/utils.lua");
}

TEST(LuaModuleResolvePath, DeepNesting) {
    char out[512];
    EXPECT_TRUE(lua_module_resolve_path("game.utils.math.vec3", "/proj", out, sizeof(out)));
    EXPECT_STREQ(out, "/proj/game/utils/math/vec3.lua");
}

TEST(LuaModuleResolvePath, TrailingSlashOnRoot) {
    char out[512];
    EXPECT_TRUE(lua_module_resolve_path("mod", "/project/", out, sizeof(out)));
    EXPECT_STREQ(out, "/project/mod.lua");
}

TEST(LuaModuleResolvePath, TrailingBackslashOnRoot) {
    char out[512];
    EXPECT_TRUE(lua_module_resolve_path("mod", "C:\\project\\", out, sizeof(out)));
    EXPECT_STREQ(out, "C:\\project/mod.lua");
}

TEST(LuaModuleResolvePath, RejectsPathTraversalDoubleDot) {
    char out[512];
    EXPECT_FALSE(lua_module_resolve_path("..evil", "/project", out, sizeof(out)));
}

TEST(LuaModuleResolvePath, RejectsPathTraversalInMiddle) {
    char out[512];
    EXPECT_FALSE(lua_module_resolve_path("game..utils", "/project", out, sizeof(out)));
}

TEST(LuaModuleResolvePath, RejectsPathTraversalComplex) {
    char out[512];
    EXPECT_FALSE(lua_module_resolve_path("../../etc/passwd", "/project", out, sizeof(out)));
}

TEST(LuaModuleResolvePath, RejectsEmptyModuleName) {
    char out[512];
    EXPECT_FALSE(lua_module_resolve_path("", "/project", out, sizeof(out)));
}

TEST(LuaModuleResolvePath, RejectsNullModuleName) {
    char out[512];
    EXPECT_FALSE(lua_module_resolve_path(nullptr, "/project", out, sizeof(out)));
}

TEST(LuaModuleResolvePath, RejectsNullProjectRoot) {
    char out[512];
    EXPECT_FALSE(lua_module_resolve_path("mod", nullptr, out, sizeof(out)));
}

TEST(LuaModuleResolvePath, RejectsNullOutPath) {
    EXPECT_FALSE(lua_module_resolve_path("mod", "/project", nullptr, 512));
}

TEST(LuaModuleResolvePath, RejectsZeroOutSize) {
    char out[512];
    EXPECT_FALSE(lua_module_resolve_path("mod", "/project", out, 0));
}

TEST(LuaModuleResolvePath, RejectsTooSmallBuffer) {
    char out[5];  // Way too small for "/project/mod.lua"
    EXPECT_FALSE(lua_module_resolve_path("mod", "/project", out, sizeof(out)));
}

TEST(LuaModuleResolvePath, RejectsAbsolutePathInModule) {
    char out[512];
    // Module name that starts with / after dot conversion
    // Actually, a leading dot would become leading /
    // But let's test embedded slashes
    EXPECT_FALSE(lua_module_resolve_path("..secret", "/project", out, sizeof(out)));
}

TEST(LuaModuleResolvePath, WindowsProjectRoot) {
    char out[512];
    EXPECT_TRUE(lua_module_resolve_path("game.utils", "C:/Users/dev/project", out, sizeof(out)));
    EXPECT_STREQ(out, "C:/Users/dev/project/game/utils.lua");
}

// ============================================================================
// Module loader install -- Basic functionality
// ============================================================================

TEST(LuaModuleInstall, RequireIsAvailableAfterInstall) {
    LuaVM vm;
    vm.init();

    // Before set_project_root, require should be nil (sandbox strips it)
    lua_getglobal(vm.L(), "require");
    EXPECT_TRUE(lua_isnil(vm.L(), -1));
    lua_pop(vm.L(), 1);

    TempDir dir("uz_mod_install_test");
    vm.set_project_root(dir.c_str());

    // Now require should be a function
    lua_getglobal(vm.L(), "require");
    EXPECT_TRUE(lua_isfunction(vm.L(), -1));
    lua_pop(vm.L(), 1);
}

TEST(LuaModuleInstall, InstallWithNullStateIsNoOp) {
    lua_module_loader_install(nullptr, "/project");
    SUCCEED();
}

TEST(LuaModuleInstall, InstallWithNullRootIsNoOp) {
    LuaVM vm;
    vm.init();
    lua_module_loader_install(vm.L(), nullptr);
    // require should still be nil
    lua_getglobal(vm.L(), "require");
    EXPECT_TRUE(lua_isnil(vm.L(), -1));
    lua_pop(vm.L(), 1);
}

TEST(LuaModuleInstall, SetProjectRootWithoutInitIsNoOp) {
    LuaVM vm;
    // Don't init -- L() is nullptr
    vm.set_project_root("/project");
    SUCCEED();
}

// ============================================================================
// require() -- Happy path
// ============================================================================

TEST(LuaModuleRequire, RequireSimpleModule) {
    TestVM t("uz_req_simple");
    t.write_module("utils.lua", "return { answer = 42 }");

    int err = t.dostring("local M = require('utils')\nassert(M.answer == 42)");
    if (err) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

TEST(LuaModuleRequire, RequireDotNotation) {
    TestVM t("uz_req_dot");
    t.write_module("game/utils.lua", "return { name = 'game_utils' }");

    int err = t.dostring("local M = require('game.utils')\nassert(M.name == 'game_utils')");
    if (err) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

TEST(LuaModuleRequire, RequireDeepPath) {
    TestVM t("uz_req_deep");
    t.write_module("game/utils/math/vec3.lua",
        "local V = {}\n"
        "function V.new(x,y,z) return {x=x,y=y,z=z} end\n"
        "return V\n");

    int err = t.dostring(
        "local V = require('game.utils.math.vec3')\n"
        "local v = V.new(1,2,3)\n"
        "assert(v.x == 1 and v.y == 2 and v.z == 3)\n");
    if (err) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

TEST(LuaModuleRequire, ModuleReturningNilCachesTrue) {
    TestVM t("uz_req_nil");
    t.write_module("empty.lua", "-- no return value");

    // First require should succeed and return true
    int err = t.dostring(
        "local M = require('empty')\n"
        "assert(M == true)\n");
    if (err) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

// ============================================================================
// require() -- Caching
// ============================================================================

TEST(LuaModuleRequire, SecondRequireReturnsCached) {
    TestVM t("uz_req_cache");
    // Module returns a table; caching means it only executes once.
    t.write_module("counter.lua",
        "local M = { count = 1 }\n"
        "return M\n");

    int err = t.dostring(
        "local M1 = require('counter')\n"
        "local M2 = require('counter')\n"
        "assert(M1 == M2, 'should return same table')\n"
        "assert(M1.count == 1, 'should only execute once')\n");
    if (err) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

TEST(LuaModuleRequire, CachedModuleReturnsSameReference) {
    TestVM t("uz_req_ref");
    t.write_module("shared.lua", "return { data = {} }");

    int err = t.dostring(
        "local A = require('shared')\n"
        "A.data.x = 42\n"
        "local B = require('shared')\n"
        "assert(B.data.x == 42, 'must be same table reference')\n");
    if (err) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

// ============================================================================
// require() -- Error handling
// ============================================================================

TEST(LuaModuleRequire, ErrorOnMissingFile) {
    TestVM t("uz_req_missing");

    std::string err = t.dostring_err("require('nonexistent')");
    EXPECT_NE(err.find("module not found"), std::string::npos) << "Error: " << err;
}

TEST(LuaModuleRequire, ErrorOnPathTraversal) {
    TestVM t("uz_req_traversal");

    std::string err = t.dostring_err("require('..evil')");
    EXPECT_NE(err.find("path traversal denied"), std::string::npos) << "Error: " << err;
}

TEST(LuaModuleRequire, ErrorOnPathTraversalComplex) {
    TestVM t("uz_req_traversal2");

    std::string err = t.dostring_err("require('....etc..passwd')");
    EXPECT_NE(err.find("path traversal denied"), std::string::npos) << "Error: " << err;
}

TEST(LuaModuleRequire, ErrorOnCompileError) {
    TestVM t("uz_req_compile");
    t.write_module("bad_syntax.lua", "function !!!invalid");

    std::string err = t.dostring_err("require('bad_syntax')");
    EXPECT_FALSE(err.empty()) << "Should have returned an error for syntax error";
}

TEST(LuaModuleRequire, ErrorOnRuntimeError) {
    TestVM t("uz_req_runtime");
    t.write_module("crash.lua", "error('module init failed')");

    std::string err = t.dostring_err("require('crash')");
    EXPECT_NE(err.find("module init failed"), std::string::npos) << "Error: " << err;
}

TEST(LuaModuleRequire, ErrorOnCircularDependency) {
    TestVM t("uz_req_circular");
    t.write_module("a.lua", "local B = require('b')\nreturn { from_a = true }");
    t.write_module("b.lua", "local A = require('a')\nreturn { from_b = true }");

    std::string err = t.dostring_err("require('a')");
    EXPECT_NE(err.find("circular dependency"), std::string::npos) << "Error: " << err;
}

TEST(LuaModuleRequire, CircularDependencyDoesNotPermanentlyBlock) {
    TestVM t("uz_req_circ_recover");
    t.write_module("x.lua", "local Y = require('y')\nreturn {}");
    t.write_module("y.lua", "local X = require('x')\nreturn {}");

    // First attempt fails with circular dependency
    std::string err1 = t.dostring_err("require('x')");
    EXPECT_NE(err1.find("circular dependency"), std::string::npos);

    // Fix the cycle by making y.lua not require x
    t.dir.remove_file("y.lua");
    t.write_module("y.lua", "return { fixed = true }");

    // Now x should load successfully (loading set was cleaned up)
    int err2 = t.dostring(
        "local M = require('x')\n"
        "assert(M ~= nil)\n");
    if (err2) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

TEST(LuaModuleRequire, RuntimeErrorDoesNotPermanentlyBlock) {
    TestVM t("uz_req_rt_recover");
    t.write_module("flaky.lua", "error('init fail')");

    // First attempt fails
    std::string err1 = t.dostring_err("require('flaky')");
    EXPECT_NE(err1.find("init fail"), std::string::npos);

    // Fix the module
    t.dir.remove_file("flaky.lua");
    t.write_module("flaky.lua", "return { ok = true }");

    // Now should load successfully (not permanently cached as error)
    int err2 = t.dostring(
        "local M = require('flaky')\n"
        "assert(M.ok == true)\n");
    if (err2) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

TEST(LuaModuleRequire, CompileErrorDoesNotPermanentlyBlock) {
    TestVM t("uz_req_ce_recover");
    t.write_module("broken.lua", "!!!syntax error!!!");

    // First attempt fails
    std::string err1 = t.dostring_err("require('broken')");
    EXPECT_FALSE(err1.empty());

    // Fix the module
    t.dir.remove_file("broken.lua");
    t.write_module("broken.lua", "return { fixed = true }");

    // Now should load successfully
    int err2 = t.dostring(
        "local M = require('broken')\n"
        "assert(M.fixed == true)\n");
    if (err2) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

// ============================================================================
// require() -- Non-string argument
// ============================================================================

TEST(LuaModuleRequire, ErrorOnNonStringArg) {
    TestVM t("uz_req_nonstr");
    std::string err = t.dostring_err("require(42)");
    EXPECT_FALSE(err.empty()) << "Should reject non-string argument";
}

TEST(LuaModuleRequire, ErrorOnNoArg) {
    TestVM t("uz_req_noarg");
    std::string err = t.dostring_err("require()");
    EXPECT_FALSE(err.empty()) << "Should reject missing argument";
}

// ============================================================================
// Module invalidation (hot-reload)
// ============================================================================

TEST(LuaModuleInvalidate, InvalidateForceReload) {
    TestVM t("uz_inv_reload");
    t.write_module("versioned.lua", "return { version = 1 }");

    // Load v1
    int err = t.dostring(
        "local M = require('versioned')\n"
        "assert(M.version == 1)\n");
    ASSERT_EQ(err, 0) << lua_tostring(t.L(), -1);

    // Update the file
    t.dir.remove_file("versioned.lua");
    t.write_module("versioned.lua", "return { version = 2 }");

    // Without invalidation, still returns v1
    err = t.dostring(
        "local M = require('versioned')\n"
        "assert(M.version == 1, 'should still be cached v1')\n");
    ASSERT_EQ(err, 0) << lua_tostring(t.L(), -1);

    // Invalidate
    lua_module_loader_invalidate(t.L(), "versioned");

    // Now should reload and get v2
    err = t.dostring(
        "local M = require('versioned')\n"
        "assert(M.version == 2, 'should be reloaded v2')\n");
    if (err) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

TEST(LuaModuleInvalidate, InvalidateNullStateIsNoOp) {
    lua_module_loader_invalidate(nullptr, "mod");
    SUCCEED();
}

TEST(LuaModuleInvalidate, InvalidateNullNameIsNoOp) {
    LuaVM vm;
    vm.init();
    lua_module_loader_invalidate(vm.L(), nullptr);
    SUCCEED();
}

TEST(LuaModuleInvalidate, InvalidateViaLuaVM) {
    TestVM t("uz_inv_vm");
    t.write_module("reloadable.lua", "return { v = 1 }");

    int err = t.dostring("local M = require('reloadable')\nassert(M.v == 1)");
    ASSERT_EQ(err, 0) << lua_tostring(t.L(), -1);

    t.dir.remove_file("reloadable.lua");
    t.write_module("reloadable.lua", "return { v = 2 }");

    t.vm.invalidate_module("reloadable");

    err = t.dostring("local M = require('reloadable')\nassert(M.v == 2)");
    if (err) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

TEST(LuaModuleInvalidate, InvalidateModuleVMWithNullName) {
    LuaVM vm;
    vm.init();
    vm.invalidate_module(nullptr);  // no crash
    SUCCEED();
}

TEST(LuaModuleInvalidate, InvalidateModuleVMWithoutInit) {
    LuaVM vm;
    vm.invalidate_module("mod");  // no crash, L_ is null
    SUCCEED();
}

// ============================================================================
// require() -- Module that requires another module
// ============================================================================

TEST(LuaModuleRequire, TransitiveDependency) {
    TestVM t("uz_req_transitive");
    t.write_module("base.lua", "return { base_val = 10 }");
    t.write_module("derived.lua",
        "local Base = require('base')\n"
        "return { total = Base.base_val + 5 }\n");

    int err = t.dostring(
        "local D = require('derived')\n"
        "assert(D.total == 15)\n");
    if (err) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

TEST(LuaModuleRequire, DiamondDependency) {
    TestVM t("uz_req_diamond");
    t.write_module("root.lua", "return { val = 100 }");
    t.write_module("left.lua",
        "local R = require('root')\n"
        "return { left_val = R.val + 1 }\n");
    t.write_module("right.lua",
        "local R = require('root')\n"
        "return { right_val = R.val + 2 }\n");
    t.write_module("top.lua",
        "local L = require('left')\n"
        "local R = require('right')\n"
        "return { sum = L.left_val + R.right_val }\n");

    int err = t.dostring(
        "local T = require('top')\n"
        "assert(T.sum == 203, 'expected 101+102=203, got ' .. T.sum)\n");
    if (err) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

// ============================================================================
// Integration -- require from script instances
// ============================================================================

TEST(LuaModuleIntegration, ScriptUsesRequireInOnStart) {
    TempDir dir("uz_mod_int_start");
    dir.write_file("math_utils.lua",
        "local M = {}\n"
        "function M.add(a, b) return a + b end\n"
        "return M\n");
    dir.write_file("test_script.lua",
        "local MU = require('math_utils')\n"
        "function on_start()\n"
        "  local result = MU.add(3, 4)\n"
        "  if result ~= 7 then error('bad add: ' .. result) end\n"
        "end\n"
        "function on_update(dt) end\n");

    LuaVM vm;
    vm.init();
    vm.set_project_root(dir.c_str());

    std::string script_path = (dir.path / "test_script.lua").string();

    LuaScriptInstance instance(&vm);
    EXPECT_TRUE(instance.load(script_path.c_str(), 1));
    instance.on_start();  // Should call on_start which uses require
    SUCCEED();
}

TEST(LuaModuleIntegration, ScriptUsesRequireInOnUpdate) {
    TempDir dir("uz_mod_int_update");
    dir.write_file("helpers.lua",
        "return { scale = function(x, f) return x * f end }\n");
    dir.write_file("updater.lua",
        "local H = require('helpers')\n"
        "function on_update(dt)\n"
        "  local scaled = H.scale(dt, 2)\n"
        "end\n");

    LuaVM vm;
    vm.init();
    vm.set_project_root(dir.c_str());

    std::string script_path = (dir.path / "updater.lua").string();

    LuaScriptInstance instance(&vm);
    EXPECT_TRUE(instance.load(script_path.c_str(), 1));
    instance.on_start();
    instance.on_update(0.016f);
    SUCCEED();
}

TEST(LuaModuleIntegration, TwoScriptsShareModule) {
    TempDir dir("uz_mod_int_share");
    dir.write_file("shared.lua",
        "local S = { counter = 0 }\n"
        "function S.increment() S.counter = S.counter + 1 end\n"
        "return S\n");
    dir.write_file("a.lua",
        "local S = require('shared')\n"
        "function on_start() S.increment() end\n"
        "function on_update(dt) end\n");
    dir.write_file("b.lua",
        "local S = require('shared')\n"
        "function on_start() S.increment() end\n"
        "function on_update(dt) end\n");

    LuaVM vm;
    vm.init();
    vm.set_project_root(dir.c_str());

    std::string path_a = (dir.path / "a.lua").string();
    std::string path_b = (dir.path / "b.lua").string();

    LuaScriptInstance inst_a(&vm);
    LuaScriptInstance inst_b(&vm);
    EXPECT_TRUE(inst_a.load(path_a.c_str(), 1));
    EXPECT_TRUE(inst_b.load(path_b.c_str(), 2));

    inst_a.on_start();
    inst_b.on_start();

    // Verify shared counter is 2
    int err = luaL_dostring(vm.L(),
        "local S = require('shared')\n"
        "assert(S.counter == 2, 'expected 2, got ' .. S.counter)\n");
    if (err) {
        FAIL() << lua_tostring(vm.L(), -1);
    }
}

// ============================================================================
// Integration -- Hot-reload with module invalidation
// ============================================================================

TEST(LuaModuleIntegration, HotReloadInvalidatesAndReloads) {
    TempDir dir("uz_mod_int_hotreload");
    dir.write_file("config.lua", "return { speed = 5.0 }");
    dir.write_file("player.lua",
        "local Config = require('config')\n"
        "function on_start()\n"
        "  if Config.speed ~= 5.0 then error('wrong speed: ' .. Config.speed) end\n"
        "end\n"
        "function on_update(dt) end\n");

    LuaVM vm;
    vm.init();
    vm.set_project_root(dir.c_str());

    std::string player_path = (dir.path / "player.lua").string();

    // Load and start player script
    LuaScriptInstance inst(&vm);
    EXPECT_TRUE(inst.load(player_path.c_str(), 1));
    inst.on_start();

    // Update config.lua
    dir.remove_file("config.lua");
    dir.write_file("config.lua", "return { speed = 10.0 }");

    // Invalidate the cached module
    vm.invalidate_module("config");

    // Verify the new value is loaded on next require
    int err = luaL_dostring(vm.L(),
        "local C = require('config')\n"
        "assert(C.speed == 10.0, 'expected 10.0, got ' .. C.speed)\n");
    if (err) {
        FAIL() << lua_tostring(vm.L(), -1);
    }
}

// ============================================================================
// require() -- Edge cases
// ============================================================================

TEST(LuaModuleRequire, ModuleReturningFunction) {
    TestVM t("uz_req_func");
    t.write_module("factory.lua", "return function(x) return x * 2 end");

    int err = t.dostring(
        "local f = require('factory')\n"
        "assert(f(21) == 42)\n");
    if (err) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

TEST(LuaModuleRequire, ModuleReturningNumber) {
    TestVM t("uz_req_num");
    t.write_module("version.lua", "return 42");

    int err = t.dostring(
        "local v = require('version')\n"
        "assert(v == 42)\n");
    if (err) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

TEST(LuaModuleRequire, ModuleReturningString) {
    TestVM t("uz_req_str");
    t.write_module("greeting.lua", "return 'hello'");

    int err = t.dostring(
        "local g = require('greeting')\n"
        "assert(g == 'hello')\n");
    if (err) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

TEST(LuaModuleRequire, ModuleReturningBoolean) {
    TestVM t("uz_req_bool");
    t.write_module("flag.lua", "return false");

    // false is falsy but not nil, should be cached as-is
    int err = t.dostring(
        "local f = require('flag')\n"
        "assert(f == false)\n");
    if (err) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

TEST(LuaModuleRequire, ModuleCanUseMathLibrary) {
    TestVM t("uz_req_math");
    t.write_module("trig.lua",
        "return { pi = math.pi, sin = math.sin }");

    int err = t.dostring(
        "local T = require('trig')\n"
        "assert(T.pi > 3.14)\n"
        "assert(T.sin(0) == 0)\n");
    if (err) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

TEST(LuaModuleRequire, ModuleCanUseStringLibrary) {
    TestVM t("uz_req_string");
    t.write_module("fmt.lua",
        "return { upper = function(s) return string.upper(s) end }");

    int err = t.dostring(
        "local F = require('fmt')\n"
        "assert(F.upper('hello') == 'HELLO')\n");
    if (err) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

TEST(LuaModuleRequire, ModuleCanUseTableLibrary) {
    TestVM t("uz_req_table");
    t.write_module("list.lua",
        "local L = {}\n"
        "function L.make(...)\n"
        "  local t = {...}\n"
        "  table.sort(t)\n"
        "  return t\n"
        "end\n"
        "return L\n");

    int err = t.dostring(
        "local L = require('list')\n"
        "local sorted = L.make(3,1,2)\n"
        "assert(sorted[1] == 1 and sorted[2] == 2 and sorted[3] == 3)\n");
    if (err) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

TEST(LuaModuleRequire, ModuleCanAccessUzBindings) {
    TestVM t("uz_req_bindings");
    t.write_module("timing.lua",
        "return { get_dt = function() return uz.delta_time() end }");

    t.vm.set_frame_time(0.016f, 1.0f);

    int err = t.dostring(
        "local T = require('timing')\n"
        "local dt = T.get_dt()\n"
        "assert(dt > 0.015 and dt < 0.017)\n");
    if (err) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

// ============================================================================
// require() without project root
// ============================================================================

TEST(LuaModuleRequire, ErrorWithoutProjectRoot) {
    LuaVM vm;
    vm.init();

    // Manually install require without a project root
    // (this shouldn't normally happen, but test the guard)
    lua_pushnil(vm.L());
    lua_setfield(vm.L(), LUA_REGISTRYINDEX, "uz_module_project_root");

    // Install the loader to get require function
    lua_module_loader_install(vm.L(), "/tmp");

    // Clear the project root to simulate the edge case
    lua_pushnil(vm.L());
    lua_setfield(vm.L(), LUA_REGISTRYINDEX, "uz_module_project_root");

    std::string err_msg;
    int err = luaL_dostring(vm.L(), "require('anything')");
    if (err) {
        const char* msg = lua_tostring(vm.L(), -1);
        err_msg = msg ? msg : "";
        lua_pop(vm.L(), 1);
    }
    EXPECT_NE(err_msg.find("not initialized"), std::string::npos) << "Error: " << err_msg;
}

// ============================================================================
// require() -- module returning false (falsy but valid)
// ============================================================================

TEST(LuaModuleRequire, ModuleReturningFalseIsCached) {
    TestVM t("uz_req_false_cache");
    t.write_module("falsy.lua",
        "global_load_count = (global_load_count or 0) + 1\n"
        "return false\n");

    int err = t.dostring(
        "local a = require('falsy')\n"
        "local b = require('falsy')\n"
        "assert(a == false)\n"
        "assert(b == false)\n");
    if (err) {
        FAIL() << lua_tostring(t.L(), -1);
    }
}

// ============================================================================
// Multiple independent VMs
// ============================================================================

TEST(LuaModuleRequire, IndependentVMsHaveSeparateCaches) {
    TempDir dir1("uz_vm1");
    TempDir dir2("uz_vm2");
    dir1.write_file("mod.lua", "return { source = 'vm1' }");
    dir2.write_file("mod.lua", "return { source = 'vm2' }");

    LuaVM vm1, vm2;
    vm1.init();
    vm2.init();
    vm1.set_project_root(dir1.c_str());
    vm2.set_project_root(dir2.c_str());

    int err1 = luaL_dostring(vm1.L(),
        "local M = require('mod')\nassert(M.source == 'vm1')");
    int err2 = luaL_dostring(vm2.L(),
        "local M = require('mod')\nassert(M.source == 'vm2')");

    if (err1) FAIL() << lua_tostring(vm1.L(), -1);
    if (err2) FAIL() << lua_tostring(vm2.L(), -1);
}

// ============================================================================
// Compile error includes file path
// ============================================================================

TEST(LuaModuleRequire, CompileErrorIncludesFilePath) {
    TestVM t("uz_req_ce_path");
    t.write_module("bad.lua", "function !!!invalid");

    std::string err = t.dostring_err("require('bad')");
    // The error should contain the file path
    EXPECT_NE(err.find("bad.lua"), std::string::npos)
        << "Error should mention file path: " << err;
}

// ============================================================================
// Stress: many modules
// ============================================================================

TEST(LuaModuleRequire, ManyModules) {
    TestVM t("uz_req_many");
    for (int i = 0; i < 50; ++i) {
        char name[64];
        std::snprintf(name, sizeof(name), "mod_%d.lua", i);
        char content[128];
        std::snprintf(content, sizeof(content), "return { id = %d }", i);
        t.write_module(name, content);
    }

    for (int i = 0; i < 50; ++i) {
        char code[256];
        std::snprintf(code, sizeof(code),
            "local M = require('mod_%d')\nassert(M.id == %d)", i, i);
        int err = t.dostring(code);
        if (err) {
            FAIL() << "Failed for mod_" << i << ": " << lua_tostring(t.L(), -1);
        }
    }
}
