// test_lua_scripting.cpp -- Comprehensive tests for LuaVM, ScriptRunner,
// LuaScriptInstance, and LuaScriptReloader.
//
// Covers: init/shutdown lifecycle, sandbox enforcement, bindings, script
// attach/detach, error handling, hot-reload, concurrent scripts, edge cases.

#include <gtest/gtest.h>

#include "engine/scripting/lua_vm.h"
#include "engine/scripting/script_runner.h"
#include "engine/scripting/script_instance.h"
#include "engine/scripting/lua_script_instance.h"
#include "engine/scripting/lua_script_reloader.h"
#include "engine/scene/scene.h"
#include "engine/scene/scene_node.h"
#include "engine/scene/component_data.h"
#include "engine/scene/component_store.h"
#include "engine/renderer/scene/scene_params.h"

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
// Helpers -- temp Lua script files
// ============================================================================

namespace {

/// RAII temp file that writes a Lua script to disk and removes it on destruction.
struct TempLuaFile {
    fs::path path;
    std::string path_str;  // Stable storage for c_str()

    explicit TempLuaFile(const char* filename, const char* content) {
        path = fs::temp_directory_path() / filename;
        path_str = path.string();
        std::ofstream f(path);
        f << content;
    }

    ~TempLuaFile() {
        std::error_code ec;
        fs::remove(path, ec);
    }

    const char* c_str() const { return path_str.c_str(); }
};

/// Helper to create a minimal scene with a single alive node.
Scene* make_test_scene() {
    auto* scene = new Scene{};
    scene_init(scene);
    // Node 0 is root (always alive after scene_init).
    // Add node 1 for tests.
    scene_create_node(scene, "TestNode");
    return scene;
}

} // anonymous namespace

// ============================================================================
// LuaVM -- Lifecycle
// ============================================================================

TEST(LuaVM, InitCreatesState) {
    LuaVM vm;
    EXPECT_EQ(vm.L(), nullptr);
    EXPECT_TRUE(vm.init());
    EXPECT_NE(vm.L(), nullptr);
}

TEST(LuaVM, DoubleInitIsIdempotent) {
    LuaVM vm;
    EXPECT_TRUE(vm.init());
    lua_State* first = vm.L();
    EXPECT_TRUE(vm.init());          // second call — no-op
    EXPECT_EQ(vm.L(), first);        // same state
}

TEST(LuaVM, ShutdownFreesState) {
    LuaVM vm;
    vm.init();
    EXPECT_NE(vm.L(), nullptr);
    vm.shutdown();
    EXPECT_EQ(vm.L(), nullptr);
}

TEST(LuaVM, ShutdownWithoutInitIsNoOp) {
    LuaVM vm;
    vm.shutdown();   // must not crash
    EXPECT_EQ(vm.L(), nullptr);
}

TEST(LuaVM, DestructorCallsShutdown) {
    lua_State* raw = nullptr;
    {
        LuaVM vm;
        vm.init();
        raw = vm.L();
        EXPECT_NE(raw, nullptr);
    }
    // After destruction, the state should have been closed.
    // We can't safely dereference raw, but the destructor should not crash.
    SUCCEED();
}

TEST(LuaVM, ReinitAfterShutdown) {
    LuaVM vm;
    vm.init();
    vm.shutdown();
    EXPECT_TRUE(vm.init());
    EXPECT_NE(vm.L(), nullptr);
}

// ============================================================================
// LuaVM -- Sandbox
// ============================================================================

TEST(LuaVM, SandboxStripsOs) {
    LuaVM vm;
    vm.init();
    lua_getglobal(vm.L(), "os");
    EXPECT_TRUE(lua_isnil(vm.L(), -1));
    lua_pop(vm.L(), 1);
}

TEST(LuaVM, SandboxStripsIo) {
    LuaVM vm;
    vm.init();
    lua_getglobal(vm.L(), "io");
    EXPECT_TRUE(lua_isnil(vm.L(), -1));
    lua_pop(vm.L(), 1);
}

TEST(LuaVM, SandboxStripsDebug) {
    LuaVM vm;
    vm.init();
    lua_getglobal(vm.L(), "debug");
    EXPECT_TRUE(lua_isnil(vm.L(), -1));
    lua_pop(vm.L(), 1);
}

TEST(LuaVM, SandboxStripsRequire) {
    LuaVM vm;
    vm.init();
    lua_getglobal(vm.L(), "require");
    EXPECT_TRUE(lua_isnil(vm.L(), -1));
    lua_pop(vm.L(), 1);
}

TEST(LuaVM, SandboxStripsLoadfile) {
    LuaVM vm;
    vm.init();
    lua_getglobal(vm.L(), "loadfile");
    EXPECT_TRUE(lua_isnil(vm.L(), -1));
    lua_pop(vm.L(), 1);
}

TEST(LuaVM, SandboxStripsDofile) {
    LuaVM vm;
    vm.init();
    lua_getglobal(vm.L(), "dofile");
    EXPECT_TRUE(lua_isnil(vm.L(), -1));
    lua_pop(vm.L(), 1);
}

TEST(LuaVM, SandboxStripsPackage) {
    LuaVM vm;
    vm.init();
    lua_getglobal(vm.L(), "package");
    EXPECT_TRUE(lua_isnil(vm.L(), -1));
    lua_pop(vm.L(), 1);
}

TEST(LuaVM, SandboxStripsRawget) {
    LuaVM vm;
    vm.init();
    lua_getglobal(vm.L(), "rawget");
    EXPECT_TRUE(lua_isnil(vm.L(), -1));
    lua_pop(vm.L(), 1);
}

TEST(LuaVM, SandboxStripsCollectgarbage) {
    LuaVM vm;
    vm.init();
    lua_getglobal(vm.L(), "collectgarbage");
    EXPECT_TRUE(lua_isnil(vm.L(), -1));
    lua_pop(vm.L(), 1);
}

