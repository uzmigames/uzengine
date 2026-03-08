## 1. Arena Instrumentation

- [x] 1.1 Add `#ifdef UZ_MEMORY_PROFILING` guarded fields to `src/engine/core/arena.h`: `std::atomic<size_t> current_used`, `std::atomic<size_t> peak_used`, `std::atomic<uint64_t> total_alloc_count`, and `std::atomic<uint32_t> reset_count` — zero cost when profiling is disabled
- [x] 1.2 Instrument `Arena::alloc()` in `src/engine/core/arena.cpp` to atomically increment `current_used` by the requested size and `total_alloc_count` by 1; update `peak_used` via a CAS loop if `current_used` exceeds the recorded peak
- [x] 1.3 Instrument `Arena::reset()` in `src/engine/core/arena.cpp` to atomically set `current_used` to 0 and increment `reset_count` by 1; emit `UZ_LOG_ERROR` if `current_used` is non-zero after reset (indicating a logic error — arenas should only reset when empty or intentionally flushed)
- [x] 1.4 Integrate with Tracy's memory zone API: call `TracyAllocN(ptr, size, arena_name)` in `Arena::alloc()` and `TracyFreeN(ptr, arena_name)` in `Arena::free()` when `TRACY_ENABLE` and `UZ_MEMORY_PROFILING` are both defined

## 2. MemoryProfiler Singleton

- [x] 2.1 Create `src/engine/core/memory_profiler.h` defining `MemoryProfiler` singleton with `Register(arena, name, category)`, `Unregister(arena)`, `GetSnapshot()` returning a `MemorySnapshot` struct, and `DetectLeaks()` checking all arenas for post-reset non-zero usage
- [x] 2.2 Define `ArenaCategory` enum in `src/engine/core/memory_profiler.h`: `CPU_FRAME`, `CPU_LEVEL`, `CPU_PERMANENT`, `GPU`
- [x] 2.3 Define `ArenaStats` struct with fields: `name` (string), `category` (ArenaCategory), `current_used_bytes`, `peak_used_bytes`, `total_alloc_count`, `reset_count`
- [x] 2.4 Define `MemorySnapshot` struct with fields: a `std::vector<ArenaStats>` for all registered arenas, `gpu_local_budget_bytes`, `gpu_local_used_bytes`, `gpu_nonlocal_budget_bytes`, `gpu_nonlocal_used_bytes`, and `top_vram_consumers` (vector of name + size pairs, top 10 by size)
- [x] 2.5 Implement `MemoryProfiler` in `src/engine/core/memory_profiler.cpp`; use a `std::mutex`-protected `std::vector` for the registry to support concurrent arena registration from multiple threads at startup

## 3. GPU VRAM Tracking

- [x] 3.1 Add `GpuMemoryTracker` in `src/engine/renderer/gpu_memory_tracker.h` that calls `IDXGIAdapter3::QueryVideoMemoryInfo` each frame for both `DXGI_MEMORY_SEGMENT_GROUP_LOCAL` and `DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL`, storing budget and current usage
- [x] 3.2 Register a GPU pseudo-arena with `MemoryProfiler` at renderer init time, updating its `current_used` from the DXGI local memory query each frame
- [x] 3.3 Create `GpuResourceRegistry` in `src/engine/renderer/gpu_resource_registry.h` with `Register(name, size_bytes, heap_type)` and `Unregister(name)` methods; wrap `ID3D12Resource` creation in `src/rhi/rhi_resource.cpp` to call `Register` on allocation and `Unregister` on release
- [x] 3.4 Implement `GpuResourceRegistry::GetTopConsumers(n)` returning the N largest registered resources by size; feed this into `MemorySnapshot::top_vram_consumers`

## 4. Leak Detection

- [x] 4.1 Implement `MemoryProfiler::DetectLeaks()` in `src/engine/core/memory_profiler.cpp`: iterate all registered arenas with category `CPU_LEVEL`; if `current_used > 0` after a level unload event, emit `UZ_LOG_ERROR("Arena '%s' leaked %zu bytes after reset", name, current_used)`
- [x] 4.2 Subscribe to the level unload event via `EventBus` in `src/engine/core/memory_profiler.cpp` to automatically call `DetectLeaks()` after each level arena reset

