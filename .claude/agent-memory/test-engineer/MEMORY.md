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

## VFX Graph Asset Format tests (runtime/tests/engine/vfx/)
- VFX test files live in `runtime/tests/engine/vfx/` — GLOB_RECURSE auto-discovers them
- Include paths: `"asset/vfx_graph.h"`, `"modules/module_registry.h"` etc. (relative to engine/vfx/)
- CMakeLists: added `engine/vfx/modules`, `engine/vfx/asset`, `engine/vfx/parameters`, `engine/vfx/events` to include dirs
- VFXHotReload: guarded by `#if defined(UZ_DEV_BUILD) || !defined(NDEBUG)` — only active in debug builds
- VFXHotReload tests: use `kNonExistentDir = "__nonexistent_vfx_test_dir_xyz123__"` — no real files needed
- VFXGraph::LoadFromString: returns non-null for valid JSON even when modules/params/events arrays are absent
- VFXParameterStore: SetVec3 stores as Vec4{x,y,z,0} — w is zero; verified via GetVec4
- VFXComposite: returns nullptr for ANY invalid entry (not just skips) — one bad entry kills the whole load
- Validator InvalidType check: "int", "bool", "curve" types fail even though loader parses them (mapping to ParameterType::Int/Bool/Curve)
- Test helper pattern: `ModuleRegistry MakeRegistry()` at top of each file that needs it (not shared fixture)

## VFX Trail system tests (runtime/tests/engine/vfx/test_trail_*.cpp)
- trail_point.h: TrailPointBuffer ring buffer — head=index 0 (newest), tail=index Count-1 (oldest)
- trail_component.h: TrailComponent::Tick(dt, pos) — Tick(dt) no-position overload exists in system.cpp
  but only Tick(dt, pos) is declared in the header; tests use the declared API only
- trail_system.cpp uses a DIFFERENT Build() signature and extended TrailMeshData than the header —
  meshData.vertexCount and meshData.centroidDistanceToCamera fields not in the header's TrailMeshData.
  Tests test against the declared public API in headers, not the implementation's internal API.
- Test helper pattern: `TrailComponent MakeComponent()` + `TrailPointBuffer(tc.maxTrailPoints)` reset
  after setting maxTrailPoints (default ctor sizes buffer to default 100, not the custom value)
- TrailMeshBuilder tests: use kCamera{0,10,0} overhead so ribbon always has non-degenerate orientation
- TrailAsset::Load with nonexistent path returns nullptr (FileReadAllText returns empty string)
- uvScrollRate: negative values accepted (reverse UV scroll direction) — no validation failure
- widthEnd == 0.0 is accepted (tapers to point), widthEnd < 0 is rejected
- sortOrder must be integer; float value ("sort_order": 1.5) returns nullptr
- TrailSystem::GetTotalSegmentCount: segments = max(0, pointCount-1) per trail
- TrailSystem slot reuse: inactive slots are reused by next RegisterTrail before growing vector

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
