/// Security tests for the Lua sandbox — every known escape from a gameplay
/// script (native code via package/ffi, registry access via debug,
/// environment escape via getfenv/setfenv, bytecode loading, shared-state
/// tampering) must be blocked, while the documented script API keeps working.
///
/// Escape checks run code exactly the way ScriptSystem does: compiled with
/// LuaSandboxLoadTextChunk, executed in a fresh LuaSandboxCreateScriptEnv
/// environment. End-to-end tests drive real CScript components through
/// ScriptSystem with an in-memory VFS.

#include "lua_sandbox.h"
#include "lua_vm.h"
#include "script_system.h"
#include "script_component.h"
#include "world.h"
#include "../asset/vfs.h"

#include <gtest/gtest.h>

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// SandboxTestVFS — in-memory script store
// ─────────────────────────────────────────────────────────────────────────────

class SandboxTestVFS final : public uz::IVFS {
public:
    void Add(const std::string& path, const std::string& bytes) { files_[path] = bytes; }

    [[nodiscard]] std::vector<uint8_t> Read(const std::string& path) override {
        auto it = files_.find(path);
        if (it == files_.end()) return {};
        return {it->second.begin(), it->second.end()};
    }

    [[nodiscard]] bool Exists(const std::string& path) override { return files_.count(path) > 0; }

    [[nodiscard]] std::string Name() const override { return "SandboxTestVFS"; }

private:
    std::unordered_map<std::string, std::string> files_;
};

/// Produces LuaJIT bytecode for `source` in a separate, fully unrestricted
/// state (host-side string.dump). The engine VM itself has no string.dump.
std::string DumpBytecode(const char* source) {
    lua_State* host = luaL_newstate();
    luaL_openlibs(host);
    std::string bytecode;
    const std::string code = std::string("return string.dump(loadstring([[") + source + "]]))";
    if (luaL_dostring(host, code.c_str()) == 0 && lua_type(host, -1) == LUA_TSTRING) {
        size_t length = 0;
        const char* data = lua_tolstring(host, -1, &length);
        bytecode.assign(data, length);
    }
    lua_close(host);
    return bytecode;
}

// ─────────────────────────────────────────────────────────────────────────────
// Fixture
// ─────────────────────────────────────────────────────────────────────────────

class LuaSandboxTest : public ::testing::Test {
protected:
    uz::World      world_;
    SandboxTestVFS vfs_;

    void SetUp() override {
        ASSERT_TRUE(uz::LuaVM::Init());
        uz::ScriptSystem::Initialize(&world_, &vfs_);
        uz::ScriptSystem::SetPlayMode(true);
    }

    void TearDown() override {
        uz::ScriptSystem::Shutdown();
        uz::LuaVM::Shutdown();
        uz::ScriptSystem::SetPlayMode(false);
    }

    /// Compiles `code` as a script chunk in a fresh sandbox environment and
    /// runs it, leaving one result on the stack on success.
    ///
    /// Returns an empty string on success, otherwise the Lua error message
    /// (compile or runtime); nothing is left on the stack on failure.
    std::string RunScript(const std::string& code) {
        lua_State* L = uz::LuaVM::GetState();
        if (uz::LuaSandboxLoadTextChunk(L, code.data(), code.size(), "=sandbox_test") != 0) {
            std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown";
            lua_pop(L, 1);
            return err.empty() ? "compile error" : err;
        }
        const int envRef = uz::LuaSandboxCreateScriptEnv(L);
        if (envRef == LUA_REFNIL) {
            lua_pop(L, 1);
            return "no sandbox environment";
        }
        lua_rawgeti(L, LUA_REGISTRYINDEX, envRef);
        lua_setfenv(L, -2);
        luaL_unref(L, LUA_REGISTRYINDEX, envRef);
        if (lua_pcall(L, 0, 1, 0) != 0) {
            std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown";
            lua_pop(L, 1);
            return err.empty() ? "runtime error" : err;
        }
        return {};
    }

    /// Evaluates `expr` inside a sandboxed script and returns it as a boolean.
    bool ScriptBool(const std::string& expr) {
        const std::string err = RunScript("return " + expr);
        if (!err.empty()) {
            ADD_FAILURE() << "script error evaluating '" << expr << "': " << err;
            return false;
        }
        lua_State* L = uz::LuaVM::GetState();
        const bool result = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        return result;
    }

    /// Runs `code` inside a sandboxed script and returns the error it raised
    /// (empty string if it did not fail).
    std::string ScriptError(const std::string& code) {
        const std::string err = RunScript(code);
        if (err.empty()) lua_pop(uz::LuaVM::GetState(), 1);
        return err;
    }

