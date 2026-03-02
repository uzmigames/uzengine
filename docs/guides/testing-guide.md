# Testing Guide

This guide covers writing comprehensive tests for UzEngine — unit tests, integration tests, and performance benchmarks with Google Test framework.

---

## Overview

UzEngine uses **Google Test (GoogleTest)** with **mock frameworks** for testing C++ code. The project requires **95%+ code coverage** for all new code.

### Key Principles

- **95% coverage minimum** — Every line of code is tested
- **No placeholder tests** — Every test verifies actual behavior
- **Test all paths** — Happy path, error cases, edge cases
- **Isolated tests** — Unit tests don't depend on I/O or external systems
- **Fast execution** — Full test suite completes in < 5 seconds

---

## Project Test Structure

```
project/
├── CMakeLists.txt              ; Top-level build config
├── tests/
│   ├── CMakeLists.txt          ; Test target configuration
│   ├── unit/
│   │   ├── core_test.cpp       ; Core API tests
│   │   ├── scene_test.cpp      ; Scene system tests
│   │   ├── lua_test.cpp        ; Lua scripting tests
│   │   ├── physics_test.cpp    ; Physics system tests
│   │   └── audio_test.cpp      ; Audio system tests
│   ├── integration/
│   │   ├── scene_load_test.cpp ; Scene loading from files
│   │   ├── asset_import_test.cpp ; Asset import pipeline
│   │   └── prefab_test.cpp     ; Prefab instantiation
│   ├── performance/
│   │   ├── physics_bench.cpp   ; Physics simulation benchmarks
│   │   └── render_bench.cpp    ; Rendering performance
│   └── fixtures/               ; Test data
│       ├── simple.uzscene      ; Test scene
│       ├── test_script.lua     ; Test Lua script
│       └── test_model.glb      ; Test mesh
```

---

## Running Tests

### Build Tests

```bash
# Configure with test preset
cmake -B build --preset test

# Build test targets
cmake --build build --config Debug --target RUN_TESTS
```

### Run All Tests

```bash
cd build
ctest --output-on-failure
```

Output:

```
Test project build/
    Start 1: CoreTests
1/8 Test #1: CoreTests ........................   Passed    0.45 sec
    Start 2: SceneTests
2/8 Test #2: SceneTests ......................   Passed    1.23 sec
    Start 3: LuaTests
3/8 Test #3: LuaTests ........................   Passed    0.82 sec
...
100% tests passed, 0 tests failed out of 8
Total Test time (real) = 5.12 sec
```

### Run Specific Test Suite

```bash
ctest -R "LuaTests" --output-on-failure
```

Or directly:

```bash
./build/Debug/tests/lua_test.exe
```

### Run Single Test

```bash
ctest -R "LuaTests\.on_start_hook" --output-on-failure
```

Or with gtest syntax:

```bash
./build/Debug/tests/lua_test.exe --gtest_filter="LuaTests.on_start_hook"
```

---

## Test Coverage

### Generate Coverage Report

```bash
# Build with coverage instrumentation
cmake -B build --preset coverage
cmake --build build

# Run tests to collect coverage data
ctest

# Generate HTML report
gcovr --html --html-details coverage.html

# View in browser
start coverage.html
```

Coverage report shows:

```
Overall coverage: 96.2%
  Core: 98.1%
  Scene: 94.5%
  Lua: 96.8%
  Physics: 94.2%
```

### Coverage Threshold

The CI/CD pipeline enforces **95% minimum coverage**:

```
✓ Coverage: 96.2% (PASS)
```

If coverage drops below 95%, the build fails:

```
✗ Coverage: 93.8% (FAIL)
  Missing coverage: scene_manager.cpp:234 (5 lines)
```

---

## Writing Tests with Google Test

### Basic Test Structure

