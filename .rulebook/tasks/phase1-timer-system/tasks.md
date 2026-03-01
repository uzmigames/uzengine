## 1. Implementation

- [ ] 1.1 Add `LuaTimer` struct to `src/game/lua_vm.h` — fields: `id`, `remaining_sec`, `interval_sec`, `callback_ref` (LUA_REGISTRYINDEX), `cancelled`
- [ ] 1.2 Add `timers_` (`std::vector<LuaTimer>`) and `next_timer_id_` to `LuaVM` class
- [ ] 1.3 Add `next_frame_callbacks_` (`std::vector<int>`) to `LuaVM` class for `schedule_next_frame` queue
- [ ] 1.4 Implement `LuaVM::drain_timers(float dt)` in `src/game/lua_vm.cpp` — drain next-frame queue, tick all timers, fire callbacks via `lua_pcall`, compact cancelled entries
- [ ] 1.5 Implement `LuaVM::schedule_after(float delay, int callback_ref)` → `uint32_t timer_id`
- [ ] 1.6 Implement `LuaVM::schedule_every(float interval, int callback_ref)` → `uint32_t timer_id`
- [ ] 1.7 Implement `LuaVM::cancel_timer(uint32_t id)` — mark entry as cancelled (compacted lazily)
- [ ] 1.8 Implement `LuaVM::schedule_next_frame(int callback_ref)` — push to `next_frame_callbacks_`
- [ ] 1.9 Implement `LuaVM::clear_timers()` — unref all callback_refs, clear vectors; called from `uz_game_stop`
- [ ] 1.10 Call `lua_vm.drain_timers(dt)` in `uz_game_update` before per-node `on_update` dispatch in `src/game/game.cpp`
- [ ] 1.11 Register `ctx:schedule_after(delay, callback)` in `lua_binding.cpp` — validates args, calls `LuaVM::schedule_after`, returns timer_id integer
- [ ] 1.12 Register `ctx:schedule_every(interval, callback)` in `lua_binding.cpp` — clamp interval to 1/60, calls `LuaVM::schedule_every`, returns timer_id
- [ ] 1.13 Register `ctx:cancel_timer(timer_id)` in `lua_binding.cpp` — no-op on unknown id
- [ ] 1.14 Register `ctx:schedule_next_frame(callback)` in `lua_binding.cpp` — no return value
- [ ] 1.15 Add error handling in `drain_timers`: on `lua_pcall` failure log error with `[lua timer error] timer_id=N — <msg>`, cancel the failing timer, continue
- [ ] 1.16 Clamp `schedule_after` negative delay to 0 in C++ binding layer
- [ ] 1.17 Ensure `drain_timers` handles repeating timers with overshoot absorption: `remaining = interval + remaining` (remaining is negative after firing)

## 2. Testing

- [ ] 2.1 Unit test: `schedule_after(1.0, cb)` — verify callback fires exactly once after 1.0s of simulated delta accumulation
- [ ] 2.2 Unit test: `schedule_after` with negative delay — verify callback fires on next drain (delay clamped to 0)
- [ ] 2.3 Unit test: `schedule_every(0.5, cb)` — simulate 2.0s of deltas, verify callback fires 4 times
- [ ] 2.4 Unit test: `schedule_every` with interval ≤ 0 — verify interval is clamped to 1/60, callback fires every frame
- [ ] 2.5 Unit test: `cancel_timer` before fire — verify callback never fires after cancellation
- [ ] 2.6 Unit test: `cancel_timer` with unknown id — verify no-op, no crash
- [ ] 2.7 Unit test: `cancel_timer` after one-shot has already fired — verify no-op
- [ ] 2.8 Unit test: `schedule_next_frame(cb)` — verify callback fires on the very next `drain_timers` call, before per-node `on_update`
- [ ] 2.9 Unit test: Lua callback throws error — verify error is logged, timer is cancelled, remaining timers continue to fire
- [ ] 2.10 Unit test: `clear_timers()` — verify all pending timers are cancelled, LUA_REGISTRYINDEX refs are released (no ref leak)
- [ ] 2.11 Integration test: Lua script registers `schedule_after(3.0, cb)` in `on_start`, verifies callback fires after 3 seconds of simulated game time
- [ ] 2.12 Integration test: Lua script hot-reload — `on_stop` cancels timer, `on_start` re-registers, verify no double-fire
- [ ] 2.13 Integration test: scene change clears all timers — start timers, unload scene, verify no callbacks fire on next drain
- [ ] 2.14 Integration test: 100 simultaneous repeating timers — verify no frame-time regression above 0.1ms per drain call

## 3. Documentation

- [x] 3.1 `docs/specs/timer-system.md` — full spec (written in planning phase)
- [ ] 3.2 `docs/specs/lua-scripting.md` — add Timer section referencing `ctx:schedule_after` etc. in the Engine API table
- [ ] 3.3 `docs/README.md` — `phase1-timer-system` row added to Rulebook Tasks table (done in planning phase)
- [ ] 3.4 Code comments in `lua_vm.h` — document drain order invariant (next-frame first, then timed, then on_update)
- [ ] 3.5 Code comments in `lua_binding.cpp` — document clamp behavior for negative delay and zero interval
