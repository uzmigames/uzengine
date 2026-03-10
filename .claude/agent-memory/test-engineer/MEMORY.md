# Test Engineer — Persistent Memory

## Project: UzEngine
Google Test + Google Mock. Minimum 95% coverage. No placeholder tests.

## Key Paths
- `tests/unit/` — unit tests (fast, isolated, no hardware)
- `tests/integration/` — integration tests (may use temp files)
- `tests/perf/` — performance benchmarks
- `tests/fixtures/` — small test assets (.uzscene, .glb, .png, .wav)
- `tests/helpers/` — mock_game_context.h, mock_xaudio2.h, etc.

## Test Infrastructure
- Framework: Google Test + Google Mock (via vcpkg)
- Coverage: gcovr or llvm-cov (cmake --preset coverage)
- Build: cmake --preset test && ctest --output-on-failure

## Mock Patterns
- GameContext: make_test_game_context() in tests/helpers/
  - Always zero _reserved[128]
  - log_fn: silent lambda
  - resource callbacks: return {0,0} handles
- XAudio2: MockXAudio2SourceVoice (COM mock)
- EnTT registry: make_test_registry() with minimal node setup
- Lua state: real luaL_newstate() + lua_sandbox_apply() (cheap, no hardware)

## Coverage Minimums
- lua_sandbox: 100% (security-critical)
- script_runner: 95%
- scene_loader: 95%
- save system: 95%
- physics queries: 95%
- audio voice pool: 90%
- asset importers: 90%
- IPC handlers: 90%

## Forbidden Patterns
- EXPECT_TRUE(true) placeholder
- DISABLED_ prefix on real tests
- Catching exceptions silently
- Global state leakage between tests

## Actual Build / Test Commands (Linux devcontainer)
- Configure: `cmake -B build -G "Ninja Multi-Config"` (no toolchain file needed on Linux)
- Build: `cmake --build /workspaces/UzEngine/build --config Debug --parallel`
- Test: `cd /workspaces/UzEngine/build && ctest -C Debug --output-on-failure`
- Test filter: `-R "SceneLoader|SceneSaver"`

## scene_loader / scene_saver patterns (tests/core/test_scene_io.cpp)
- Scene is ~8MB — always `std::make_unique<Scene>()` + `scene_init()` after allocation
- `scene_save_to_string(nullptr)` returns `""` (empty, not an error Result)
- `SceneLoadResult` / `SceneSaveResult` = `Result<bool>`: use `.is_ok()`, `.is_err()`, `.error()`
- Default flags when "active"/"visible" absent: both ON (NODE_FLAG_ACTIVE | NODE_FLAG_VISIBLE)
- Inline default.uzscene as a `constexpr const char*` — avoids CI dependency on assets/ dir
- Orphaned parent test: set `node->alive = false` + decrement `node_count` directly
  (do NOT use scene_destroy_node — it cascades and kills the child too)
- All 9 NodeTypes + unknown string → Empty fallback confirmed by node_type_from_string
- Float round-trip tolerance: 1e-4f (saver uses %.6f format)