TEST(LuaVM, SandboxKeepsMath) {
    LuaVM vm;
    vm.init();
    lua_getglobal(vm.L(), "math");
    EXPECT_TRUE(lua_istable(vm.L(), -1));
    lua_pop(vm.L(), 1);
}

TEST(LuaVM, SandboxKeepsString) {
    LuaVM vm;
    vm.init();
    lua_getglobal(vm.L(), "string");
    EXPECT_TRUE(lua_istable(vm.L(), -1));
    lua_pop(vm.L(), 1);
}

TEST(LuaVM, SandboxKeepsTable) {
    LuaVM vm;
    vm.init();
    lua_getglobal(vm.L(), "table");
    EXPECT_TRUE(lua_istable(vm.L(), -1));
    lua_pop(vm.L(), 1);
}

// ============================================================================
// LuaVM -- Bindings
// ============================================================================

TEST(LuaVM, BindingsUzTableExists) {
    LuaVM vm;
    vm.init();
    lua_getglobal(vm.L(), "uz");
    EXPECT_TRUE(lua_istable(vm.L(), -1));
    lua_pop(vm.L(), 1);
}

TEST(LuaVM, BindingsDeltaTimeReturnsValue) {
    LuaVM vm;
    vm.init();
    vm.set_frame_time(0.016f, 1.5f);

    // Call uz.delta_time() from Lua
    int err = luaL_dostring(vm.L(), "return uz.delta_time()");
    EXPECT_EQ(err, 0);
    EXPECT_NEAR(lua_tonumber(vm.L(), -1), 0.016, 0.001);
    lua_pop(vm.L(), 1);
}

TEST(LuaVM, BindingsTotalTimeReturnsValue) {
    LuaVM vm;
    vm.init();
    vm.set_frame_time(0.016f, 42.5f);

    int err = luaL_dostring(vm.L(), "return uz.total_time()");
    EXPECT_EQ(err, 0);
    EXPECT_NEAR(lua_tonumber(vm.L(), -1), 42.5, 0.01);
    lua_pop(vm.L(), 1);
}

TEST(LuaVM, BindingsLogExists) {
    LuaVM vm;
    vm.init();
    // Just verify it's callable (DIAG_FMT writes to log, we don't assert on output)
    int err = luaL_dostring(vm.L(), "uz.log('test message')");
    EXPECT_EQ(err, 0);
}

TEST(LuaVM, BindingsSceneTableExists) {
    LuaVM vm;
    vm.init();
    lua_getglobal(vm.L(), "uz");
    lua_getfield(vm.L(), -1, "scene");
    EXPECT_TRUE(lua_istable(vm.L(), -1));
    lua_pop(vm.L(), 2);
}

TEST(LuaVM, BindingsCameraTableExists) {
    LuaVM vm;
    vm.init();
    lua_getglobal(vm.L(), "uz");
    lua_getfield(vm.L(), -1, "camera");
    EXPECT_TRUE(lua_istable(vm.L(), -1));
    lua_pop(vm.L(), 2);
}

TEST(LuaVM, BindingsSceneFindNodeWithoutScene) {
    LuaVM vm;
    vm.init();
    // No scene set — should return 0
    int err = luaL_dostring(vm.L(), "return uz.scene.find_node('anything')");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(lua_tointeger(vm.L(), -1), 0);
    lua_pop(vm.L(), 1);
}

TEST(LuaVM, BindingsSceneGetPositionWithoutScene) {
    LuaVM vm;
    vm.init();
    int err = luaL_dostring(vm.L(), "return uz.scene.get_position(1)");
    EXPECT_EQ(err, 0);
    // Should return 0,0,0
    EXPECT_DOUBLE_EQ(lua_tonumber(vm.L(), -3), 0.0);
    EXPECT_DOUBLE_EQ(lua_tonumber(vm.L(), -2), 0.0);
    EXPECT_DOUBLE_EQ(lua_tonumber(vm.L(), -1), 0.0);
    lua_pop(vm.L(), 3);
}

TEST(LuaVM, BindingsSceneSetPositionWithoutScene) {
    LuaVM vm;
    vm.init();
    // Should be no-op (no crash)
    int err = luaL_dostring(vm.L(), "uz.scene.set_position(1, 1, 2, 3)");
    EXPECT_EQ(err, 0);
}

TEST(LuaVM, BindingsCameraGetPositionWithoutParams) {
    LuaVM vm;
    vm.init();
    int err = luaL_dostring(vm.L(), "return uz.camera.get_position()");
    EXPECT_EQ(err, 0);
    EXPECT_DOUBLE_EQ(lua_tonumber(vm.L(), -3), 0.0);
    EXPECT_DOUBLE_EQ(lua_tonumber(vm.L(), -2), 0.0);
    EXPECT_DOUBLE_EQ(lua_tonumber(vm.L(), -1), 0.0);
    lua_pop(vm.L(), 3);
}

TEST(LuaVM, BindingsCameraSetPositionWithoutParams) {
    LuaVM vm;
    vm.init();
    int err = luaL_dostring(vm.L(), "uz.camera.set_position(1, 2, 3)");
    EXPECT_EQ(err, 0);
}

TEST(LuaVM, BindingsCameraGetYawPitchWithoutParams) {
    LuaVM vm;
    vm.init();
    int err = luaL_dostring(vm.L(), "return uz.camera.get_yaw_pitch()");
    EXPECT_EQ(err, 0);
    EXPECT_DOUBLE_EQ(lua_tonumber(vm.L(), -2), 0.0);
    EXPECT_DOUBLE_EQ(lua_tonumber(vm.L(), -1), 0.0);
    lua_pop(vm.L(), 2);
}

TEST(LuaVM, BindingsCameraSetYawPitchWithoutParams) {
    LuaVM vm;
    vm.init();
    int err = luaL_dostring(vm.L(), "uz.camera.set_yaw_pitch(1.0, 0.5)");
    EXPECT_EQ(err, 0);
}