```cpp
#include <gtest/gtest.h>

class SceneTests : public ::testing::Test {
protected:
    void SetUp() override {
        ; Initialize before each test
        scene_manager = std::make_unique<SceneManager>();
    }

    void TearDown() override {
        ; Clean up after each test
        scene_manager.reset();
    }

    std::unique_ptr<SceneManager> scene_manager;
};

TEST_F(SceneTests, can_create_node) {
    NodeId node = scene_manager->create_node("Test");
    EXPECT_NE(node, 0);
}

TEST_F(SceneTests, node_has_default_transform) {
    NodeId node = scene_manager->create_node("Test");
    Transform t = scene_manager->get_transform(node);
    EXPECT_EQ(t.position, Vec3(0, 0, 0));
    EXPECT_EQ(t.rotation, Quaternion::identity());
    EXPECT_EQ(t.scale, Vec3(1, 1, 1));
}
```

### Test Naming

Use descriptive names following pattern: `Test<Component>.<Scenario>`

**Good:**
```cpp
TEST_F(SceneTests, create_node_with_unique_id) { }
TEST_F(SceneTests, reparent_node_updates_hierarchy) { }
TEST_F(LuaTests, on_start_hook_called_once) { }
TEST_F(PhysicsTests, collision_event_fires_for_dynamic_bodies) { }
```

**Bad:**
```cpp
TEST_F(SceneTests, test1) { }  ; Too vague
TEST_F(SceneTests, works) { }  ; No behavior described
TEST_F(SceneTests, create_node_success) { }  ; "success" is implicit
```

### Assertion Macros

| Macro | Behavior |
|-------|----------|
| `EXPECT_*` | Test continues after failure (non-fatal) |
| `ASSERT_*` | Test stops after failure (fatal) |

**Common assertions:**

```cpp
; Boolean
EXPECT_TRUE(x);
EXPECT_FALSE(x);

; Comparison
EXPECT_EQ(actual, expected);        ; actual == expected
EXPECT_NE(actual, expected);        ; actual != expected
EXPECT_LT(actual, expected);        ; actual < expected
EXPECT_LE(actual, expected);        ; actual <= expected
EXPECT_GT(actual, expected);        ; actual > expected
EXPECT_GE(actual, expected);        ; actual >= expected

; Floating point (with tolerance)
EXPECT_FLOAT_EQ(actual, expected);  ; ±4 ULPs
EXPECT_DOUBLE_EQ(actual, expected); ; ±4 ULPs
EXPECT_NEAR(actual, expected, tolerance);

; Strings
EXPECT_STREQ(actual, expected);     ; C-style strings
EXPECT_EQ(std::string(actual), expected);  ; std::string

; Containers
EXPECT_THAT(container, ::testing::ElementsAre(1, 2, 3));
EXPECT_THAT(container, ::testing::Contains(5));

; Exceptions
EXPECT_THROW(expr, ExceptionType);
EXPECT_NO_THROW(expr);
```

---

## Mock Patterns

### Creating Mock Objects

Use Google Mock (`gmock`) for testing code that depends on external services:

```cpp
#include <gmock/gmock.h>

; Mock interface
class MockAssetManager : public IAssetManager {
public:
    MOCK_METHOD(Mesh*, load_mesh, (const char* path), (override));
    MOCK_METHOD(Material*, load_material, (const char* path), (override));
    MOCK_METHOD(void, unload_asset, (AssetHandle handle), (override));
};

; Test using the mock
TEST(SceneLoaderTests, loads_mesh_from_asset_manager) {
    MockAssetManager mock_assets;

    ; Expect load_mesh to be called and return a test mesh
    Mesh test_mesh;
    EXPECT_CALL(mock_assets, load_mesh("res://models/cube.glb"))
        .Times(1)
        .WillOnce(::testing::Return(&test_mesh));

    ; Code under test
    SceneLoader loader(mock_assets);
    loader.load_scene("res://scenes/test.uzscene");

    ; Verify: mock_assets.load_mesh was called exactly once
}
```

### Common Mock Setup Patterns

```cpp
; Mock returns specific value
EXPECT_CALL(mock, method(arg))
    .Times(1)
    .WillOnce(::testing::Return(value));

; Mock called multiple times
EXPECT_CALL(mock, method())
    .Times(5);  ; Exactly 5 times

; Mock called at least once
EXPECT_CALL(mock, method())
    .Times(::testing::AtLeast(1));

; Mock called, save arguments
float captured_value;
EXPECT_CALL(mock, set_value(::testing::_))
    .WillOnce(::testing::SaveArg<0>(&captured_value));

; Mock throws exception
EXPECT_CALL(mock, risky_method())
    .WillOnce(::testing::Throw(std::runtime_error("Failed")));
```