    /// Evaluates `expr` in the VM's global (host) environment.
    bool HostBool(const std::string& expr) {
        lua_State* L = uz::LuaVM::GetState();
        const std::string code = "return " + expr;
        if (luaL_dostring(L, code.c_str()) != 0) {
            ADD_FAILURE() << "host error evaluating '" << expr << "': " << lua_tostring(L, -1);
            lua_pop(L, 1);
            return false;
        }
        const bool result = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        return result;
    }

    uz::Entity AddScriptEntity(const std::string& path, const std::string& source) {
        vfs_.Add(path, source);
        auto e = world_.CreateEntity();
        world_.AddComponent<uz::CScript>(e, uz::CScript{path});
        return e;
    }

    /// Pushes the global `name` from the private environment of `e`'s script.
    /// Pushes nil when the script has no environment.
    void PushScriptGlobal(uz::Entity e, const char* name) {
        lua_State* L = uz::LuaVM::GetState();
        const auto& script = world_.GetComponent<uz::CScript>(e);
        if (script.envRef == LUA_REFNIL) {
            lua_pushnil(L);
            return;
        }
        lua_rawgeti(L, LUA_REGISTRYINDEX, script.envRef);
        lua_getfield(L, -1, name);
        lua_remove(L, -2);
    }

    std::string ScriptGlobalString(uz::Entity e, const char* name) {
        PushScriptGlobal(e, name);
        lua_State* L = uz::LuaVM::GetState();
        std::string value = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "<not a string>";
        lua_pop(L, 1);
        return value;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Escape routes are absent from a script environment
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSandboxTest, Script_PackageIsUnreachable) {
    EXPECT_TRUE(ScriptBool("package == nil"));
    EXPECT_TRUE(ScriptBool("require == nil"));
    EXPECT_TRUE(ScriptBool("module == nil"));
    EXPECT_FALSE(ScriptError("return package.loadlib('kernel32.dll', 'WinExec')").empty());
}

TEST_F(LuaSandboxTest, Script_FfiIsUnreachableThroughPreload) {
    EXPECT_TRUE(ScriptBool("ffi == nil"));
    EXPECT_FALSE(ScriptError("return package.preload.ffi").empty());
    EXPECT_FALSE(ScriptError("return require('ffi').C.system('calc')").empty());

    // No module loaders (ffi, jit.util, jit.profile) are pre-registered at all.
    lua_State* L = uz::LuaVM::GetState();
    lua_getfield(L, LUA_REGISTRYINDEX, "_PRELOAD");
    EXPECT_TRUE(lua_isnil(L, -1)) << "the preload table must not exist";
    lua_pop(L, 1);

    // ffi was never opened, so it is not among the loaded modules either.
    lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
    ASSERT_TRUE(lua_istable(L, -1));
    lua_getfield(L, -1, LUA_FFILIBNAME);
    EXPECT_TRUE(lua_isnil(L, -1)) << "ffi must never be loaded";
    lua_pop(L, 2);
}

TEST_F(LuaSandboxTest, Script_DebugIsUnreachable) {
    EXPECT_TRUE(ScriptBool("debug == nil"));
    EXPECT_FALSE(ScriptError("return debug.getregistry()").empty());
}

TEST_F(LuaSandboxTest, Script_JitIsUnreachable) {
    EXPECT_TRUE(ScriptBool("jit == nil"));
    EXPECT_FALSE(ScriptError("return jit.util.funcinfo(print)").empty());
}

TEST_F(LuaSandboxTest, Script_IoAndOsAreUnreachable) {
    EXPECT_TRUE(ScriptBool("io == nil"));
    EXPECT_TRUE(ScriptBool("os == nil"));
    EXPECT_FALSE(ScriptError("os.execute('calc')").empty());
    EXPECT_FALSE(ScriptError("io.open('C:/Windows/win.ini')").empty());
}

TEST_F(LuaSandboxTest, Script_CodeLoadingFunctionsAreAbsent) {
    EXPECT_TRUE(ScriptBool("loadstring == nil"));
    EXPECT_TRUE(ScriptBool("load == nil"));
    EXPECT_TRUE(ScriptBool("loadfile == nil"));
    EXPECT_TRUE(ScriptBool("dofile == nil"));
    EXPECT_TRUE(ScriptBool("string.dump == nil"));
    EXPECT_TRUE(ScriptBool("('').dump == nil")) << "string.dump via the string metatable";
}

TEST_F(LuaSandboxTest, Script_EnvironmentFunctionsAreAbsent) {
    EXPECT_TRUE(ScriptBool("getfenv == nil"));
    EXPECT_TRUE(ScriptBool("setfenv == nil"));
    EXPECT_TRUE(ScriptBool("rawget == nil"));
    EXPECT_TRUE(ScriptBool("rawset == nil"));
    EXPECT_TRUE(ScriptBool("rawequal == nil"));
    EXPECT_TRUE(ScriptBool("collectgarbage == nil"));
    EXPECT_TRUE(ScriptBool("newproxy == nil"));
    EXPECT_TRUE(ScriptBool("gcinfo == nil"));
}

// ─────────────────────────────────────────────────────────────────────────────
// The VM's own global table is stripped too (host-evaluated code)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSandboxTest, Host_GlobalTableIsStripped) {
    for (const char* name : {"package", "debug", "jit", "ffi", "io", "os", "require", "module",
                             "load", "loadstring", "loadfile", "dofile", "getfenv", "setfenv",
                             "collectgarbage", "newproxy", "rawget", "rawset", "rawequal"}) {
        EXPECT_TRUE(HostBool(std::string(name) + " == nil")) << name << " must be removed";
    }
    EXPECT_TRUE(HostBool("string.dump == nil"));
}