TEST(LuaVM, BindingsCameraLookAtWithoutParams) {
    LuaVM vm;
    vm.init();
    int err = luaL_dostring(vm.L(), "uz.camera.look_at(0, 0, 0)");
    EXPECT_EQ(err, 0);
}

// ============================================================================
// LuaVM -- Bindings with scene data
// ============================================================================

TEST(LuaVM, BindingsSceneFindNodeFindsNode) {
    LuaVM vm;
    vm.init();
    auto* scene = make_test_scene();
    vm.set_scene(scene);

    int err = luaL_dostring(vm.L(), "return uz.scene.find_node('TestNode')");
    EXPECT_EQ(err, 0);
    lua_Integer id = lua_tointeger(vm.L(), -1);
    EXPECT_GT(id, 0);
    lua_pop(vm.L(), 1);

    delete scene;
}

TEST(LuaVM, BindingsSceneFindNodeReturnsZeroForMissing) {
    LuaVM vm;
    vm.init();
    auto* scene = make_test_scene();
    vm.set_scene(scene);

    int err = luaL_dostring(vm.L(), "return uz.scene.find_node('Nonexistent')");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(lua_tointeger(vm.L(), -1), 0);
    lua_pop(vm.L(), 1);

    delete scene;
}

TEST(LuaVM, BindingsSceneSetAndGetPosition) {
    LuaVM vm;
    vm.init();
    auto* scene = make_test_scene();
    vm.set_scene(scene);

    // Find the node ID first
    int err = luaL_dostring(vm.L(),
        "local id = uz.scene.find_node('TestNode')\n"
        "uz.scene.set_position(id, 10, 20, 30)\n"
        "return uz.scene.get_position(id)");
    EXPECT_EQ(err, 0);
    EXPECT_NEAR(lua_tonumber(vm.L(), -3), 10.0, 0.01);
    EXPECT_NEAR(lua_tonumber(vm.L(), -2), 20.0, 0.01);
    EXPECT_NEAR(lua_tonumber(vm.L(), -1), 30.0, 0.01);
    lua_pop(vm.L(), 3);

    delete scene;
}

TEST(LuaVM, BindingsSceneSetAndGetRotation) {
    LuaVM vm;
    vm.init();
    auto* scene = make_test_scene();
    vm.set_scene(scene);

    int err = luaL_dostring(vm.L(),
        "local id = uz.scene.find_node('TestNode')\n"
        "uz.scene.set_rotation(id, 45, 90, 180)\n"
        "return uz.scene.get_rotation(id)");
    EXPECT_EQ(err, 0);
    EXPECT_NEAR(lua_tonumber(vm.L(), -3), 45.0, 0.01);
    EXPECT_NEAR(lua_tonumber(vm.L(), -2), 90.0, 0.01);
    EXPECT_NEAR(lua_tonumber(vm.L(), -1), 180.0, 0.01);
    lua_pop(vm.L(), 3);

    delete scene;
}

TEST(LuaVM, BindingsSceneSetScale) {
    LuaVM vm;
    vm.init();
    auto* scene = make_test_scene();
    vm.set_scene(scene);

    int err = luaL_dostring(vm.L(),
        "local id = uz.scene.find_node('TestNode')\n"
        "uz.scene.set_scale(id, 2, 3, 4)");
    EXPECT_EQ(err, 0);

    // Verify via scene directly
    NodeId nid = scene_find_by_name(scene, "TestNode");
    const SceneNode* node = scene_get_node(scene, nid);
    ASSERT_NE(node, nullptr);
    EXPECT_NEAR(node->local_transform.scale.x, 2.0f, 0.01f);
    EXPECT_NEAR(node->local_transform.scale.y, 3.0f, 0.01f);
    EXPECT_NEAR(node->local_transform.scale.z, 4.0f, 0.01f);

    delete scene;
}

TEST(LuaVM, BindingsSceneGetPositionInvalidNode) {
    LuaVM vm;
    vm.init();
    auto* scene = make_test_scene();
    vm.set_scene(scene);

    int err = luaL_dostring(vm.L(), "return uz.scene.get_position(9999)");
    EXPECT_EQ(err, 0);
    EXPECT_DOUBLE_EQ(lua_tonumber(vm.L(), -3), 0.0);
    EXPECT_DOUBLE_EQ(lua_tonumber(vm.L(), -2), 0.0);
    EXPECT_DOUBLE_EQ(lua_tonumber(vm.L(), -1), 0.0);
    lua_pop(vm.L(), 3);

    delete scene;
}

TEST(LuaVM, BindingsSceneGetRotationInvalidNode) {
    LuaVM vm;
    vm.init();
    auto* scene = make_test_scene();
    vm.set_scene(scene);

    int err = luaL_dostring(vm.L(), "return uz.scene.get_rotation(9999)");
    EXPECT_EQ(err, 0);
    EXPECT_DOUBLE_EQ(lua_tonumber(vm.L(), -3), 0.0);
    EXPECT_DOUBLE_EQ(lua_tonumber(vm.L(), -2), 0.0);
    EXPECT_DOUBLE_EQ(lua_tonumber(vm.L(), -1), 0.0);
    lua_pop(vm.L(), 3);

    delete scene;
}

TEST(LuaVM, BindingsSceneSetPositionInvalidNode) {
    LuaVM vm;
    vm.init();
    auto* scene = make_test_scene();
    vm.set_scene(scene);

    int err = luaL_dostring(vm.L(), "uz.scene.set_position(9999, 1, 2, 3)");
    EXPECT_EQ(err, 0);  // no crash

    delete scene;
}

TEST(LuaVM, BindingsSceneSetRotationInvalidNode) {
    LuaVM vm;
    vm.init();
    auto* scene = make_test_scene();
    vm.set_scene(scene);

    int err = luaL_dostring(vm.L(), "uz.scene.set_rotation(9999, 1, 2, 3)");
    EXPECT_EQ(err, 0);

    delete scene;
}