## 5. IPC Handler

- [x] 5.1 Add `engine.get_memory_stats` IPC handler in `src/ipc/ipc_handlers.cpp` that calls `MemoryProfiler::GetSnapshot()` and serializes `MemorySnapshot` to JSON with per-arena stats, GPU VRAM stats, top-10 VRAM consumers, and per-frame delta (current minus previous snapshot's `current_used`)
- [x] 5.2 Compute per-frame delta in the IPC handler by caching the previous `MemorySnapshot` in a static local variable and subtracting `current_used` values; a positive delta on a frame arena after frame end indicates unbounded growth

## 6. Threshold CVars

- [x] 6.1 Register CVar `mem.frame_arena_warn_mb` (float, default `64.0`) that triggers `UZ_LOG_WARN` when the frame arena's `current_used` exceeds the threshold at any point during a frame
- [x] 6.2 Register CVar `mem.vram_warn_pct` (float, default `90.0`) that triggers `UZ_LOG_WARN` when GPU local memory utilisation (`current_used / budget`) exceeds the threshold
- [x] 6.3 Check both thresholds in `MemoryProfiler::Tick()` called once per frame from the engine main loop; emit warnings at most once per second to avoid log spam

## 7. VSCode Extension Memory Panel

- [x] 7.1 Create `vscode-uzengine/src/panels/MemoryPanel.ts` implementing a WebView panel that polls `engine.get_memory_stats` at 4 Hz using `setInterval`
- [x] 7.2 Render a stacked bar chart for CPU arenas (frame, level, permanent) in the WebView HTML using inline SVG; each bar segment is color-coded by category and shows `current_used / peak_used` as a fill ratio
- [x] 7.3 Render a VRAM pie chart showing GPU local memory breakdown: top-10 named consumers each get a segment; remaining usage is grouped as "Other"
- [x] 7.4 Add a "Leaks" section in the panel that highlights any arena with non-zero `current_used` post-reset in red

## 8. Testing

- [x] 8.1 Create `tests/core/test_memory_profiler.cpp` with fixture that constructs an isolated `MemoryProfiler` instance (not the global singleton)
- [x] 8.2 Test `Arena::alloc()` increments `current_used` by the allocated size and `total_alloc_count` by 1
- [x] 8.3 Test `peak_used` is updated when `current_used` exceeds the previous peak and NOT updated when it does not
- [x] 8.4 Test `Arena::reset()` sets `current_used` to 0 and increments `reset_count`
- [x] 8.5 Test that atomic counters are correct under concurrent access: spawn 8 threads each calling `alloc()` 1000 times and verify `total_alloc_count == 8000` and `current_used == 8000 * alloc_size`
- [x] 8.6 Test `MemoryProfiler::Register()` stores the arena under the correct name and category
- [x] 8.7 Test `MemoryProfiler::GetSnapshot()` returns correct `current_used_bytes` matching the sum of outstanding arena allocations
- [x] 8.8 Test leak detection: allocate 100 bytes in a `CPU_LEVEL` arena, call `reset()` without freeing, simulate a level unload event, and verify `DetectLeaks()` emits the error log
- [x] 8.9 Test `mem.frame_arena_warn_mb` threshold: allocate `threshold + 1` MB and verify the warning log is emitted; allocate `threshold - 1` MB and verify no warning
- [x] 8.10 Test `engine.get_memory_stats` IPC handler returns valid JSON with `arenas`, `gpu`, and `top_consumers` keys
- [x] 8.11 Test per-frame delta in IPC: call the handler twice with a 100-byte allocation between calls; verify the second response shows delta of 100 bytes for the affected arena
- [x] 8.12 Run coverage -- all new source files in `src/engine/core/` and `src/engine/renderer/` must reach >= 95% line and branch coverage
