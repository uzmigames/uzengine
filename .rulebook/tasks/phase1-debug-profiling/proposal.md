# Proposal: phase1-debug-profiling

## Why

UzEngine currently has no assert infrastructure, no profiling instrumentation, and no unit test framework. This means CPU and GPU performance bottlenecks during renderer development are invisible — developers must guess where frame time is spent. Memory errors in the arena allocators go undetected until a crash occurs with no diagnostic information. Engine regressions introduced during the rapid Phase 1 development cycle cannot be caught automatically, and any breakage is only discovered manually.

This task is placed at the start of Phase 1 deliberately, before the PBR renderer and asset pipeline, because every subsequent system depends on it. Adding profiling zones to a renderer that already exists is straightforward; retrofitting a profiling framework into a complex multi-system engine is expensive and disruptive. The same applies to the test framework — establishing it now means every new system from Phase 1 onward ships with tests.

The in-engine debug overlay (rendered to the backbuffer and visible through the JPEG stream in VSCode) gives developers real-time visibility into FPS, frame time history, memory consumption, and draw call counts without needing an external tool. Tracy integration provides the deeper per-zone CPU/GPU breakdown needed to diagnose renderer performance problems as the engine grows.

## What Changes

- Add `ASSERT(cond)`, `ASSERT_MSG(cond, fmt, ...)`, and `VERIFY(cond)` macros in `src/core/assert.h` that invoke `__debugbreak()` in Debug builds and are stripped to no-ops in Release; `VERIFY` retains the expression evaluation in Release
- Integrate Tracy Profiler (via vcpkg `tracy`) and add `UZ_PROFILE_ZONE(name)`, `UZ_PROFILE_FUNCTION()`, and `UZ_PROFILE_FRAME_MARK()` macros; instrument the engine main loop, IPC dispatch, render submit, and JPEG encode paths from day one
- Add Tracy GPU zone support for DX12 using `TracyD3D12Context` so GPU command list recording and execution times appear in the Tracy timeline alongside CPU zones
- Add PIX event marker calls (`PIXBeginEvent`/`PIXEndEvent`) to all major render passes so GPU captures taken in PIX or RenderDoc show labeled regions
- Implement arena high-water mark tracking in `UZ_DEBUG` builds: each `ArenaAllocator` records peak allocation and exposes it via `arena_stats_t` struct
- Build an in-engine debug overlay using ImGui (rendered as a DX12 pass into the backbuffer, visible through the JPEG stream): displays FPS counter, 128-frame time graph, CPU/GPU frame time (ms), per-arena memory bars (used/peak/capacity), draw call count, and triangle count
- Wire F1 key (and `debug.toggle_overlay` IPC command) to show/hide the debug overlay without restarting the engine
- Add IPC commands `debug.get_stats` (returns current frame metrics as JSON), `debug.dump_arena` (returns all arena stats), and `debug.trigger_capture` (initiates a RenderDoc or PIX capture if the tool is attached)
- Integrate doctest framework (header-only, via vcpkg) and register it with CTest; write the initial test suite covering arena allocator correctness, IPC message framing, ring buffer, and scene node hierarchy operations
- Add a `debug.run_tests` IPC command that runs the CTest suite in-process and returns pass/fail results as JSON, enabling AI agents to verify engine health after making changes

## Impact

- Affected specs: `docs/specs/debug-profiling.md`, `docs/specs/memory-system.md`, `docs/specs/renderer.md`
- Affected code: `src/core/assert.h`, `src/core/profiler.h`, `src/core/arena_allocator.cpp`, `src/renderer/debug_overlay.cpp`, `src/renderer/debug_overlay.h`, `src/ipc/debug_handlers.cpp`, `tests/`
- Breaking change: NO
- User benefit: Developers get real-time FPS/memory/draw-call stats in the viewport, Tracy frame captures for performance analysis, and an automated test suite that catches regressions before they ship