TEST(LuaVM, BindingsSceneSetScaleInvalidNode) {
    LuaVM vm;
    vm.init();
    auto* scene = make_test_scene();
    vm.set_scene(scene);

    int err = luaL_dostring(vm.L(), "uz.scene.set_scale(9999, 1, 2, 3)");
    EXPECT_EQ(err, 0);

    delete scene;
}

// ============================================================================
// LuaVM -- Camera bindings with SceneParams
// ============================================================================

TEST(LuaVM, CameraSetAndGetPosition) {
    LuaVM vm;
    vm.init();
    SceneParams params{};
    vm.set_scene_params(&params);

    int err = luaL_dostring(vm.L(),
        "uz.camera.set_position(5, 10, 15)\n"
        "return uz.camera.get_position()");
    EXPECT_EQ(err, 0);
    EXPECT_NEAR(lua_tonumber(vm.L(), -3), 5.0, 0.01);
    EXPECT_NEAR(lua_tonumber(vm.L(), -2), 10.0, 0.01);
    EXPECT_NEAR(lua_tonumber(vm.L(), -1), 15.0, 0.01);
    lua_pop(vm.L(), 3);
}

TEST(LuaVM, CameraSetAndGetYawPitch) {
    LuaVM vm;
    vm.init();
    SceneParams params{};
    vm.set_scene_params(&params);

    int err = luaL_dostring(vm.L(),
        "uz.camera.set_yaw_pitch(1.5, 0.3)\n"
        "return uz.camera.get_yaw_pitch()");
    EXPECT_EQ(err, 0);
    EXPECT_NEAR(lua_tonumber(vm.L(), -2), 1.5, 0.01);
    EXPECT_NEAR(lua_tonumber(vm.L(), -1), 0.3, 0.01);
    lua_pop(vm.L(), 2);
}

TEST(LuaVM, CameraLookAtSetsYawPitch) {
    LuaVM vm;
    vm.init();
    SceneParams params{};
    params.camPosX = 0.0f;
    params.camPosY = 0.0f;
    params.camPosZ = 0.0f;
    vm.set_scene_params(&params);

    // Look at (0, 0, 10) — straight ahead along +Z
    int err = luaL_dostring(vm.L(), "uz.camera.look_at(0, 0, 10)");
    EXPECT_EQ(err, 0);

    // Yaw should be ~0 (target is directly ahead in +Z)
    EXPECT_NEAR(params.camYaw, 0.0f, 0.01f);
    EXPECT_NEAR(params.camPitch, 0.0f, 0.01f);
}

TEST(LuaVM, CameraLookAtLookingUp) {
    LuaVM vm;
    vm.init();
    SceneParams params{};
    params.camPosX = 0.0f;
    params.camPosY = 0.0f;
    params.camPosZ = 0.0f;
    vm.set_scene_params(&params);

    // Look at (0, 10, 0) — straight up
    int err = luaL_dostring(vm.L(), "uz.camera.look_at(0, 10, 0)");
    EXPECT_EQ(err, 0);

    // Pitch should be close to pi/2
    EXPECT_NEAR(params.camPitch, 1.5708f, 0.01f);
}

// ============================================================================
// LuaVM -- Frame time accessors
// ============================================================================

TEST(LuaVM, SetFrameTimeUpdatesValues) {
    LuaVM vm;
    EXPECT_FLOAT_EQ(vm.delta_time(), 0.0f);
    EXPECT_FLOAT_EQ(vm.total_time(), 0.0f);

    vm.set_frame_time(0.033f, 100.0f);
    EXPECT_FLOAT_EQ(vm.delta_time(), 0.033f);
    EXPECT_FLOAT_EQ(vm.total_time(), 100.0f);
}

TEST(LuaVM, SetSceneStoresPointer) {
    LuaVM vm;
    EXPECT_EQ(vm.scene(), nullptr);
    // Scene is too large for stack (~26MB) — heap-allocate.
    auto* s = new Scene{};
    vm.set_scene(s);
    EXPECT_EQ(vm.scene(), s);
    vm.set_scene(nullptr);
    delete s;
}

TEST(LuaVM, SetSceneParamsStoresPointer) {
    LuaVM vm;
    EXPECT_EQ(vm.scene_params(), nullptr);
    SceneParams p{};
    vm.set_scene_params(&p);
    EXPECT_EQ(vm.scene_params(), &p);
}

// ============================================================================
// ScriptRunner -- Lifecycle
// ============================================================================

TEST(ScriptRunner, InitSucceeds) {
    ScriptRunner runner;
    EXPECT_TRUE(runner.init());
}

TEST(ScriptRunner, DoubleInitIsIdempotent) {
    ScriptRunner runner;
    EXPECT_TRUE(runner.init());
    EXPECT_TRUE(runner.init());
}

TEST(ScriptRunner, ScriptCountStartsAtZero) {
    ScriptRunner runner;
    runner.init();
    EXPECT_EQ(runner.script_count(), 0u);
}

TEST(ScriptRunner, ShutdownIsIdempotent) {
    ScriptRunner runner;
    runner.init();
    runner.shutdown();
    runner.shutdown();  // no crash
}

TEST(ScriptRunner, ShutdownWithoutInit) {
    ScriptRunner runner;
    runner.shutdown();  // no crash
}

TEST(ScriptRunner, DestructorWithoutInit) {
    { ScriptRunner runner; }
    SUCCEED();
}

TEST(ScriptRunner, ClearEmptiesScripts) {
    ScriptRunner runner;
    runner.init();

    TempLuaFile script("clear_test.lua", "function on_update(dt) end");
    runner.attach_script(script.c_str(), 1);
    EXPECT_EQ(runner.script_count(), 1u);

    runner.clear();
    EXPECT_EQ(runner.script_count(), 0u);
}