TEST_F(LuaSandboxTest, Host_JitCompilerStaysEnabled) {
    // The jit library is opened only to initialize the trace compiler; its
    // module table remains reachable from C through the registry.
    lua_State* L = uz::LuaVM::GetState();
    lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
    ASSERT_TRUE(lua_istable(L, -1));
    lua_getfield(L, -1, LUA_JITLIBNAME);
    ASSERT_TRUE(lua_istable(L, -1));
    lua_getfield(L, -1, "status");
    ASSERT_TRUE(lua_isfunction(L, -1));
    ASSERT_EQ(lua_pcall(L, 0, 1, 0), 0);
    EXPECT_TRUE(lua_toboolean(L, -1) != 0) << "LuaJIT trace compiler must be on";
    lua_pop(L, 3);
}

// ─────────────────────────────────────────────────────────────────────────────
// Precompiled bytecode is rejected on every path
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSandboxTest, Bytecode_RejectedByLoadTextChunkWithClearError) {
    const std::string bytecode = DumpBytecode("return 1");
    ASSERT_GE(bytecode.size(), 4u);
    ASSERT_EQ(static_cast<unsigned char>(bytecode[0]), 0x1Bu);

    lua_State* L = uz::LuaVM::GetState();
    const int top = lua_gettop(L);
    EXPECT_NE(uz::LuaSandboxLoadTextChunk(L, bytecode.data(), bytecode.size(), "evil.lua"), 0);
    ASSERT_EQ(lua_gettop(L), top + 1);
    const std::string message = lua_tostring(L, -1);
    EXPECT_NE(message.find("bytecode"), std::string::npos) << message;
    lua_pop(L, 1);
}

TEST_F(LuaSandboxTest, Bytecode_RejectedByLuaVMLoadScript) {
    const std::string bytecode = DumpBytecode("return 1");
    ASSERT_FALSE(bytecode.empty());
    const std::vector<uint8_t> bytes(bytecode.begin(), bytecode.end());
    EXPECT_EQ(uz::LuaVM::LoadScript(bytes, "evil.lua"), LUA_REFNIL);
}

TEST_F(LuaSandboxTest, Bytecode_BehindShebangOrBomIsStillRejected) {
    const std::string bytecode = DumpBytecode("return 1");
    ASSERT_FALSE(bytecode.empty());
    lua_State* L = uz::LuaVM::GetState();
    for (const std::string& prefix : {std::string("#!/usr/bin/luajit\n"),
                                      std::string("\xEF\xBB\xBF")}) {
        const std::string chunk = prefix + bytecode;
        EXPECT_NE(uz::LuaSandboxLoadTextChunk(L, chunk.data(), chunk.size(), "evil.lua"), 0);
        lua_pop(L, 1);
    }
}

TEST_F(LuaSandboxTest, Bytecode_ScriptComponentIsNeverExecuted) {
    const std::string bytecode = DumpBytecode("function on_start(e) ran = true end");
    ASSERT_FALSE(bytecode.empty());
    const auto e = AddScriptEntity("scripts/evil.lua", bytecode);

    uz::ScriptSystem::Update(0.016f);
    uz::ScriptSystem::Update(0.016f);

    const auto& script = world_.GetComponent<uz::CScript>(e);
    EXPECT_EQ(script.luaRef, LUA_REFNIL);
    EXPECT_EQ(script.envRef, LUA_REFNIL);
    EXPECT_TRUE(script.started) << "a rejected script must not be retried every frame";
    EXPECT_TRUE(HostBool("ran == nil"));
}

