# Proposal: phase3-seeded-rng

## Why
`std::rand()` is non-deterministic — the same input sequence produces different results across platforms, compiler versions, and between sessions due to implementation-defined behavior and global state. Deterministic simulation is required for replay systems (record seed + inputs, reproduce exact outcome), competitive game fairness (server and clients must agree on random outcomes), and debug reproduction (developer can reproduce exact bug from a crash report containing the session seed). No seeded RNG system currently exists in UzEngine, so games cannot guarantee reproducibility.

## What Changes
- Add `SeededRNG` class using xorshift64 algorithm: fast, period 2^64-1, no heap allocation, fully portable across MSVC/GCC/Clang
- `SeededRNG::next_u64()`, `next_float()` (0–1), `range(min, max)` (inclusive integer), `range_f(min, max)` (float)
- `GameSession` struct holds a `uint64_t seed` set at session start (from user input, server-assigned, or `std::chrono` wall clock as fallback)
- Per-system isolated RNG instances seeded deterministically from `GameSession::seed + system_id` to prevent cross-system ordering dependency
- Replay system records `GameSession::seed` in replay header — loading a replay re-seeds all systems identically
- Lua API: `ctx.rng:next()` → float 0–1, `ctx.rng:range(min, max)` → integer, `ctx.rng:range_f(min, max)` → float, `ctx.rng:seed(n)` → reseeds the Lua-visible RNG instance
- `ctx.rng` is a per-script isolated instance seeded from `GameSession::seed XOR script_hash` to prevent scripts from diverging each other's sequences

## Impact
- Affected specs: `docs/specs/lua-scripting.md`, `docs/specs/save-system.md`
- Affected code: `src/core/seeded_rng.h`, `src/core/seeded_rng.cpp`, `src/core/game_session.h`, `src/lua/lua_rng_api.cpp`, `src/replay/replay_header.cpp`
- Breaking change: NO
- User benefit: Replays reproduce byte-for-byte; competitive games can seed from a server-provided value; bugs with random reproduction steps become reproducible from a crash log
