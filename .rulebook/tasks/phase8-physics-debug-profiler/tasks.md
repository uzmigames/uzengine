## 1. Physics Stats Collection

- [ ] 1.1 Call `JPH::PhysicsSystem::GetBodyStats()` and the Jolt step listener timing APIs each frame in `src/physics/physics_system.cpp` to collect: active body count, sleeping body count, constraint count, broad phase step time (ms), narrow phase step time (ms), solver iteration count, and total step time (ms)
- [ ] 1.2 Define `struct PhysicsFrameStats` in `src/physics/physics_system.h` containing all seven collected fields plus a `frame_index` (uint64_t)
- [ ] 1.3 Implement a ring buffer of 128 `PhysicsFrameStats` entries in `PhysicsSystem`; overwrite oldest entry each frame
- [ ] 1.4 Expose `PhysicsSystem::get_latest_stats() const` returning a `const PhysicsFrameStats&` for the most recently written frame
- [ ] 1.5 Expose `PhysicsSystem::get_stats_history(size_t count) const` returning a `std::span<const PhysicsFrameStats>` for the last N frames (clamped to 128)

## 2. IPC Handler

- [ ] 2.1 Register IPC handler `physics.get_stats` in `src/ipc/ipc_handlers.cpp` returning the latest `PhysicsFrameStats` as JSON: `{ "active_bodies": N, "sleeping_bodies": N, "constraints": N, "broad_ms": f, "narrow_ms": f, "solver_iterations": N, "total_ms": f }`
- [ ] 2.2 Register IPC handler `physics.get_stats_history` accepting `{ "frames": N }` and returning an array of up to N stat snapshots

## 3. VSCode Physics Panel

- [ ] 3.1 Create `vscode-uzengine/src/panels/PhysicsPanel.ts` implementing a WebView panel that polls `physics.get_stats` at 10 Hz via IPC
- [ ] 3.2 Render a line graph for physics step time (ms) over the last 128 frames in the WebView using inline SVG or Canvas
- [ ] 3.3 Render a line graph for active/sleeping body counts over the last 128 frames
- [ ] 3.4 Render a histogram of constraint counts per frame
- [ ] 3.5 Register the PhysicsPanel in `vscode-uzengine/src/extension.ts` with command `uzengine.showPhysicsPanel`

## 4. Debug Body Overlays

- [ ] 4.1 Add `r_debug_physics_bodies` console cvar (bool, default false) to `src/physics/physics_system.cpp`
- [ ] 4.2 When `r_debug_physics_bodies` is enabled, render all active bodies as green wireframe AABBs and all sleeping bodies as blue wireframe AABBs via the DX12 debug pass
- [ ] 4.3 Add `r_debug_physics_contacts` console cvar (bool, default false); when enabled, render contact normals as yellow line segments at each contact point using data from `JPH::ContactListener`

## 5. Frame Budget HUD Integration

- [ ] 5.1 Add a `Physics` row to the existing `debug-profiling` frame budget HUD in `src/editor/debug_overlay.cpp`, showing total physics step time in ms
- [ ] 5.2 Color the Physics row green when `total_ms < 2.0`, yellow when `2.0 <= total_ms < 5.0`, and red when `total_ms >= 5.0`

## 6. Testing

- [ ] 6.1 Create `tests/physics/test_physics_debug_profiler.cpp` testing that `PhysicsFrameStats` is populated with non-zero values after one physics step with at least one active body
- [ ] 6.2 Test ring buffer wraps correctly at index 128: after 129 steps, the oldest entry is overwritten and `get_stats_history(128)` returns the most recent 128 frames
- [ ] 6.3 Test `get_latest_stats()` always returns the most recent frame's data
- [ ] 6.4 Test IPC handler `physics.get_stats` returns valid JSON with all seven required keys
- [ ] 6.5 Test IPC handler `physics.get_stats_history` with `frames=5` returns exactly 5 entries when at least 5 frames have been recorded
- [ ] 6.6 Test `r_debug_physics_bodies` cvar registration: verify the cvar exists and its default value is false
- [ ] 6.7 Test `r_debug_physics_contacts` cvar registration: verify the cvar exists and its default value is false
- [ ] 6.8 Test PhysicsPanel TypeScript: mock the IPC `physics.get_stats` response and verify the panel renders without throwing; verify the WebView content is updated on each poll
- [ ] 6.9 Achieve >= 95% line and branch coverage across `physics_system.cpp` stats collection paths and IPC handlers

## 7. Documentation

- [ ] 7.1 Update `docs/specs/physics-system.md` documenting `PhysicsFrameStats` fields, ring buffer size, and `get_stats_history()` API
- [ ] 7.2 Update `docs/specs/debug-profiling.md` documenting the Physics frame budget HUD row and `r_debug_physics_bodies`/`r_debug_physics_contacts` cvars
- [ ] 7.3 Update `docs/specs/editor-panels.md` documenting the Physics panel, its IPC polling rate, and graph descriptions