---

## Test Fixtures (Reusable Test Data)

### GameContext Fixture

For tests that need an engine context:

```cpp
// Mock game context (from src/core/game_context.h)
struct TestGameContext {
    static GameContext create() {
        GameContext ctx;
        ctx.log_fn = [](const char*) { };  ; Silent logger
        ctx.memory = std::make_unique<MemoryArena>(1024 * 1024);
        ctx.asset_manager = nullptr;  ; Set per-test if needed
        return ctx;
    }
};

TEST(SceneManagerTests, scene_manager_creates_with_valid_context) {
    GameContext ctx = TestGameContext::create();
    SceneManager scene_mgr(ctx);
    EXPECT_NE(scene_mgr.get_root(), 0);
}
```

### EnTT Registry Fixture

For ECS tests:

```cpp
class ECSTests : public ::testing::Test {
protected:
    entt::registry registry;

    void SetUp() override {
        registry.clear();
    }

    ; Helper to create an entity with components
    entt::entity create_entity_with_transform(
        const Vec3& pos, const Quaternion& rot, const Vec3& scale) {
        entt::entity e = registry.create();
        auto& transform = registry.emplace<TransformComponent>(e);
        transform.position = pos;
        transform.rotation = rot;
        transform.scale = scale;
        return e;
    }
};

TEST_F(ECSTests, transform_system_updates_positions) {
    auto entity = create_entity_with_transform(
        Vec3(0, 0, 0), Quaternion::identity(), Vec3(1, 1, 1));

    ; Run system
    auto& transform = registry.get<TransformComponent>(entity);
    transform.position[0] += 10.0f;

    ; Verify
    EXPECT_EQ(transform.position[0], 10.0f);
}
```

### Lua Script Fixture

For Lua scripting tests:

```cpp
class LuaTests : public ::testing::Test {
protected:
    lua_State* L;
    GameContext ctx;

    void SetUp() override {
        ctx = TestGameContext::create();
        L = luaL_newstate();  ; Create a fresh Lua state
        luaL_openlibs(L);     ; Open standard libraries
        ctx.lua_state = L;
    }

    void TearDown() override {
        lua_close(L);
    }

    ; Helper to load and call Lua functions
    void load_lua_script(const char* code) {
        luaL_loadstring(L, code);
        lua_pcall(L, 0, 0, 0);
    }

    int call_lua_function(const char* func_name) {
        lua_getglobal(L, func_name);
        return lua_pcall(L, 0, LUA_MULTRET, 0);
    }
};

TEST_F(LuaTests, on_start_hook_initializes_state) {
    load_lua_script(R"(
        local value = 0
        function on_start()
            value = 42
        end
    )");

    call_lua_function("on_start");

    lua_getglobal(L, "value");
    int result = lua_tointeger(L, -1);
    EXPECT_EQ(result, 42);
}
```

---

## Testing Lua Scripts

### Load and Execute Lua

```cpp
#include "lua.hpp"

TEST(LuaTests, script_loads_without_error) {
    lua_State* L = luaL_newstate();

    int result = luaL_loadfile(L, "res://scripts/player.lua");
    EXPECT_EQ(result, LUA_OK) << lua_tostring(L, -1);

    result = lua_pcall(L, 0, 0, 0);
    EXPECT_EQ(result, LUA_OK) << lua_tostring(L, -1);

    lua_close(L);
}
```

### Test Lua Global Variables

```cpp
TEST(LuaTests, script_sets_global_variable) {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    luaL_loadstring(L, "local MY_VAR = 123");
    lua_pcall(L, 0, 0, 0);

    lua_getglobal(L, "MY_VAR");
    EXPECT_EQ(lua_tointeger(L, -1), 123);

    lua_close(L);
}
```

### Test Lua Function Behavior