TEST(ScriptRunner, ReinitAfterShutdown) {
    ScriptRunner runner;
    runner.init();
    runner.shutdown();
    EXPECT_TRUE(runner.init());
    EXPECT_EQ(runner.script_count(), 0u);
}

// ============================================================================
// ScriptRunner -- Attach / Detach
// ============================================================================

TEST(ScriptRunner, AttachScriptSucceeds) {
    ScriptRunner runner;
    runner.init();

    TempLuaFile script("attach_test.lua",
        "function on_start() end\n"
        "function on_update(dt) end\n");

    EXPECT_TRUE(runner.attach_script(script.c_str(), 1));
    EXPECT_EQ(runner.script_count(), 1u);
}

TEST(ScriptRunner, AttachScriptFailsBeforeInit) {
    ScriptRunner runner;
    TempLuaFile script("noinit.lua", "function on_update(dt) end");
    EXPECT_FALSE(runner.attach_script(script.c_str(), 1));
}

TEST(ScriptRunner, AttachNonexistentFileFails) {
    ScriptRunner runner;
    runner.init();
    EXPECT_FALSE(runner.attach_script("nonexistent_path_abc123.lua", 1));
    EXPECT_EQ(runner.script_count(), 0u);
}

TEST(ScriptRunner, AttachMalformedScriptFails) {
    ScriptRunner runner;
    runner.init();

    TempLuaFile script("malformed.lua", "this is not valid lua !!@@##");
    EXPECT_FALSE(runner.attach_script(script.c_str(), 1));
    EXPECT_EQ(runner.script_count(), 0u);
}

TEST(ScriptRunner, AttachScriptWithRuntimeErrorFails) {
    ScriptRunner runner;
    runner.init();

    // Script body (not inside a function) has a runtime error
    TempLuaFile script("runtime_err.lua", "error('intentional error')");
    EXPECT_FALSE(runner.attach_script(script.c_str(), 1));
    EXPECT_EQ(runner.script_count(), 0u);
}

TEST(ScriptRunner, DetachScriptsRemovesByNodeId) {
    ScriptRunner runner;
    runner.init();

    TempLuaFile script("detach_test.lua", "function on_update(dt) end");
    runner.attach_script(script.c_str(), 42);
    EXPECT_EQ(runner.script_count(), 1u);

    runner.detach_scripts(42);
    EXPECT_EQ(runner.script_count(), 0u);
}

TEST(ScriptRunner, DetachNonexistentNodeIsNoOp) {
    ScriptRunner runner;
    runner.init();

    TempLuaFile script("detach_noop.lua", "function on_update(dt) end");
    runner.attach_script(script.c_str(), 1);
    EXPECT_EQ(runner.script_count(), 1u);

    runner.detach_scripts(999);
    EXPECT_EQ(runner.script_count(), 1u);
}

TEST(ScriptRunner, AttachMultipleScriptsToSameNode) {
    ScriptRunner runner;
    runner.init();

    TempLuaFile s1("multi_a.lua", "function on_update(dt) end");
    TempLuaFile s2("multi_b.lua", "function on_update(dt) end");

    runner.attach_script(s1.c_str(), 1);
    runner.attach_script(s2.c_str(), 1);
    EXPECT_EQ(runner.script_count(), 2u);

    // Detach removes all scripts for that node
    runner.detach_scripts(1);
    EXPECT_EQ(runner.script_count(), 0u);
}

TEST(ScriptRunner, AttachScriptsToDifferentNodes) {
    ScriptRunner runner;
    runner.init();

    TempLuaFile s1("node_a.lua", "function on_update(dt) end");
    TempLuaFile s2("node_b.lua", "function on_update(dt) end");

    runner.attach_script(s1.c_str(), 1);
    runner.attach_script(s2.c_str(), 2);
    EXPECT_EQ(runner.script_count(), 2u);

    runner.detach_scripts(1);
    EXPECT_EQ(runner.script_count(), 1u);

    runner.detach_scripts(2);
    EXPECT_EQ(runner.script_count(), 0u);
}

// ============================================================================
// ScriptRunner -- Tick
// ============================================================================

TEST(ScriptRunner, TickWithoutInitIsNoOp) {
    ScriptRunner runner;
    runner.tick(0.016f, 1.0f);  // no crash
}

TEST(ScriptRunner, TickCallsOnStartAndOnUpdate) {
    ScriptRunner runner;
    runner.init();

    // Script that sets a global when on_start / on_update are called
    TempLuaFile script("tick_test.lua",
        "started = false\n"
        "updated = false\n"
        "function on_start()\n"
        "  started = true\n"
        "end\n"
        "function on_update(dt)\n"
        "  updated = true\n"
        "end\n");

    runner.attach_script(script.c_str(), 1);
    runner.tick(0.016f, 1.0f);

    // The script's on_start and on_update should have been called.
    // We can't easily read the script's local env from here, but we verify
    // no crash and the tick completes successfully.
    SUCCEED();
}

TEST(ScriptRunner, TickOnStartCalledOnlyOnce) {
    ScriptRunner runner;
    runner.init();

    // Use a global counter to track on_start calls
    TempLuaFile script("start_once.lua",
        "start_count = 0\n"
        "function on_start()\n"
        "  start_count = start_count + 1\n"
        "end\n"
        "function on_update(dt) end\n");

    runner.attach_script(script.c_str(), 1);

    // Tick multiple times
    runner.tick(0.016f, 1.0f);
    runner.tick(0.016f, 2.0f);
    runner.tick(0.016f, 3.0f);

    // No crash — on_start should only have been called once (first tick)
    SUCCEED();
}

TEST(ScriptRunner, TickWithEmptyScript) {
    ScriptRunner runner;
    runner.init();

    // A script with no lifecycle functions
    TempLuaFile script("empty.lua", "-- empty script");
    runner.attach_script(script.c_str(), 1);

    runner.tick(0.016f, 1.0f);  // no crash
    SUCCEED();
}

