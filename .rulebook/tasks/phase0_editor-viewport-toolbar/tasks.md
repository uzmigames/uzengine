## Session 1: ShowFlags Architecture
- [x] 1.1 Create `show_flags.h` — `ShowFlags` struct with 64-bit bitfield. Flags: Grid, Fog, Bloom, AO, Shadows, ContactShadows, SSR, SSGI, StaticMeshes, SkeletalMeshes, Particles, Decals, Translucency, Atmosphere, Clouds, PointLights, SpotLights, DirectionalLight, PostProcess, AntiAliasing, MotionBlur, DOF, LensFlare, Volumetrics. Default: all enabled.
- [x] 1.2 Add `ShowFlags m_showFlags` to `SceneRenderer`. Add `GetShowFlags()`/`SetShowFlag(flag, enabled)` API.
- [x] 1.3 Wire ShowFlags into render passes: `RegisterGTAOPass` checks `ShowFlags::AO`, `RegisterAtmosphereFogCloudPasses` checks `ShowFlags::Fog`/`ShowFlags::Clouds`/`ShowFlags::Atmosphere`, `EditorGrid::Draw` checks `ShowFlags::Grid`, `RegisterSSRPasses` checks `ShowFlags::SSR`, bloom pass checks `ShowFlags::Bloom`, etc.
- [x] 1.4 Add `r.Show.*` CVars that mirror ShowFlags — `r.Show.Grid`, `r.Show.Fog`, `r.Show.Bloom`, `r.Show.AO`, `r.Show.Shadows`, `r.Show.Particles`, `r.Show.Decals`. CVar changes update ShowFlags, ShowFlag changes update CVars.
- [x] 1.5 Build + verify: toggle `--cvar r.Show.Fog=0` disables fog, `r.Show.Grid=0` hides editor grid, existing rendering unchanged with defaults.

## Session 2: Extended View Modes + Orthographic
- [x] 2.1 Add `DebugViewMode::DetailLighting` (ALT+5) — directional light × NdotL only, no albedo, no IBL. Shows pure lighting contribution.
- [x] 2.2 Add `DebugViewMode::LightingOnly` (ALT+6) — full lighting (direct + indirect + IBL) without albedo multiply. Shows light intensity distribution.
- [x] 2.3 Add `DebugViewMode::LitWireframe` — normal Lit rendering with wireframe overlay (dual-pass: lit + wireframe blend).
- [x] 2.4 Add `DebugViewMode::Reflections` — shows only SSR + IBL specular contribution (isolates reflection quality).
- [x] 2.5 Orthographic camera presets: implement `SetOrthographicView(OrthoView::Top/Front/Left/Right/Back/Bottom)` in CameraManager. Each preset sets position + orientation + orthographic projection. Wire hotkeys ALT+J/K/H/G and ALT+SHIFT variants.
- [x] 2.6 Build + verify: `--view-mode detaillighting` shows lighting only, `--view-mode litwireframe` shows overlay, ortho views work from CLI.

## Session 3: Transform Tools + Coordinate Space
- [ ] 3.1 Add `TransformMode::Select` (Q) — no gizmo drawn, LMB is pure selection (ray-cast pick). Add to existing `GizmoInteraction` mode enum.
- [ ] 3.2 Implement `GizmoCoordSpace` enum (World/Local) toggle — gizmo axes align to world XYZ or selected entity's local orientation. Store in `GizmoRenderer`, apply rotation to gizmo model matrix.
- [ ] 3.3 Add `r.Gizmo.Show` CVar — toggles gizmo visibility independently of selection state.
- [ ] 3.4 Add `r.Gizmo.Scale` CVar (float, default 1.0) — scales gizmo screen-space size for 4K/small displays.
- [ ] 3.5 Hotkey wiring: Q=Select, W=Translate, E=Rotate, R=Scale (already partially wired). Add coordinate space toggle hotkey (` ` backtick or Ctrl+~).
- [ ] 3.6 Build + verify: Q hides gizmo and enables selection, W/E/R restore gizmo, coordinate space toggles between world/local axes.

## Session 4: ViewportToolbarState + IPC
- [ ] 4.1 Create `ViewportToolbarState` struct — current TransformMode, GizmoCoordSpace, DebugViewMode, ShowFlags (as uint64), QualityLevel, active camera ID, FOV, near/far plane. Serialize to JSON.
- [ ] 4.2 Add IPC command `toolbar.get_state` — runtime sends full ViewportToolbarState JSON to VSCode extension.
- [ ] 4.3 Add IPC command `toolbar.set_view_mode` — accepts view mode name string, calls `SetDebugViewMode()`.
- [ ] 4.4 Add IPC command `toolbar.set_show_flag` — accepts flag name + bool, calls `SetShowFlag()`.
- [ ] 4.5 Add IPC command `toolbar.set_quality` — accepts quality level name, calls `QualityPresetManager::ApplyPreset()`.
- [ ] 4.6 Add IPC command `toolbar.set_transform_mode` — accepts mode name (select/translate/rotate/scale).
- [ ] 4.7 Build + verify: send IPC commands from test, verify state changes in runtime.

## Session 5: Toolbar Profiles (Full vs Minimal)
- [ ] 5.1 Define `ToolbarProfile` enum — `Full` (level editor, all menus), `MaterialPreview` (view mode + lighting only, no transform/show flags), `MeshPreview` (view mode + wireframe + camera, no show flags), `Minimal` (view mode only).
- [ ] 5.2 Each profile defines which toolbar sections are visible/available. `ViewportToolbarState` includes active profile. IPC `toolbar.set_profile` switches between profiles.
- [ ] 5.3 Material editor preview: `MaterialPreview` profile exposes only Lit/Unlit/Wireframe view modes + lighting model + camera orbit. No grid, no gizmo, no show flags.
- [ ] 5.4 Mesh preview: `MeshPreview` profile exposes view modes + wireframe overlay + LOD visualization + camera orbit.
- [ ] 5.5 Build + verify: switching profiles via IPC correctly limits available toolbar options.

## Session 6: Lua Toolbar API + Game View
- [ ] 6.1 Lua API `uz.viewport.set_view_mode(name)` / `uz.viewport.get_view_mode()` — wraps DebugViewMode.
- [ ] 6.2 Lua API `uz.viewport.set_show_flag(name, bool)` / `uz.viewport.get_show_flag(name)` — wraps ShowFlags.
- [ ] 6.3 Lua API `uz.viewport.set_quality(level)` — wraps QualityPresetManager.
- [ ] 6.4 Game View toggle (G key) — hides all editor overlays (grid, gizmo, selection outline, stats overlay) while keeping game rendering. Toggle sets ShowFlags::Grid=false, ShowFlags::EditorOverlays=false.
- [ ] 6.5 Build + verify: Lua scripts can toggle view modes and show flags at runtime.

## 1. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 1.1 Update or create documentation covering the implementation
- [ ] 1.2 Write tests covering the new behavior
- [ ] 1.3 Run tests and confirm they pass