```cpp
TEST(LuaTests, player_controller_script_hooks) {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    ; Load script
    luaL_loadstring(L, R"(
        local position = {0, 0, 0}

        function on_start()
            position = {1, 2, 3}
        end

        function on_update(dt)
            position[1] = position[1] + dt
        end

        function get_position()
            return position
        end
    )");
    lua_pcall(L, 0, 0, 0);

    ; Call on_start
    lua_getglobal(L, "on_start");
    lua_pcall(L, 0, 0, 0);

    ; Check state after on_start
    lua_getglobal(L, "get_position");
    lua_pcall(L, 0, 3, 0);  ; Returns 3 values (x, y, z)
    EXPECT_EQ(lua_tointeger(L, -3), 1);
    EXPECT_EQ(lua_tointeger(L, -2), 2);
    EXPECT_EQ(lua_tointeger(L, -1), 3);

    ; Call on_update
    lua_getglobal(L, "on_update");
    lua_pushnumber(L, 0.016);  ; 60 FPS = 16ms
    lua_pcall(L, 1, 0, 0);

    ; Check position changed
    lua_getglobal(L, "get_position");
    lua_pcall(L, 0, 3, 0);
    EXPECT_NEAR(lua_tonumber(L, -3), 1.016, 0.001);

    lua_close(L);
}
```

---

## Testing Scene Loading

### Test .uzscene Parsing

```cpp
TEST(SceneLoaderTests, parses_simple_scene_file) {
    ; Create a temporary test scene
    std::string scene_content = R"([uzscene version=1 name="Test"]

[node id=1 type=Node3D name="Root" parent=0]
position = [0.0, 0.0, 0.0]

[node id=2 type=StaticMesh name="Ground" parent=1]
mesh = "res://models/ground.glb"
material = "res://materials/default.uzmat"
)";

    ; Write to temp file
    std::ofstream scene_file("/tmp/test_scene.uzscene");
    scene_file << scene_content;
    scene_file.close();

    ; Load and verify
    GameContext ctx = TestGameContext::create();
    SceneLoader loader(ctx);
    SceneHandle scene = loader.load_scene("file:///tmp/test_scene.uzscene");

    EXPECT_NE(scene, 0);

    ; Verify structure
    auto nodes = loader.get_scene_nodes(scene);
    EXPECT_EQ(nodes.size(), 2);  ; Root + Ground
    EXPECT_EQ(nodes[0].name, "Root");
    EXPECT_EQ(nodes[1].name, "Ground");
}
```

### Test Node Hierarchy

```cpp
TEST(SceneLoaderTests, builds_correct_node_hierarchy) {
    SceneManager scene;
    NodeId root = scene.create_node("Root");
    NodeId child1 = scene.create_child(root, "Child1");
    NodeId child2 = scene.create_child(root, "Child2");
    NodeId grandchild = scene.create_child(child1, "Grandchild");

    ; Verify parent relationships
    EXPECT_EQ(scene.get_parent(child1), root);
    EXPECT_EQ(scene.get_parent(child2), root);
    EXPECT_EQ(scene.get_parent(grandchild), child1);

    ; Verify children count
    EXPECT_EQ(scene.get_children(root).size(), 2);
    EXPECT_EQ(scene.get_children(child1).size(), 1);
}
```

---

## Testing IPC Handlers

### Mock IPC Command

```cpp
TEST(SceneIPCTests, scene_add_node_command) {
    GameContext ctx = TestGameContext::create();
    SceneManager scene(ctx);

    ; Simulate IPC command
    json command = {
        {"command", "scene.addNode"},
        {"type", "StaticMesh"},
        {"name", "TestMesh"},
        {"parent", 1}
    };

    json response = handle_scene_command(scene, command);

    EXPECT_EQ(response["success"], true);
    EXPECT_NE(response["node_id"], nullptr);

    ; Verify node was created
    int node_id = response["node_id"];
    EXPECT_EQ(scene.get_node_name(node_id), "TestMesh");
}
```

---

## Forbidden Test Patterns

### Never write placeholder tests