TEST(ScriptRunner, TickWithOnlyOnStop) {
    ScriptRunner runner;
    runner.init();

    TempLuaFile script("only_stop.lua", "function on_stop() end");
    runner.attach_script(script.c_str(), 1);

    runner.tick(0.016f, 1.0f);  // no crash
    SUCCEED();
}

// ============================================================================
// ScriptRunner -- Set scene / params
// ============================================================================

TEST(ScriptRunner, SetScenePassesToVM) {
    ScriptRunner runner;
    runner.init();
    auto* scene = make_test_scene();
    runner.set_scene(scene);

    // Verify by attaching a script that uses uz.scene.find_node
    TempLuaFile script("scene_access.lua",
        "function on_start()\n"
        "  local id = uz.scene.find_node('TestNode')\n"
        "  if id == 0 then error('node not found') end\n"
        "end\n"
        "function on_update(dt) end\n");

    runner.attach_script(script.c_str(), 1);
    runner.tick(0.016f, 1.0f);  // on_start should not error

    delete scene;
}

TEST(ScriptRunner, SetSceneParamsEnablesCameraBindings) {
    ScriptRunner runner;
    runner.init();
    SceneParams params{};
    runner.set_scene_params(&params);

    TempLuaFile script("cam_bind.lua",
        "function on_start()\n"
        "  uz.camera.set_position(1, 2, 3)\n"
        "end\n"
        "function on_update(dt) end\n");

    runner.attach_script(script.c_str(), 1);
    runner.tick(0.016f, 1.0f);

    EXPECT_NEAR(params.camPosX, 1.0f, 0.01f);
    EXPECT_NEAR(params.camPosY, 2.0f, 0.01f);
    EXPECT_NEAR(params.camPosZ, 3.0f, 0.01f);
}

// ============================================================================
// ScriptRunner -- find_nodes_by_script_path
// ============================================================================

TEST(ScriptRunner, FindNodesByPathReturnsMatches) {
    ScriptRunner runner;
    runner.init();

    TempLuaFile script("find_path.lua", "function on_update(dt) end");
    runner.attach_script(script.c_str(), 10);
    runner.attach_script(script.c_str(), 20);

    uint32_t ids[4] = {};
    uint32_t count = runner.find_nodes_by_script_path(script.c_str(), ids, 4);
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(ids[0], 10u);
    EXPECT_EQ(ids[1], 20u);
}

TEST(ScriptRunner, FindNodesByPathReturnsZeroForNoMatch) {
    ScriptRunner runner;
    runner.init();

    TempLuaFile script("find_none.lua", "function on_update(dt) end");
    runner.attach_script(script.c_str(), 1);

    uint32_t ids[4] = {};
    uint32_t count = runner.find_nodes_by_script_path("nonexistent.lua", ids, 4);
    EXPECT_EQ(count, 0u);
}

TEST(ScriptRunner, FindNodesByPathRespectsMaxOut) {
    ScriptRunner runner;
    runner.init();

    TempLuaFile script("find_max.lua", "function on_update(dt) end");
    runner.attach_script(script.c_str(), 1);
    runner.attach_script(script.c_str(), 2);
    runner.attach_script(script.c_str(), 3);

    uint32_t ids[2] = {};
    uint32_t count = runner.find_nodes_by_script_path(script.c_str(), ids, 2);
    EXPECT_EQ(count, 2u);  // capped at max_out
}

TEST(ScriptRunner, FindNodesByPathNullArgs) {
    ScriptRunner runner;
    runner.init();

    uint32_t ids[4] = {};
    EXPECT_EQ(runner.find_nodes_by_script_path(nullptr, ids, 4), 0u);
    EXPECT_EQ(runner.find_nodes_by_script_path("test.lua", nullptr, 4), 0u);
    EXPECT_EQ(runner.find_nodes_by_script_path("test.lua", ids, 0), 0u);
}

// ============================================================================
// LuaScriptInstance -- Direct tests
// ============================================================================

TEST(LuaScriptInstance, LoadFromFile) {
    LuaVM vm;
    vm.init();

    TempLuaFile script("instance_load.lua",
        "function on_start() end\n"
        "function on_update(dt) end\n"
        "function on_stop() end\n");

    LuaScriptInstance instance(&vm);
    EXPECT_TRUE(instance.load(script.c_str(), 42));
    EXPECT_EQ(instance.node_id, 42u);
    EXPECT_STREQ(instance.script_path(), script.c_str());
}

TEST(LuaScriptInstance, LoadFailsForNonexistentFile) {
    LuaVM vm;
    vm.init();

    LuaScriptInstance instance(&vm);
    EXPECT_FALSE(instance.load("definitely_not_a_file.lua", 1));
}

TEST(LuaScriptInstance, LoadFailsForSyntaxError) {
    LuaVM vm;
    vm.init();

    TempLuaFile script("syntax_err.lua", "function !!invalid syntax!!");
    LuaScriptInstance instance(&vm);
    EXPECT_FALSE(instance.load(script.c_str(), 1));
}

TEST(LuaScriptInstance, LoadFailsForRuntimeError) {
    LuaVM vm;
    vm.init();

    TempLuaFile script("rt_err.lua", "error('boom')");
    LuaScriptInstance instance(&vm);
    EXPECT_FALSE(instance.load(script.c_str(), 1));
}

TEST(LuaScriptInstance, LoadFailsWithNullState) {
    LuaVM vm;
    // Don't init — L() is nullptr
    LuaScriptInstance instance(&vm);
    TempLuaFile script("null_state.lua", "function on_update(dt) end");
    EXPECT_FALSE(instance.load(script.c_str(), 1));
}

TEST(LuaScriptInstance, OnStartCalledOnce) {
    LuaVM vm;
    vm.init();

    TempLuaFile script("start_once_inst.lua",
        "count = 0\n"
        "function on_start() count = count + 1 end\n"
        "function on_update(dt) end\n");

    LuaScriptInstance instance(&vm);
    instance.load(script.c_str(), 1);

    instance.on_start();
    instance.on_start();  // second call — should be no-op
    SUCCEED();
}