TEST_F(LuaSandboxTest, SourceText_StillLoads) {
    lua_State* L = uz::LuaVM::GetState();
    const std::string source = "\xEF\xBB\xBF" "return 6 * 7";
    ASSERT_EQ(uz::LuaSandboxLoadTextChunk(L, source.data(), source.size(), "ok.lua"), 0);
    ASSERT_EQ(lua_pcall(L, 0, 1, 0), 0);
    EXPECT_EQ(lua_tonumber(L, -1), 42.0);
    lua_pop(L, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared tables are read-only from scripts
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSandboxTest, ReadOnly_EngineApiCannotBeModified) {
    for (const char* code : {"uz.log = nil", "uz.injected = {}", "uz.scene.injected = 1",
                             "uz.math.vec3 = print"}) {
        const std::string err = ScriptError(code);
        EXPECT_NE(err.find("read-only"), std::string::npos) << code << " -> " << err;
    }
    EXPECT_TRUE(HostBool("uz.injected == nil and uz.scene.injected == nil"));
}

TEST_F(LuaSandboxTest, ReadOnly_StandardLibrariesCannotBeModified) {
    for (const char* code : {"math.sin = nil", "string.format = nil", "table.insert = nil",
                             "coroutine.wrap = nil", "bit.band = nil"}) {
        const std::string err = ScriptError(code);
        EXPECT_NE(err.find("read-only"), std::string::npos) << code << " -> " << err;
    }
    EXPECT_TRUE(HostBool("type(math.sin) == 'function' and type(string.format) == 'function'"));
}

TEST_F(LuaSandboxTest, ReadOnly_SharedMetatablesAreHidden) {
    EXPECT_TRUE(ScriptBool("getmetatable('') == false"));
    EXPECT_TRUE(ScriptBool("getmetatable(uz) == false"));
    EXPECT_TRUE(ScriptBool("getmetatable(uz.scene) == false"));
    EXPECT_TRUE(ScriptBool("getmetatable(math) == false"));
    EXPECT_TRUE(ScriptBool("getmetatable(_G) == false"));
    EXPECT_TRUE(ScriptBool("getmetatable(uz.math.vec3(1, 2, 3)) == false"));
    EXPECT_TRUE(ScriptBool("getmetatable(uz.math.quat()) == false"));
    EXPECT_FALSE(ScriptError("setmetatable(_G, nil)").empty());
    EXPECT_FALSE(ScriptError("setmetatable(uz, {})").empty());
    EXPECT_FALSE(ScriptError("setmetatable(math, {})").empty());
}

TEST_F(LuaSandboxTest, ReadOnly_ProxiesReadLiveEngineValues) {
    uz::LuaVM::UpdateFrameGlobals(0.5f, 2.0f, 7);
    EXPECT_TRUE(ScriptBool("uz.delta_time == 0.5 and uz.total_time == 2 and uz.frame_index == 7"));
    EXPECT_TRUE(ScriptBool("uz.scene == uz.scene")) << "proxy identity must be stable";
    EXPECT_TRUE(ScriptBool("type(uz.scene) == 'table' and type(uz.log) == 'function'"));

    // Sub-APIs registered after LuaVM::Init (VFX, decals, ...) must be visible.
    lua_State* L = uz::LuaVM::GetState();
    ASSERT_EQ(luaL_dostring(L, "uz.late_api = { answer = 42, nested = { flag = true } }"), 0);
    EXPECT_TRUE(ScriptBool("uz.late_api.answer == 42 and uz.late_api.nested.flag == true"));
    EXPECT_FALSE(ScriptError("uz.late_api.nested.flag = false").empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// The documented script API keeps working
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSandboxTest, Script_SafeLibrariesWork) {
    EXPECT_TRUE(ScriptBool("math.floor(math.sqrt(16)) == 4 and math.pi > 3.14"));
    EXPECT_TRUE(ScriptBool("string.format('%d-%s', 5, 'x') == '5-x'"));
    EXPECT_TRUE(ScriptBool("('abc'):upper() == 'ABC'"));
    EXPECT_TRUE(ScriptBool("table.concat({1, 2, 3}, ',') == '1,2,3'"));
    EXPECT_TRUE(ScriptBool("bit.band(6, 3) == 2"));
    EXPECT_TRUE(ScriptBool("select('#', 1, 2, 3) == 3 and unpack({4, 5}) == 4"));
    EXPECT_TRUE(ScriptBool("coroutine.wrap(function() coroutine.yield(9) end)() == 9"));
    EXPECT_TRUE(ScriptBool("pcall(error, 'boom') == false"));
    EXPECT_TRUE(ScriptBool("type(_VERSION) == 'string'"));
    EXPECT_TRUE(ScriptBool("_G._G == _G")) << "_G refers to the script's own environment";
    EXPECT_TRUE(ScriptBool("setmetatable({}, { __index = { k = 1 } }).k == 1"));
    EXPECT_TRUE(ScriptBool("tostring(uz.math.vec3(1, 2, 3)) ~= nil"));
}

TEST_F(LuaSandboxTest, Print_IsRedirectedAndValidatesTostring) {
    EXPECT_TRUE(ScriptBool("type(print) == 'function'"));
    EXPECT_EQ(ScriptError("print('a', 1, nil, true, {}, uz.math.vec3(1, 2, 3))"), "");
    EXPECT_EQ(ScriptError("print()"), "");
    const std::string err =
        ScriptError("print(setmetatable({}, { __tostring = function() return 1 end }))");
    EXPECT_NE(err.find("'tostring' must return a string"), std::string::npos) << err;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-script isolation (end-to-end through ScriptSystem)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LuaSandboxTest, Isolation_GlobalsWrittenByOneScriptAreInvisibleToAnother) {
    const auto writer = AddScriptEntity("scripts/writer.lua",
        "shared_secret = 'from_writer'\n"
        "_G.via_g = 'from_writer'\n"
        "print = 'overridden'\n"
        "tostring = 'overridden'\n");
    const auto reader = AddScriptEntity("scripts/reader.lua",
        "function on_update(e, dt)\n"
        "    saw_secret    = type(shared_secret)\n"
        "    saw_via_g     = type(_G.via_g)\n"
        "    print_type    = type(print)\n"
        "    tostring_type = type(tostring)\n"
        "end\n");

    uz::ScriptSystem::Update(0.016f);   // load + run both chunks
    uz::ScriptSystem::Update(0.016f);   // reader's on_update

    EXPECT_EQ(ScriptGlobalString(reader, "saw_secret"), "nil");
    EXPECT_EQ(ScriptGlobalString(reader, "saw_via_g"), "nil");
    EXPECT_EQ(ScriptGlobalString(reader, "print_type"), "function");
    EXPECT_EQ(ScriptGlobalString(reader, "tostring_type"), "function");

    EXPECT_EQ(ScriptGlobalString(writer, "shared_secret"), "from_writer");
    EXPECT_EQ(ScriptGlobalString(writer, "via_g"), "from_writer");
    EXPECT_EQ(ScriptGlobalString(writer, "print"), "overridden")
        << "the writer only replaced its own print";

    EXPECT_TRUE(HostBool("shared_secret == nil and via_g == nil and type(print) == 'function'"));
}

TEST_F(LuaSandboxTest, Isolation_ScriptCannotSeeHostOnlyGlobals) {
    lua_State* L = uz::LuaVM::GetState();
    ASSERT_EQ(luaL_dostring(L, "host_only_value = 123"), 0);
    EXPECT_TRUE(ScriptBool("host_only_value == nil"))
        << "scripts read a whitelist, not the VM's global table";
}

TEST_F(LuaSandboxTest, Isolation_EachScriptGetsAFreshEnvironment) {
    const auto first  = AddScriptEntity("scripts/same.lua", "counter = (counter or 0) + 1\n");
    const auto second = world_.CreateEntity();
    world_.AddComponent<uz::CScript>(second, uz::CScript{"scripts/same.lua"});

    uz::ScriptSystem::Update(0.016f);

    const auto& a = world_.GetComponent<uz::CScript>(first);
    const auto& b = world_.GetComponent<uz::CScript>(second);
    ASSERT_NE(a.envRef, LUA_REFNIL);
    ASSERT_NE(b.envRef, LUA_REFNIL);
    EXPECT_NE(a.envRef, b.envRef);
    PushScriptGlobal(first, "counter");
    EXPECT_EQ(lua_tonumber(uz::LuaVM::GetState(), -1), 1.0);
    lua_pop(uz::LuaVM::GetState(), 1);
    PushScriptGlobal(second, "counter");
    EXPECT_EQ(lua_tonumber(uz::LuaVM::GetState(), -1), 1.0);
    lua_pop(uz::LuaVM::GetState(), 1);
}

} // namespace
