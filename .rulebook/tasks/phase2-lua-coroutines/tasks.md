## 1. Prerequisites

- [x] 1.1 Confirm `phase1-lua-scripting` is complete — `LuaVM` exists, `on_update` dispatch works
- [x] 1.2 Read `docs/specs/async-streaming.md` — understand `wait_async(handle)` contract
- [x] 1.3 Read LuaJIT coroutine API documentation — `coroutine.create`, `lua_resume`, `lua_yield`, `lua_status`

## 2. LuaCoroutineScheduler — Core

- [x] 2.1 Create `src/game/lua_coroutine_scheduler.h` — `LuaCoroutineScheduler` class declaration
- [x] 2.2 Create `src/game/lua_coroutine_scheduler.cpp` — implementation
- [x] 2.3 Define `CoroutineEntry` struct: `lua_State* thread`, `WaitCondition` variant, `node_id`
- [x] 2.4 Define `WaitCondition` variant: `WaitSeconds{float remaining}`, `WaitFrames{int remaining}`, `WaitUntil{int lua_fn_ref}`, `WaitAsync{AsyncHandle<void>}`
- [x] 2.5 Implement `LuaCoroutineScheduler::spawn(lua_State* thread, node_id) -> CoroutineHandle` — registers entry
- [x] 2.6 Implement `LuaCoroutineScheduler::drain(float dt)` — iterates entries, advances conditions, resumes ready coroutines via `lua_resume`
- [x] 2.7 Implement `LuaCoroutineScheduler::cancel(CoroutineHandle)` — removes entry, does not resume
- [x] 2.8 Implement `LuaCoroutineScheduler::cancel_all()` — removes all entries (called on scene stop)
- [x] 2.9 Implement `LuaCoroutineScheduler::cancel_by_node(node_id)` — removes all coroutines for a node (called on node disable/destroy)

## 3. Wait Condition Implementations

- [x] 3.1 Implement `WaitSeconds` advancement: `remaining -= dt`; resume when `remaining <= 0`
- [x] 3.2 Implement `WaitFrames` advancement: `remaining -= 1` per drain call; resume when `remaining <= 0`
- [x] 3.3 Implement `WaitUntil` evaluation: call Lua function ref via `lua_rawgeti`; resume if returns truthy; protect with `lua_pcall`
- [x] 3.4 Implement `WaitAsync` check: `handle.is_ready()`; resume when ready; push result value onto coroutine stack before resume

## 4. Lua API — ctx.coroutine

- [x] 4.1 Register `ctx.coroutine` table in `lua_binding.cpp` during `ctx` metatable setup
- [x] 4.2 Implement `ctx.coroutine.wait(seconds)` — creates `coroutine.running()` entry with `WaitSeconds`, calls `lua_yield`
- [x] 4.3 Implement `ctx.coroutine.wait_frames(n)` — creates entry with `WaitFrames`, calls `lua_yield`
- [x] 4.4 Implement `ctx.coroutine.wait_until(fn)` — stores function reference, creates `WaitUntil` entry, calls `lua_yield`
- [x] 4.5 Implement `ctx.coroutine.wait_async(handle)` — wraps C++ `AsyncHandle` userdata, creates `WaitAsync` entry, calls `lua_yield`
- [x] 4.6 Implement `ctx.coroutine.spawn(fn)` — creates new `lua_State` thread via `lua_newthread`, calls `fn` as coroutine, registers with scheduler
- [x] 4.7 Implement `ctx.coroutine.cancel(handle)` — cancels a spawned coroutine by handle

## 5. Error Isolation

- [x] 5.1 Wrap `lua_resume` in error check — `LUA_ERRRUN` → log error with node_id + traceback, remove entry
- [x] 5.2 Ensure coroutine error does not propagate to `drain()` caller
- [x] 5.3 Disable node on coroutine error (consistent with `on_update` error behavior)
- [x] 5.4 Expose coroutine error to `lua.get_errors` IPC handler

## 6. Integration with uz_game_update/stop

- [x] 6.1 Update `uz_game_update` — call `LuaCoroutineScheduler::drain(dt)` after `on_update` dispatch
- [x] 6.2 Update `uz_game_stop` — call `LuaCoroutineScheduler::cancel_all()` before VM close
- [x] 6.3 Update `LuaVM::destroy` — ensure scheduler is destroyed before `lua_close`

## 7. Testing

- [x] 7.1 Unit test: `wait(0.5)` resumes after 500ms of simulated `drain(dt)` calls
- [x] 7.2 Unit test: `wait_frames(3)` resumes after exactly 3 drain calls
- [x] 7.3 Unit test: `wait_until(fn)` resumes on first frame where fn returns true
- [x] 7.4 Unit test: `cancel_by_node` removes exactly the coroutines for that node
- [x] 7.5 Unit test: runtime error inside coroutine does not crash drain loop
- [x] 7.6 Integration test: Lua script uses `ctx.coroutine.wait(1.0)` and logs message after 1 second
- [x] 7.7 Integration test: `ctx.coroutine.spawn(fn)` creates independent coroutine that runs concurrently with `on_update`
- [x] 7.8 Integration test: `cancel_all()` on scene stop does not resume any coroutine

## 8. Documentation

- [x] 8.1 Update `docs/specs/lua-scripting.md` — add "Coroutines" section with `ctx.coroutine.*` API table
- [x] 8.2 Add cutscene pattern example: sequential dialogue + camera move using `wait()` chaining
- [x] 8.3 Update `docs/modules/game/README.md` — document `LuaCoroutineScheduler`