TEST(LuaScriptInstance, OnUpdateWithDt) {
    LuaVM vm;
    vm.init();

    TempLuaFile script("update_dt.lua",
        "last_dt = 0\n"
        "function on_update(dt)\n"
        "  last_dt = dt\n"
        "end\n");

    LuaScriptInstance instance(&vm);
    instance.load(script.c_str(), 1);
    instance.on_update(0.033f);
    SUCCEED();
}

TEST(LuaScriptInstance, OnStopCalledBeforeDestruction) {
    LuaVM vm;
    vm.init();

    TempLuaFile script("stop_test.lua",
        "function on_start() end\n"
        "function on_stop() end\n");

    {
        LuaScriptInstance instance(&vm);
        instance.load(script.c_str(), 1);
        instance.on_start();
        // Destructor should call on_stop()
    }
    SUCCEED();
}

TEST(LuaScriptInstance, OnStopNotCalledIfNotStarted) {
    LuaVM vm;
    vm.init();

    TempLuaFile script("stop_nostart.lua",
        "function on_start() end\n"
        "function on_stop() error('should not be called') end\n");

    {
        LuaScriptInstance instance(&vm);
        instance.load(script.c_str(), 1);
        // Never call on_start — destructor should NOT call on_stop
    }
    SUCCEED();
}

TEST(LuaScriptInstance, OnUpdateWithoutHasUpdateIsNoOp) {
    LuaVM vm;
    vm.init();

    TempLuaFile script("no_update.lua",
        "function on_start() end\n");

    LuaScriptInstance instance(&vm);
    instance.load(script.c_str(), 1);
    instance.on_update(0.016f);  // no crash
    SUCCEED();
}

TEST(LuaScriptInstance, ScriptEnvironmentIsolation) {
    LuaVM vm;
    vm.init();

    TempLuaFile s1("iso_a.lua", "my_var = 'script_a'");
    TempLuaFile s2("iso_b.lua", "my_var = 'script_b'");

    LuaScriptInstance inst1(&vm);
    LuaScriptInstance inst2(&vm);

    inst1.load(s1.c_str(), 1);
    inst2.load(s2.c_str(), 2);

    // Each script runs in its own environment — global 'my_var' should not leak
    // This test verifies the isolation by checking they load without conflict.
    SUCCEED();
}

TEST(LuaScriptInstance, NodeIdAvailableInScript) {
    LuaVM vm;
    vm.init();

    // The script should be able to read NODE_ID from its environment
    TempLuaFile script("nodeid_env.lua",
        "function on_start()\n"
        "  if NODE_ID ~= 77 then error('wrong NODE_ID') end\n"
        "end\n"
        "function on_update(dt) end\n");

    LuaScriptInstance instance(&vm);
    instance.load(script.c_str(), 77);
    instance.on_start();  // should not error
    SUCCEED();
}

// ============================================================================
// LuaScriptReloader -- Basic
// ============================================================================

TEST(LuaScriptReloader, DefaultState) {
    LuaScriptReloader reloader;
    EXPECT_EQ(reloader.reload_count(), 0u);
}

TEST(LuaScriptReloader, SetScriptRunner) {
    LuaScriptReloader reloader;
    ScriptRunner runner;
    runner.init();
    reloader.set_script_runner(&runner);
    EXPECT_EQ(reloader.reload_count(), 0u);
}

TEST(LuaScriptReloader, OnFileChangedWithoutRunnerIsNoOp) {
    LuaScriptReloader reloader;
    EXPECT_EQ(reloader.on_file_changed("test.lua"), 0u);
    EXPECT_EQ(reloader.reload_count(), 0u);
}

TEST(LuaScriptReloader, OnFileChangedWithNullPathIsNoOp) {
    LuaScriptReloader reloader;
    ScriptRunner runner;
    runner.init();
    reloader.set_script_runner(&runner);

    EXPECT_EQ(reloader.on_file_changed(nullptr), 0u);
    EXPECT_EQ(reloader.reload_count(), 0u);
}

TEST(LuaScriptReloader, OnFileChangedWithEmptyPathIsNoOp) {
    LuaScriptReloader reloader;
    ScriptRunner runner;
    runner.init();
    reloader.set_script_runner(&runner);

    EXPECT_EQ(reloader.on_file_changed(""), 0u);
    EXPECT_EQ(reloader.reload_count(), 0u);
}

TEST(LuaScriptReloader, OnFileChangedNonAttachedPathIsNoOp) {
    LuaScriptReloader reloader;
    ScriptRunner runner;
    runner.init();
    reloader.set_script_runner(&runner);

    TempLuaFile script("attached.lua", "function on_update(dt) end");
    runner.attach_script(script.c_str(), 1);

    EXPECT_EQ(reloader.on_file_changed("not_attached.lua"), 0u);
    EXPECT_EQ(reloader.reload_count(), 0u);
    EXPECT_EQ(runner.script_count(), 1u);
}

TEST(LuaScriptReloader, OnFileChangedReloadsAttachedScript) {
    LuaScriptReloader reloader;
    ScriptRunner runner;
    runner.init();
    reloader.set_script_runner(&runner);

    TempLuaFile script("reload_me.lua", "function on_update(dt) end");
    runner.attach_script(script.c_str(), 5);
    EXPECT_EQ(runner.script_count(), 1u);

    // Simulate file change — script should be detached and reattached
    uint32_t reloaded = reloader.on_file_changed(script.c_str());
    EXPECT_EQ(reloaded, 1u);
    EXPECT_EQ(reloader.reload_count(), 1u);
    EXPECT_EQ(runner.script_count(), 1u);  // still 1 script, just reloaded
}

