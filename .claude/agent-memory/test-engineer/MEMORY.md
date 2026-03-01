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
