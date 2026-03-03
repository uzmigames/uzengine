# Proposal: phase2-memory-profiler

## Why
The engine uses arena allocators (frame, level, permanent) but provides no instrumentation for how much memory each arena is consuming. A frame arena that grows unbounded will cause a crash after thousands of frames; a leaking level arena will crash on the second level load. GPU VRAM exhaustion produces a silent DX12 device removal with no diagnostic information. Without visibility, these bugs are diagnosed only after they cause crashes in production, which is unacceptable for a shipping engine.

Memory bugs in game engines are notoriously difficult to reproduce and diagnose. Heap fragmentation from a subsystem allocating variable-size chunks can silently degrade performance for hundreds of frames before becoming a crash. The only way to shift these bugs from production to development is to make memory costs continuously visible to every developer working on every system. An in-editor memory panel that updates every frame is the baseline requirement for a professional development environment.

GPU VRAM pressure deserves equal treatment. DX12 introduces overcommit semantics — the OS will page VRAM to system RAM rather than fail an allocation — so running out of VRAM manifests as a severe performance cliff rather than a clean crash. Without per-resource tracking developers cannot identify which render target or texture atlas is responsible, and the engine cannot emit early warnings before the cliff is reached.

## What Changes
- Instrument each `Arena` type in `src/memory/` to track `current_used`, `peak_used`, `total_alloc_count`, and `reset_count` using atomic counters with zero overhead when profiling is disabled (`#ifdef UZ_MEMORY_PROFILING`)
- Register each arena with a global `MemoryProfiler` singleton (`src/memory/memory_profiler.h`) at construction time, storing a name string, category tag (`CPU_FRAME`, `CPU_LEVEL`, `CPU_PERMANENT`, `GPU`), and pointer to its counters
- Integrate with Tracy's memory zone API (`TracyAllocN`/`TracyFreeN`) so Tracy captures show named memory zones in the memory graph view with call stacks attributed to the correct arena
- Add GPU VRAM tracking by querying `IDXGIAdapter3::QueryVideoMemoryInfo` each frame for `Budget`, `CurrentUsage`, and `AvailableForReservation`; track per-heap-type (local vs non-local) and expose utilization percentage
- Add per-resource VRAM tracking by wrapping `ID3D12Resource` creation in `src/rhi/rhi_resource.cpp` and recording the resource name, size, and heap type in a `GpuResourceRegistry`
- Add IPC handler `engine.get_memory_stats` returning a JSON object with per-arena stats, GPU VRAM stats, top-10 VRAM consumers by size, and per-frame delta to detect leaks across frames
- Add leak detection heuristic: if a level arena's `current_used` after reset is non-zero, emit a console error with the arena name and bytes leaked
- Add a VSCode extension "Memory" panel that polls `engine.get_memory_stats` at 4 Hz and renders a stacked bar chart for CPU arenas and a separate VRAM pie chart using the existing webview infrastructure
- Add threshold CVars: `mem.frame_arena_warn_mb` and `mem.vram_warn_pct` that trigger visible overlay warnings in the editor when thresholds are exceeded
- Write unit tests in `tests/memory/memory_profiler_test.cpp` covering counter accuracy, thread-safety of atomic increments, and leak detection triggering

## Impact
- Affected specs: `docs/specs/memory-system.md`, `docs/specs/debug-profiling.md`, `docs/specs/editor-panels.md`
- Affected code: `src/memory/arena.h`, `src/memory/arena.cpp`, `src/memory/memory_profiler.h`, `src/memory/memory_profiler.cpp`, `src/rhi/rhi_resource.cpp`, `src/ipc/ipc_handlers.cpp`, `vscode-extension/src/panels/MemoryPanel.ts`
- Breaking change: NO — instrumentation is gated behind `UZ_MEMORY_PROFILING` and zero-cost in release builds
- User benefit: Developers can detect frame arena growth, level arena leaks, and VRAM exhaustion in the editor before they become crashes; Tracy captures include named memory zones with call stacks; VRAM budget warnings prevent silent performance cliffs on low-VRAM GPUs
