## 1. SeededRNG Core Implementation
- [ ] 1.1 Create `src/core/seeded_rng.h` declaring `SeededRNG` struct with a single `uint64_t state` field and inline methods `next_u64()`, `next_float()`, `range(int min, int max)`, `range_f(float min, float max)`
- [ ] 1.2 Implement `SeededRNG::next_u64()` using the xorshift64 algorithm: `state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state;`
- [ ] 1.3 Implement `SeededRNG::next_float()` returning `(float)(next_u64() >> 11) * (1.0f / (1 << 21))` for a uniform [0, 1) float
- [ ] 1.4 Implement `SeededRNG::range(int min, int max)` returning `min + (int)(next_u64() % ((uint64_t)(max - min + 1)))` for inclusive integer range
- [ ] 1.5 Implement `SeededRNG::range_f(float min, float max)` returning `min + next_float() * (max - min)` for float range
- [ ] 1.6 Create `src/core/seeded_rng.cpp` with a single instantiation of `SeededRNG` — keep the struct header-inline except for any static test helpers

## 2. GameSession Seed Management
- [ ] 2.1 Create `src/core/game_session.h` defining `GameSession` struct with `uint64_t seed`, `uint64_t frame_index`, and `uint32_t session_id` fields
- [ ] 2.2 Implement `GameSession::InitSeed(uint64_t explicit_seed)` that sets `seed = explicit_seed`; if `explicit_seed == 0`, falls back to `std::chrono::high_resolution_clock::now().time_since_epoch().count()` as the seed
- [ ] 2.3 Define system IDs in `src/core/game_session.h` as named constants: `SYSTEM_ID_PHYSICS = 1`, `SYSTEM_ID_SPAWNER = 2`, `SYSTEM_ID_AI = 3`, `SYSTEM_ID_LUA = 4`
- [ ] 2.4 Implement `GameSession::CreateRNG(uint32_t system_id)` returning a `SeededRNG` initialized with `seed ^ ((uint64_t)system_id << 32)` for per-system isolation

## 3. Replay Header Integration
- [ ] 3.1 Add `uint64_t session_seed` field to the replay header struct in `src/replay/replay_header.h`
- [ ] 3.2 Write `session_seed` to the replay header when recording begins in `src/replay/replay_header.cpp`
- [ ] 3.3 Read `session_seed` from the replay header when loading a replay and call `GameSession::InitSeed(session_seed)` before replaying any frames

## 4. Lua API
- [ ] 4.1 Create `src/lua/lua_rng_api.cpp` implementing `ctx.rng` as a per-script `SeededRNG` instance
- [ ] 4.2 Seed each script's `ctx.rng` instance with `GameSession::seed XOR hash(script_path)` to isolate script sequences from each other
- [ ] 4.3 Implement `ctx.rng:next()` Lua binding returning `next_float()` as a Lua number (0–1)
- [ ] 4.4 Implement `ctx.rng:range(min, max)` Lua binding returning `range(min, max)` as a Lua integer
- [ ] 4.5 Implement `ctx.rng:range_f(min, max)` Lua binding returning `range_f(min, max)` as a Lua float
- [ ] 4.6 Implement `ctx.rng:seed(n)` Lua binding that reseeds the script's `SeededRNG::state` to `n`

## 5. Testing
- [ ] 5.1 Create `tests/core/test_seeded_rng.cpp` with Google Test suite `SeededRNGTest`
- [ ] 5.2 Write test `DeterministicSequence`: create two `SeededRNG` instances with the same seed; call `next_u64()` 100 times on each; assert all 100 outputs are identical
- [ ] 5.3 Write test `DifferentSeedsDifferentSequences`: create two instances with different seeds; call `next_u64()` once on each; assert outputs differ
- [ ] 5.4 Write test `NextFloatInRange`: call `next_float()` 10000 times; assert every value is in `[0.0f, 1.0f)`
- [ ] 5.5 Write test `RangeIntBoundsRespected`: call `range(5, 10)` 10000 times; assert every value is in `[5, 10]` inclusive
- [ ] 5.6 Write test `RangeFloatBoundsRespected`: call `range_f(-1.0f, 1.0f)` 10000 times; assert every value is in `[-1.0f, 1.0f]`
- [ ] 5.7 Write test `ZeroSeedFallsBackToClock`: call `GameSession::InitSeed(0)`; assert `session.seed != 0`
- [ ] 5.8 Write test `ExplicitSeedPreserved`: call `GameSession::InitSeed(12345)`; assert `session.seed == 12345`
- [ ] 5.9 Write test `PerSystemRNGIsolation`: create system RNGs for `SYSTEM_ID_PHYSICS` and `SYSTEM_ID_AI` from the same session; call `next_u64()` once on each; assert they produce different values
- [ ] 5.10 Write test `ReplayHeaderRoundtrip`: write a `GameSession::seed` to a replay header, serialize and deserialize it, assert the recovered seed equals the original
- [ ] 5.11 Write test `PeriodNeverZero`: start with seed 1; call `next_u64()` 1000000 times; assert state never becomes 0 (xorshift64 has no zero state in its period)
- [ ] 5.12 Verify C++ line coverage >= 95% and branch coverage >= 95% for `seeded_rng.h` (inline methods) and `game_session.cpp`

## 6. Documentation
- [ ] 6.1 Update `docs/specs/lua-scripting.md` with all `ctx.rng.*` API entries and the per-script isolation model
- [ ] 6.2 Update `docs/specs/save-system.md` with the `GameSession::seed` replay header field and how to reproduce a session from a crash log