```cpp
; FORBIDDEN: Placeholder assertion
TEST(SceneTests, bad_placeholder) {
    EXPECT_TRUE(true);  ; This test proves nothing!
}

; CORRECT: Verify actual behavior
TEST(SceneTests, good_real_test) {
    SceneManager scene;
    NodeId node = scene.create_node("Test");
    EXPECT_NE(node, 0);  ; Verifies ID is valid
    EXPECT_EQ(scene.get_node_name(node), "Test");  ; Verifies name set
}
```

### Never skip test cases

```cpp
; FORBIDDEN: Skipped test
TEST(SceneTests, DISABLED_should_handle_error) {
    ; This test is skipped in CI
}

; CORRECT: Implement all test cases
TEST(SceneTests, handles_null_parent) {
    EXPECT_THROW(scene.create_child(0, "Child"), std::invalid_argument);
}
```

### Never use weak assertions

```cpp
; FORBIDDEN: Weak assertion
TEST(SceneTests, bad_weak_assertion) {
    NodeId node = scene.create_node("Test");
    EXPECT_NE(node, nullptr);  ; Only checks not null, not the actual value
}

; CORRECT: Strong assertions
TEST(SceneTests, good_strong_assertions) {
    NodeId node = scene.create_node("Test");
    EXPECT_EQ(node, 1);  ; Checks exact value
    EXPECT_EQ(scene.get_node_name(node), "Test");  ; Checks name
    EXPECT_EQ(scene.get_parent(node), 0);  ; Checks parent is root
}
```

### Never simplify test coverage

```cpp
; FORBIDDEN: Reduced test cases
TEST(PhysicsTests, bad_minimal_test) {
    ; Only tests happy path, missing error cases
    RigidBody body;
    body.set_mass(5.0);
    EXPECT_EQ(body.get_mass(), 5.0);
}

; CORRECT: Full test coverage
TEST(PhysicsTests, good_comprehensive_test) {
    RigidBody body;

    ; Happy path
    body.set_mass(5.0);
    EXPECT_EQ(body.get_mass(), 5.0);

    ; Boundary: zero mass
    EXPECT_THROW(body.set_mass(0.0), std::invalid_argument);

    ; Boundary: negative mass
    EXPECT_THROW(body.set_mass(-1.0), std::invalid_argument);

    ; Boundary: very large mass
    body.set_mass(1e6);
    EXPECT_EQ(body.get_mass(), 1e6);
}
```

---

## Complete Test File Example

File: `tests/unit/scene_test.cpp`

