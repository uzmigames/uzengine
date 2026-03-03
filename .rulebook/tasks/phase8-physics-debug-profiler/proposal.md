# Proposal: phase3-physics-debug-profiler

## Why
There is no visibility into Jolt Physics performance. Broad phase time, narrow phase time, constraint solver iterations, and active body count are all hidden from the developer. Without this data it is impossible to diagnose why physics is consuming 8ms in a specific scene or determine whether sleeping bodies are working correctly. All other engine subsystems expose profiling data through the debug overlay; physics being the exception makes performance investigation a guessing game.

## What Changes
- Read `JPH::PhysicsSystem::GetBodyStats()` and `JPH::PhysicsSystem::GetStats()` each frame to collect: active body count, sleeping body count, constraint count, broad phase step time, narrow phase step time, solver iteration count, and total step time
- Store stats in a ring buffer (128 frames) for graph display
- Add IPC handler `physics.get_stats` returning the latest stats snapshot as JSON for VSCode to consume
- Add `Physics` panel to the VSCode editor sidebar: line graphs for step time (ms), active/sleeping body counts, and a histogram of constraint counts; updates at 10 Hz
- Add `r_debug_physics_bodies` console cvar: when enabled, renders active bodies as green wireframe overlays and sleeping bodies as blue overlays via the DX12 debug pass
- Add `r_debug_physics_contacts` console cvar: renders contact normals as yellow lines at each contact point
- Integrate physics stats into the existing `debug-profiling` frame budget HUD row

## Impact
- Affected specs: `docs/specs/physics-system.md`, `docs/specs/debug-profiling.md`, `docs/specs/editor-panels.md`
- Affected code: `src/physics/physics_system.cpp`, `src/ipc/ipc_handlers.cpp`, `src/editor/debug_overlay.cpp`, `vscode-extension/src/panels/PhysicsPanel.ts` (new)
- Breaking change: NO
- User benefit: Physics performance bottlenecks become immediately visible. A developer can open the Physics panel, scrub through frame history, and pinpoint which scene change caused a step-time spike without resorting to external profilers.