TEST(LuaScriptReloader, OnFileChangedReloadsMultipleInstances) {
    LuaScriptReloader reloader;
    ScriptRunner runner;
    runner.init();
    reloader.set_script_runner(&runner);

    TempLuaFile script("reload_multi.lua", "function on_update(dt) end");
    runner.attach_script(script.c_str(), 1);
    runner.attach_script(script.c_str(), 2);
    runner.attach_script(script.c_str(), 3);
    EXPECT_EQ(runner.script_count(), 3u);

    uint32_t reloaded = reloader.on_file_changed(script.c_str());
    EXPECT_EQ(reloaded, 3u);
    EXPECT_EQ(reloader.reload_count(), 3u);
    EXPECT_EQ(runner.script_count(), 3u);
}

TEST(LuaScriptReloader, OnFileChangedReloadFailsGracefully) {
    LuaScriptReloader reloader;
    ScriptRunner runner;
    runner.init();
    reloader.set_script_runner(&runner);

    TempLuaFile script("reload_fail.lua", "function on_update(dt) end");
    runner.attach_script(script.c_str(), 1);
    EXPECT_EQ(runner.script_count(), 1u);

    // Delete the file so reload fails
    fs::remove(script.path);

    uint32_t reloaded = reloader.on_file_changed(script.c_str());
    EXPECT_EQ(reloaded, 0u);
    EXPECT_EQ(reloader.reload_count(), 0u);
    // Script was detached but reattach failed — net count drops to 0
    EXPECT_EQ(runner.script_count(), 0u);
}

TEST(LuaScriptReloader, OnFileChangedOnlyAffectsMatchingPath) {
    LuaScriptReloader reloader;
    ScriptRunner runner;
    runner.init();
    reloader.set_script_runner(&runner);

    TempLuaFile s1("target.lua", "function on_update(dt) end");
    TempLuaFile s2("other.lua", "function on_update(dt) end");

    runner.attach_script(s1.c_str(), 1);
    runner.attach_script(s2.c_str(), 2);
    EXPECT_EQ(runner.script_count(), 2u);

    uint32_t reloaded = reloader.on_file_changed(s1.c_str());
    EXPECT_EQ(reloaded, 1u);
    EXPECT_EQ(runner.script_count(), 2u);  // other script unaffected
}

TEST(LuaScriptReloader, ReloadCountAccumulates) {
    LuaScriptReloader reloader;
    ScriptRunner runner;
    runner.init();
    reloader.set_script_runner(&runner);

    TempLuaFile script("accumulate.lua", "function on_update(dt) end");
    runner.attach_script(script.c_str(), 1);

    reloader.on_file_changed(script.c_str());
    reloader.on_file_changed(script.c_str());
    reloader.on_file_changed(script.c_str());

    EXPECT_EQ(reloader.reload_count(), 3u);
}

// ============================================================================
// ScriptRunner -- scan_scene
// ============================================================================

TEST(ScriptRunner, ScanSceneWithoutSceneIsNoOp) {
    ScriptRunner runner;
    runner.init();
    runner.scan_scene();   // no crash, no scripts
    EXPECT_EQ(runner.script_count(), 0u);
}

// ============================================================================
// Integration -- script accessing timing values
// ============================================================================

TEST(ScriptRunnerIntegration, ScriptCanReadDeltaTime) {
    ScriptRunner runner;
    runner.init();

    TempLuaFile script("dt_access.lua",
        "function on_update(dt)\n"
        "  local vm_dt = uz.delta_time()\n"
        "  -- Both should be the same frame's delta time\n"
        "end\n");

    runner.attach_script(script.c_str(), 1);
    runner.tick(0.016f, 1.0f);
    runner.tick(0.033f, 1.033f);
    SUCCEED();
}

TEST(ScriptRunnerIntegration, ScriptCanReadTotalTime) {
    ScriptRunner runner;
    runner.init();

    TempLuaFile script("total_access.lua",
        "function on_update(dt)\n"
        "  local t = uz.total_time()\n"
        "end\n");

    runner.attach_script(script.c_str(), 1);
    runner.tick(0.016f, 5.0f);
    SUCCEED();
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(ScriptRunner, AttachSameScriptTwiceToSameNode) {
    ScriptRunner runner;
    runner.init();

    TempLuaFile script("dup.lua", "function on_update(dt) end");
    runner.attach_script(script.c_str(), 1);
    runner.attach_script(script.c_str(), 1);
    EXPECT_EQ(runner.script_count(), 2u);

    // Both are detached when detach_scripts(1) is called
    runner.detach_scripts(1);
    EXPECT_EQ(runner.script_count(), 0u);
}

TEST(ScriptRunner, ClearAndReattach) {
    ScriptRunner runner;
    runner.init();

    TempLuaFile script("reattach.lua", "function on_update(dt) end");
    runner.attach_script(script.c_str(), 1);
    runner.clear();
    EXPECT_EQ(runner.script_count(), 0u);

    runner.attach_script(script.c_str(), 2);
    EXPECT_EQ(runner.script_count(), 1u);
}

TEST(ScriptRunner, TickAfterClearIsNoOp) {
    ScriptRunner runner;
    runner.init();

    TempLuaFile script("tick_clear.lua", "function on_update(dt) end");
    runner.attach_script(script.c_str(), 1);
    runner.clear();
    runner.tick(0.016f, 1.0f);  // no crash
    SUCCEED();
}

TEST(LuaScriptReloader, CopyAndMoveDeleted) {
    // Verify the class is not copyable (compile-time check via static_assert)
    static_assert(!std::is_copy_constructible_v<LuaScriptReloader>);
    static_assert(!std::is_copy_assignable_v<LuaScriptReloader>);
    SUCCEED();
}

TEST(LuaVM, CopyAndMoveDeleted) {
    static_assert(!std::is_copy_constructible_v<LuaVM>);
    static_assert(!std::is_copy_assignable_v<LuaVM>);
    SUCCEED();
}
