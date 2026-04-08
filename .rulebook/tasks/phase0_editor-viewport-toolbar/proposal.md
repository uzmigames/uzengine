# Proposal: Editor Viewport Toolbar (Phase 0)

## Why
UzEngine's editor viewport has no toolbar — all rendering options require CLI flags (`--cvar`, `--view-mode`) or Lua commands. UE5's viewport toolbar provides instant access to view modes, show flags, camera settings, transform tools, and quality presets directly above the 3D viewport. Without a toolbar, artists cannot toggle fog/grid/AO, switch Lit/Unlit/Wireframe, change projection, or adjust quality without restarting. This is the #1 usability blocker for the editor MVP. The toolbar also needs minimal variants for embedded previews (material editor sphere, mesh preview).

## What Changes

### New Systems
1. **ShowFlags** — 64-bit bitfield struct with per-feature toggles (Grid, Fog, Bloom, AO, Shadows, StaticMeshes, Particles, Decals, Translucency, all light types). Polled by each render pass to skip execution.
2. **ViewportToolbarState** — C++ struct holding current toolbar state: transform mode, coordinate space, view mode, show flags, camera preset, quality level. Serialized to/from IPC.
3. **IPC Toolbar Commands** — `toolbar.get_state`, `toolbar.set_view_mode`, `toolbar.set_show_flag`, `toolbar.set_quality`, `toolbar.set_transform_mode`.
4. **Additional DebugViewModes** — Detail Lighting, Lighting Only, Lit Wireframe, Reflections.
5. **Orthographic Camera Presets** — Top/Front/Left/Right/Back/Bottom via CameraManager + hotkeys (ALT+J/K/H).
6. **Toolbar CVar Bridge** — Maps toolbar toggles to existing CVars (`r.Fog`, `r.GTAO`, `r.Bloom`, `r.TAA`, etc.).

### Refactoring
- Each render pass checks relevant ShowFlag bit before executing.
- DebugViewMode enum extended with 4 new entries (appended, non-breaking).

## Impact
- Affected specs: `docs/specs/editor-panels.md`, `docs/specs/debug-profiling.md`
- Affected code: `show_flags.h` (new), `debug_view_mode.h` (extended), `scene_render_frame.cpp`, `frame_lighting.cpp`, all `frame_*.cpp`, IPC handlers
- Breaking change: NO — ShowFlags defaults all-enabled
- User benefit: Instant visual feedback toggles, view mode switching, quality presets from editor
- Enables: VSCode viewport toolbar panel, material editor preview, multi-viewport
