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
- [x] 3.1 Add `TransformMode::Select` (Q) — no gizmo drawn, LMB is pure selection (ray-cast pick). Add to existing `GizmoInteraction` mode enum.
- [x] 3.2 Implement `GizmoCoordSpace` enum (World/Local) toggle — gizmo axes align to world XYZ or selected entity's local orientation. Store in `GizmoRenderer`, apply rotation to gizmo model matrix.
- [x] 3.3 Add `r.Gizmo.Show` CVar — toggles gizmo visibility independently of selection state.
- [x] 3.4 Add `r.Gizmo.Scale` CVar (float, default 1.0) — scales gizmo screen-space size for 4K/small displays.
- [x] 3.5 Hotkey wiring: Q=Select, W=Translate, E=Rotate, R=Scale (already partially wired). Add coordinate space toggle hotkey (` ` backtick or Ctrl+~).
- [x] 3.6 Build + verify: Q hides gizmo and enables selection, W/E/R restore gizmo, coordinate space toggles between world/local axes.

## Session 4: ViewportToolbarState + Runtime Toolbar Renderer
- [x] 4.1 Create `ViewportToolbarState` struct — current TransformMode, GizmoCoordSpace, DebugViewMode, ShowFlags (as uint64), QualityLevel, active camera ID, FOV, near/far plane. Plain C++ struct, no JSON serialization.
- [x] 4.2 Create `ViewportToolbar` class — owns `ViewportToolbarState`, renders a fixed-size button strip into the frame (top of viewport). Uses existing ImGui or the engine's 2D overlay system (same layer as `StatsOverlay`).
- [x] 4.3 Implement toolbar button groups: [Select|Translate|Rotate|Scale] | [Lit|Unlit|Wireframe|LitWireframe|…] | [Show Flags dropdown] | [Quality dropdown]. Buttons call the same C++ APIs already implemented (SetDebugViewMode, SetShowFlag, etc.).
- [x] 4.4 Implement toolbar mouse hit-testing — map forwarded mouse XY (from existing input system) to button rects. LMB click fires the button action directly in C++. No IPC round-trip needed.
- [x] 4.5 Wire `ViewportToolbar` into `SceneRenderer::Render()` as a post-pass overlay (after tonemapping, before swapchain present).
- [x] 4.6 Add `r.Toolbar.Show` CVar (bool, default 1) — toggles toolbar visibility without rebuilding.
- [x] 4.7 Build + verify: toolbar renders over the viewport, buttons respond to mouse clicks, CVar hides/shows it.

## Session 5: Toolbar Profiles (Full vs Minimal)
- [x] 5.1 Define `ToolbarProfile` enum — `Full` (level editor, all button groups), `MaterialPreview` (view mode + lighting buttons only, no transform/show flags), `MeshPreview` (view mode + wireframe + camera, no show flags), `Minimal` (view mode only).
- [x] 5.2 Each profile defines which button groups are visible. `ViewportToolbar::SetProfile(ToolbarProfile)` rebuilds the visible button list. Profile stored in `ViewportToolbarState`.
- [x] 5.3 `MaterialPreview` profile: show only Lit/Unlit/Wireframe view mode buttons + camera orbit indicator. Hide grid, gizmo, show flags, transform tools.
- [x] 5.4 `MeshPreview` profile: show view mode buttons + wireframe overlay toggle + LOD indicator. Hide show flags and transform tools.
- [x] 5.5 Add `r.Toolbar.Profile` CVar (int, default 0=Full) — switches profile at runtime. Build + verify: profile switch correctly hides/shows button groups.

## Session 6: Lua Toolbar API + Game View
- [x] 6.1 Lua API `uz.viewport.set_view_mode(name)` / `uz.viewport.get_view_mode()` — runtime-side binding, wraps `SceneRenderer::SetDebugViewMode()`.
- [x] 6.2 Lua API `uz.viewport.set_show_flag(name, bool)` / `uz.viewport.get_show_flag(name)` — runtime-side binding, wraps `SceneRenderer::SetShowFlag()`.
- [x] 6.3 Lua API `uz.viewport.set_quality(level)` — runtime-side binding, wraps `QualityPresetManager::ApplyPreset()`.
- [x] 6.4 Game View toggle (G key) — hides all editor overlays (grid, gizmo, selection outline, stats overlay, toolbar) while keeping game rendering. Toggle sets r.Show.Grid=0, r.Gizmo.Show=0, r.Toolbar.Show=0.
- [x] 6.5 Build + verify: Lua scripts can toggle view modes and show flags at runtime; G key hides all overlays.

## 1. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 1.1 Update or create documentation covering the implementation
- [x] 1.2 Write tests covering the new behavior
- [x] 1.3 Run tests and confirm they pass
