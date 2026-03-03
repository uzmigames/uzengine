# Proposal: phase1-lua-coroutines

## Why

Lua scripts that need to "wait" for something — a timer, a condition, another async operation — currently must implement hand-rolled state machines in `on_update`. This produces verbose, error-prone code and makes sequential game logic (cutscenes, tutorial sequences, NPC routines) nearly unreadable.

LuaJIT natively supports coroutines (`coroutine.create`/`resume`/`yield`). By integrating a `LuaCoroutineScheduler` into the game update loop, scripts can write sequential logic with `ctx.coroutine.wait(seconds)` / `wait_frames(n)` / `wait_until(fn)` / `wait_async(handle)`, yielding control and resuming automatically. This eliminates state machine boilerplate and enables readable cutscene/tutorial scripts.

## What Changes

- **`src/game/lua_coroutine_scheduler.h/cpp`** — `LuaCoroutineScheduler`: registry of pending coroutines, drain loop
- **`src/engine/scripting/lua_binding.cpp`** — extend `ctx` metatable with `ctx.coroutine.*` API
- **`uz_game_update`** — calls `LuaCoroutineScheduler::drain(dt)` each frame
- **`uz_game_stop`** — calls `LuaCoroutineScheduler::cancel_all()` to prevent resume-after-free
- **Tests** — wait/resume correctness, error isolation, cancel behavior

## Impact

- Affected specs: `docs/specs/lua-scripting.md` (coroutine section), `docs/specs/async-streaming.md` (`wait_async` hook)
- Dependencies: `phase1-lua-scripting` (LuaVM must exist), `phase0-async-streaming-design` (for `wait_async` contract)
- Breaking change: NO — additive API; existing `on_update` scripts unaffected
- User benefit: sequential game logic without state machines; cutscenes and tutorials become straightforward Lua coroutines