```cpp
#include <gtest/gtest.h>
#include "scene_manager.h"
#include "game_context.h"

class SceneManagerTests : public ::testing::Test {
protected:
    GameContext ctx;
    std::unique_ptr<SceneManager> scene;

    void SetUp() override {
        ; Create test context
        ctx.log_fn = [](const char*) { };  ; Silent
        ctx.memory = std::make_unique<MemoryArena>(1024 * 1024);

        ; Create scene manager
        scene = std::make_unique<SceneManager>(ctx);
    }

    void TearDown() override {
        scene.reset();
    }
};

; Test 1: Creating nodes
TEST_F(SceneManagerTests, create_node_returns_valid_id) {
    NodeId id = scene->create_node("TestNode");
    EXPECT_NE(id, 0);
}

TEST_F(SceneManagerTests, created_node_has_correct_name) {
    NodeId id = scene->create_node("MyNode");
    EXPECT_EQ(scene->get_node_name(id), "MyNode");
}

TEST_F(SceneManagerTests, created_node_has_default_transform) {
    NodeId id = scene->create_node("TestNode");
    Vec3 pos = scene->get_position(id);
    EXPECT_EQ(pos[0], 0.0f);
    EXPECT_EQ(pos[1], 0.0f);
    EXPECT_EQ(pos[2], 0.0f);
}

; Test 2: Parent/child relationships
TEST_F(SceneManagerTests, can_create_child_node) {
    NodeId parent = scene->create_node("Parent");
    NodeId child = scene->create_child(parent, "Child");
    EXPECT_NE(child, 0);
}

TEST_F(SceneManagerTests, child_has_correct_parent) {
    NodeId parent = scene->create_node("Parent");
    NodeId child = scene->create_child(parent, "Child");
    EXPECT_EQ(scene->get_parent(child), parent);
}

TEST_F(SceneManagerTests, parent_has_child_in_list) {
    NodeId parent = scene->create_node("Parent");
    NodeId child1 = scene->create_child(parent, "Child1");
    NodeId child2 = scene->create_child(parent, "Child2");

    auto children = scene->get_children(parent);
    EXPECT_EQ(children.size(), 2);
    EXPECT_TRUE(std::find(children.begin(), children.end(), child1) != children.end());
    EXPECT_TRUE(std::find(children.begin(), children.end(), child2) != children.end());
}

; Test 3: Error cases
TEST_F(SceneManagerTests, creating_child_with_invalid_parent_fails) {
    EXPECT_THROW(
        scene->create_child(9999, "Child"),
        std::invalid_argument
    );
}

TEST_F(SceneManagerTests, deleting_node_removes_all_children) {
    NodeId parent = scene->create_node("Parent");
    NodeId child = scene->create_child(parent, "Child");

    scene->delete_node(parent);

    EXPECT_THROW(
        scene->get_node_name(parent),
        std::invalid_argument
    );
    EXPECT_THROW(
        scene->get_node_name(child),
        std::invalid_argument
    );
}

; Test 4: Transforms
TEST_F(SceneManagerTests, set_position_updates_transform) {
    NodeId node = scene->create_node("TestNode");
    Vec3 new_pos{10.0f, 20.0f, 30.0f};

    scene->set_position(node, new_pos);

    Vec3 pos = scene->get_position(node);
    EXPECT_EQ(pos[0], 10.0f);
    EXPECT_EQ(pos[1], 20.0f);
    EXPECT_EQ(pos[2], 30.0f);
}

TEST_F(SceneManagerTests, set_rotation_updates_transform) {
    NodeId node = scene->create_node("TestNode");
    Quaternion rot = Quaternion::from_euler_angles({0, 90, 0});

    scene->set_rotation(node, rot);

    Quaternion stored = scene->get_rotation(node);
    EXPECT_NEAR(stored.x, rot.x, 0.001f);
    EXPECT_NEAR(stored.y, rot.y, 0.001f);
    EXPECT_NEAR(stored.z, rot.z, 0.001f);
    EXPECT_NEAR(stored.w, rot.w, 0.001f);
}
```

---

## CI Integration

Tests run automatically on every commit:

```yaml
# .github/workflows/test.yml
name: Tests
on: [push, pull_request]

jobs:
  test:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v3
      - name: Configure
        run: cmake -B build --preset test
      - name: Build
        run: cmake --build build
      - name: Test
        run: ctest --output-on-failure
      - name: Coverage
        run: gcovr --xml coverage.xml
      - name: Upload coverage
        uses: codecov/codecov-action@v3
```

If coverage drops below 95%, the build fails and the PR cannot merge.

---

## Performance Testing

### Benchmark Tests

```cpp
#include <benchmark/benchmark.h>

; Measure scene creation time
static void BM_CreateScene(benchmark::State& state) {
    GameContext ctx = TestGameContext::create();
    SceneManager scene(ctx);

    for (auto _ : state) {
        NodeId node = scene.create_node("Test");
        benchmark::DoNotOptimize(node);
    }
}
BENCHMARK(BM_CreateScene);

; Measure transform updates
static void BM_SetTransform(benchmark::State& state) {
    GameContext ctx = TestGameContext::create();
    SceneManager scene(ctx);
    NodeId node = scene.create_node("Test");

    for (auto _ : state) {
        scene.set_position(node, Vec3(1, 2, 3));
    }
}
BENCHMARK(BM_SetTransform);
```

Run benchmarks:

```bash
./build/Release/benchmarks.exe --benchmark_time_unit=ms
```

Output:

```
Benchmark                Time       CPU    Iterations
──────────────────────────────────────────────────────
BM_CreateScene       0.142 ms   0.136 ms    5000000
BM_SetTransform      0.089 ms   0.084 ms    7500000
```

---

## Next Steps

- **Build system**: See [build.md](build.md)
- **Architecture**: See `docs/engine/cpp-architecture.md`
- **Editor**: See [editor-workflow.md](editor-workflow.md)

