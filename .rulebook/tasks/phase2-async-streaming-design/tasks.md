## 1. Research — Read Existing Specs and Code

- [x] 1.1 Read `docs/specs/asset-pipeline.md` — understand current synchronous load path
- [x] 1.2 Read `docs/specs/scene-system.md` — understand scene load lifecycle
- [x] 1.3 Read `docs/specs/lua-scripting.md` — understand Lua VM update loop and yield points
- [x] 1.4 Read `docs/specs/memory-system.md` — understand arena allocators and lifetime tiers
- [x] 1.5 Survey C++23 coroutine (`co_await`/`co_yield`) patterns for IO integration
- [x] 1.6 Survey LuaJIT coroutine (`lua_resume`/`lua_yield`) for Lua yield-from-C patterns

## 2. Design — IoThreadPool

- [x] 2.1 Define `IoThreadPool` — fixed worker count (`max(2, cpu_cores / 2)`), configurable via `.uzproj`
- [x] 2.2 Define `IoTask` struct: `priority` (High/Normal/Low), `fn` (std::function), `cancel_token`
- [x] 2.3 Define priority queue: three MPSC queues (one per priority), high checked first
- [x] 2.4 Specify `IoThreadPool::submit(IoTask) -> AsyncHandle<T>` — thread-safe submission
- [x] 2.5 Specify `IoThreadPool::shutdown()` — drains in-flight tasks, cancels pending
- [x] 2.6 Specify thread naming convention: `"UzIO-0"` … `"UzIO-N"` for debugger visibility
- [x] 2.7 Specify task cancellation: cooperative via `cancel_token.is_cancelled()` checks inside task fn

## 3. Design — AsyncHandle<T>

- [x] 3.1 Define `AsyncHandle<T>` — thin wrapper over `std::shared_future<T>`
- [x] 3.2 Specify `is_ready() -> bool` — non-blocking poll
- [x] 3.3 Specify `get() -> T` — blocking wait (only safe to call on IO thread or when `is_ready()`)
- [x] 3.4 Specify `cancel()` — signals cancel token; does not block
- [x] 3.5 Specify `then(fn)` — continuation callback on completion, called on main thread via deferred queue
- [x] 3.6 Specify `AsyncHandle<void>` specialization for fire-and-forget tasks
- [x] 3.7 Specify error propagation: failed tasks throw typed exception inside `get()`; `then()` receives `std::exception_ptr`

## 4. Design — C++23 Coroutine Integration

- [x] 4.1 Define `UzTask<T>` coroutine return type — `promise_type`, `final_suspend`, `initial_suspend`
- [x] 4.2 Specify `co_await AsyncHandle<T>` — suspends coroutine, resumes on completion continuation
- [x] 4.3 Specify `co_await UzDelay(ms)` — suspends for N milliseconds without blocking thread
- [x] 4.4 Specify coroutine executor: resumes on main thread (not IO thread) for safety
- [x] 4.5 Specify coroutine lifetime: cancelled if owning system shuts down before completion
- [x] 4.6 Note: C++23 coroutines are engine-only; game.dll uses Lua coroutines instead

## 5. Design — Lua Coroutine Integration

- [x] 5.1 Specify `ctx.coroutine.wait_async(handle)` — calls `lua_yield` with handle; VM scheduler resumes when handle ready
- [x] 5.2 Specify `ctx.coroutine.wait(seconds)` — yields Lua coroutine for N seconds
- [x] 5.3 Specify `ctx.coroutine.wait_frames(n)` — yields for N game frames
- [x] 5.4 Specify `ctx.coroutine.wait_until(fn)` — yields until predicate returns true
- [x] 5.5 Specify `LuaCoroutineScheduler::drain(dt)` — called from `uz_game_update`, advances all pending coroutines
- [x] 5.6 Specify error handling: uncaught Lua error in coroutine → node disabled, error logged, VM continues

## 6. Design — Level Streaming Chunk Protocol

- [x] 6.1 Define `StreamingChunk` descriptor: `chunk_id`, `aabb`, `scene_path`, `load_state` enum
- [x] 6.2 Define `LoadState` enum: `Unloaded`, `Queued`, `Loading`, `Loaded`, `UnloadQueued`, `Unloading`
- [x] 6.3 Specify `CStreamingVolume` trigger component: `enter_radius`, `load_radius`, `chunk_ids[]`
- [x] 6.4 Specify load/unload hysteresis: `load_radius > unload_radius` to prevent thrashing
- [x] 6.5 Specify async load flow: `Unloaded → Queued (IoThreadPool submit) → Loading → Loaded`
- [x] 6.6 Specify placeholder mesh injection: low-res stand-in spawned at `Queued` state, replaced at `Loaded`
- [x] 6.7 Specify main-thread integration: chunk spawn/despawn only on main thread; IO thread provides parsed data

## 7. Design — Memory Budget

- [x] 7.1 Specify streaming memory tier: separate arena pool, configurable size (default 512 MB)
- [x] 7.2 Specify eviction policy: LRU eviction when high-water mark (80%) exceeded
- [x] 7.3 Specify priority for eviction: lowest-priority chunks farthest from camera evicted first
- [x] 7.4 Specify `streaming.budget_exceeded` IPC event for editor warning panel
- [x] 7.5 Specify `AsyncHandle` memory: handle payload stored in streaming arena, released on eviction

## 8. Write Spec

- [x] 8.1 Create `docs/specs/async-streaming.md` — full spec with all sections from steps 2–7
- [x] 8.2 Add sequence diagrams: async load flow, Lua coroutine yield/resume, chunk state machine
- [x] 8.3 Add memory layout diagram: streaming arena with LRU eviction pointer
- [x] 8.4 Update `docs/README.md` — add `async-streaming.md` to specs table
- [x] 8.5 Update `docs/specs/memory-system.md` — add streaming tier section

## 9. Review

- [x] 9.1 Verify `LuaCoroutineScheduler::drain` can safely resume after Lua error isolation
- [x] 9.2 Verify `IoThreadPool::shutdown` is called before DX12 device release to prevent dangling GPU uploads
- [x] 9.3 Verify chunk state machine handles pause (game paused mid-load) without deadlock
- [x] 9.4 Confirm `AsyncHandle<T>` is usable from both game.dll (indirectly via Lua) and engine (C++ directly)
- [x] 9.5 Confirm design works with `phase2-source-asset-db` dependency tracking requirements
