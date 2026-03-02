# Proposal: phase0-async-streaming-design

## Why

World streaming, async asset loading, and Lua coroutines all require a non-blocking IO model. Without a shared design, each subsystem will spawn its own threads with no coordination, causing priority inversions, duplicate threadpool implementations, and undefined behavior when the game is paused or stopped while IO is in-flight.

A unified async streaming spec — designed before any subsystem touches threading — defines the IO thread pool contract, the `AsyncHandle<T>` API, coroutine integration points, and the level-streaming chunk protocol. Downstream tasks (`phase3-world-streaming`, `phase1-lua-coroutines`, `phase2-source-asset-db`) reference this spec instead of inventing their own.

## What Changes

- **No code changes** — this is a design-only task.
- **`docs/specs/async-streaming.md`** — new spec defining:
  - `IoThreadPool` — fixed-size thread pool (worker count = CPU cores / 2), priority queue
  - `AsyncHandle<T>` — typed future-like handle; `.is_ready()`, `.get()`, `.cancel()`
  - `co_await` coroutine integration for C++23 (engine-side use only)
  - Lua coroutine integration: `ctx.coroutine.wait_async(handle)` yields until handle ready
  - Level streaming chunk protocol: `StreamingChunk` descriptor, load/unload async state machine
  - Memory budget: streaming allocator with high-water mark and eviction policy

## Impact

- Affected specs: `docs/specs/async-streaming.md` (new)
- Downstream tasks: `phase3-world-streaming`, `phase1-lua-coroutines`, `phase2-source-asset-db`, `phase4-virtual-texturing`
- Breaking change: NO — design only
- User benefit: zero main-thread stall during asset loads; predictable memory budget for streaming
