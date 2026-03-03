## 1. Assert Macros (`src/engine/core/assert.h`)

- [x] 1.1 Define `ASSERT(expr)` — stderr log + `std::abort()` in debug, no-op in `UZ_SHIPPING`
- [x] 1.2 Define `ASSERT_MSG(expr, msg)` — same with custom message
- [x] 1.3 Define `VERIFY(expr)` — always evaluates expression, aborts only in debug
- [x] 1.4 Define `ENSURE(expr)` — logs to stderr on failure, does NOT abort (recoverable)
- [x] 1.5 All macros wrapped in `do-while(0)` for dangling-else safety
- [x] 1.6 `uz::detail::assert_fail` / `assert_fail_msg` in anonymous detail namespace

## 2. Profiling Macros (`src/engine/core/profiler.h`)

- [x] 2.1 `UZ_PROFILE_FUNCTION()` — no-op when `UZ_PROFILING=0`, Tracy `ZoneScoped` when `=1`
- [x] 2.2 `UZ_PROFILE_ZONE(name)` — named zone
- [x] 2.3 `UZ_PROFILE_FRAME_MARK()` — end-of-frame marker
- [x] 2.4 `UZ_PROFILE_VALUE(name, val)` — plot a numeric value
- [x] 2.5 `UZ_PROFILE_MESSAGE(str, len)` — annotate timeline

## 3. Arena Stats (`src/engine/core/arena.h/.cpp`)

- [x] 3.1 `ArenaStats` struct: `used`, `peak`, `capacity`, `name`
- [x] 3.2 `arena_get_stats(Arena*)` function declaration and implementation
- [x] 3.3 `peak_used` field tracked in arena

## 4. Tests

- [x] 4.1 `tests/core/test_assert.cpp` — 30 tests: ASSERT/ASSERT_MSG/VERIFY/ENSURE behavior, EXPECT_DEATH paths, dangling-else safety, ArenaStats
- [x] 4.2 `tests/core/test_profiler.cpp` — 18 tests: all 5 macros compile as no-ops, multiple types, loops, class methods
- [x] 4.3 All 48 new tests pass — 811/811 total after integration

## 5. CMakeLists.txt Integration

- [x] 5.1 `tests/core/test_assert.cpp` added to `UzEngineTests`
- [x] 5.2 `tests/core/test_profiler.cpp` added to `UzEngineTests`
