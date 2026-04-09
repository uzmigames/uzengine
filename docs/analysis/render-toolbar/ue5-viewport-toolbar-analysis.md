<!-- doc-meta
title: UE5 Viewport Toolbar — Complete Function Analysis
subsystem: editor/viewport
date: 2026-04-02
author: unreal-architect
-->

# UE5 Viewport Toolbar — Complete Function Analysis

Comprehensive audit of every item in the UE5 Level Editor viewport toolbar.
For each item: what it does, which CVar/setting it maps to, UzEngine equivalence status, and implementation priority.

Priority key:
- **P0** — Critical for editor MVP (must ship in Phase 1 viewport milestone)
- **P1** — Important for workflow (Phase 2 polish)
- **P2** — Nice-to-have / power-user feature (Phase 3+)

---

## Menu 1: Transform Tools (leftmost dropdown)

### Transform Mode Selection

| Item | Hotkey | What it does | UE5 CVar/Setting | UzEngine Status | Priority |
|------|--------|-------------|-----------------|-----------------|----------|
| Select Mode | Q | Switches to rectangle-select / click-select. No gizmo drawn. Mouse LMB selects actors. | `EditorViewportClient::SetWidgetMode(WM_None)` | **MISSING** — UzEngine gizmo always shows, no dedicated select mode | P0 |
| Translate Mode | W | Shows 3-axis arrows + plane handles gizmo. Drag moves selection. | `SetWidgetMode(WM_Translate)` | **EXISTS** — `GizmoInteraction::AxisX/Y/Z` translate, plane handles too | P0 |
| Rotate Mode | E | Shows 3-ring rotation gizmo (pitch/yaw/roll rings). | `SetWidgetMode(WM_Rotate)` | **EXISTS** — `GizmoInteraction` rotate mode | P0 |
| Scale Mode | R | Shows 3-axis scale handles + center uniform scale cube. | `SetWidgetMode(WM_Scale)` | **EXISTS** — `GizmoInteraction` scale mode | P0 |

**Gap**: UzEngine lacks a "select-only" mode (Q) where no gizmo renders and LMB is pure selection.

### Coordinate System Submenu

| Item | What it does | UE5 Mapping | UzEngine Status | Priority |
|------|-------------|-------------|-----------------|----------|
| World | Gizmo axes align to world XYZ (absolute). | `CoordSystem = COORD_World` | **MISSING** — no coordinate space toggle | P0 |
| Local | Gizmo axes align to selected object's local orientation. | `CoordSystem = COORD_Local` | **MISSING** | P0 |

**Gap**: `GizmoInteraction` always uses world space. Need a `GizmoCoordSpace` enum + toggle per viewport.

### Gizmo Section

| Item | What it does | UE5 Mapping | UzEngine Status | Priority |
|------|-------------|-------------|-----------------|----------|
| Show Transform Gizmo | Toggles gizmo visibility. When off, gizmo is hidden but selection still works. | `bShowWidget` bool on viewport client | **MISSING** — gizmo is always drawn when entity selected | P1 |
| Gizmo Scale slider | Scales the gizmo size in screen space (default 1.0). Useful on 4K or small screens. | `GizmoScaleFactor` float, stored in editor prefs | **MISSING** — gizmo has fixed screen-space size | P1 |

### Selection Behavior

| Item | Hotkey | What it does | UE5 Mapping | UzEngine Status | Priority |
|------|--------|-------------|-------------|-----------------|----------|
| Preserve Non-Uniform Scale | — | When scaling, preserves existing non-uniform scale ratios during drag. | Editor pref `bPreserveNonUniformScale` | **MISSING** | P2 |
| Enable Arcball Rotation | — | Switches rotation from ring-gizmo to arcball (sphere drag). | Editor pref `bUseArcballRotation` | **MISSING** | P2 |
| Enable Screenspace Rotation | — | Rotates object around camera Z axis (roll) using outer ring. | Editor pref `bUseScreenspaceRotation` | **MISSING** | P2 |
| Allow Translucent Selection | T | Clicking through translucent surfaces selects objects behind them. | `bAllowTranslucentSelection` | **MISSING** — no translucency occlusion in selection | P1 |
| Allow Group Selection | Ctrl+Shift+G | Selects entire actor group when any group member is clicked. | `bAllowGroupSelection` | **MISSING** — no group concept | P2 |
| Strict Box Selection | — | Box select only selects fully-enclosed actors (not just touched). | `bStrictBoxSelection` | **MISSING** | P2 |
| Box Select Occluded Objects | — | Box select includes occluded (hidden-by-geometry) actors. | `bBoxSelectOccluded` | **MISSING** | P2 |
| Show Subcomponents | — | Hovering shows component sub-parts; click selects component not actor. | `bShowSubComponents` | **MISSING** | P2 |
| Preselection Highlight | — | Objects highlight on mouse hover before click-selection. | `bEnablePreselectionHighlight` | **EXISTS** — `OutlinePass` draws selection outline. Hover highlight is **MISSING** | P1 |

---

## Menu 2: Perspective / Camera Dropdown

### View Projection

| Item | Hotkey | What it does | UE5 Mapping | UzEngine Status | Priority |
|------|--------|-------------|-------------|-----------------|----------|
| Perspective | ALT+G | Standard 3D perspective projection. | `ViewportType = LVT_Perspective`, `ProjectionMode::Perspective` | **EXISTS** — `Camera::ProjectionMode::Perspective` | P0 |
| Top | ALT+J | Orthographic top-down view (+Z looking down). | `LVT_OrthoXY` | **MISSING** — no ortho views | P1 |
| Bottom | ALT+SHIFT+J | Orthographic bottom-up view (-Z looking up). | `LVT_OrthoNegativeXY` | **MISSING** | P2 |
| Left | ALT+K | Orthographic left side (-Y looking right). | `LVT_OrthoYZ` | **MISSING** | P1 |
| Right | ALT+SHIFT+K | Orthographic right side (+Y looking left). | `LVT_OrthoNegativeYZ` | **MISSING** | P2 |
| Front | ALT+H | Orthographic front view (+X looking back). | `LVT_OrthoXZ` | **MISSING** | P1 |
| Back | ALT+SHIFT+H | Orthographic back view (-X looking forward). | `LVT_OrthoNegativeXZ` | **MISSING** | P2 |

**Gap**: `Camera` has `ProjectionMode::Orthographic` but no viewport-switching UI or ortho focus distance computation.

### Camera Control Submenus

| Item | What it does | UE5 Mapping | UzEngine Status | Priority |
|------|-------------|-------------|-----------------|----------|
| Pilot (submenu) | "Possesses" a Camera Actor in the scene — the viewport becomes its feed. WASD flies through it. | `FEditorViewportClient::SetActorLock()` | **MISSING** — no actor-lock viewport mode | P1 |
| Camera Movement (submenu) | Speed presets (1–8) for fly camera + acceleration multiplier. Mouse wheel adjusts speed. | `CameraSpeed` 1-8, `CameraSpeedScalar` | **PARTIAL** — `FlyCameraController` has speed but no preset levels | P1 |

### View Parameters

| Item | Default | What it does | UE5 CVar | UzEngine Status | Priority |
|------|---------|-------------|----------|-----------------|----------|
| Field of View | 90° | Horizontal FOV of the editor viewport in degrees. Does NOT affect game camera FOV. | `EditorViewportClient::ViewFOV` | **EXISTS** — `Camera::SetPerspective(fovY)`. Editor UI to change it is **MISSING** | P0 |
| Near View Plane | 10 cm | Near clip plane distance. Decreasing reveals near-object clipping. | `NearPlane` on viewport client | **EXISTS** — `Camera::SetPerspective(..., near, far)` | P1 |
| Far View Plane | Infinity | Far clip plane. "Infinity" uses floating-point max (reversed-Z safe). | `FarPlane` (0 = no far clip) | **EXISTS** — camera exposes far plane | P1 |

### Exposure / EV

| Item | Default | What it does | UE5 CVar | UzEngine Status | Priority |
|------|---------|-------------|----------|-----------------|----------|
| Game Settings (checkbox) | off | Uses the scene's PostProcessVolume auto-exposure settings instead of the manual EV100 below. | `bUseGameExposureSettings` | **MISSING** — no per-viewport exposure override | P1 |
| EV100 | 1.0 | Manual exposure value in EV100 units. 0 = 1 cd/m², each +1 doubles brightness. Applied as `exposure = 1 / (1.2 * 2^EV100)`. | `ExposureSettings.FixedEV100` in viewport | **MISSING** — post-process tonemap has auto-exposure but no manual EV100 override per viewport | P1 |

### Viewport Type

| Item | What it does | UE5 Mapping | UzEngine Status | Priority |
|------|-------------|-------------|-----------------|----------|
| Default Viewport | Standard editor viewport with all overlays (gizmo, grid, stats, outlines). | Default | **EXISTS** | P0 |
| Cinematic Viewport | Letterbox overlay, proper aspect ratio, hides editor chrome. Used to frame shots. | `bCinematicMode` | **MISSING** | P2 |

### Create Submenu

| Item | What it does | UE5 Mapping | UzEngine Status | Priority |
|------|-------------|-------------|-----------------|----------|
| Create Camera | Places a `CameraActor` in the scene at the current viewport position/orientation. | Spawns `ACameraActor`, copies `ViewTransform` | **MISSING** — no camera actor concept | P1 |
| Bookmarks (submenu) | Saves/restores named viewport positions (1-10 slots, Ctrl+1..0 / 1..0). | `FViewportBookmark[10]` per viewport | **MISSING** | P2 |

### Camera Options

| Item | What it does | UE5 Mapping | UzEngine Status | Priority |
|------|-------------|-------------|-----------------|----------|
| Allow Cinematic Control | Allows Sequencer/matinee to drive this viewport's camera. | `bAllowCinematicControl` | **MISSING** | P2 |
| Game View | G | Hides all editor-only geometry (gizmos, grid, debug lines). Shows scene as it would appear in game. | `bGameView` toggle | **MISSING** — no clean game-view toggle | P1 |
| Allow Camera Shakes | Enables camera shake preview in editor viewport (e.g., explosion shake). | `bAllowCameraShakes` | **MISSING** | P2 |
| Preview Selected Cameras | When a CameraActor is selected, shows a PIP of its view. | `bPreviewSelectedCameras` | **MISSING** | P2 |

### High Resolution Screenshot

| Item | What it does | UE5 Mapping | UzEngine Status | Priority |
|------|-------------|-------------|-----------------|----------|
| High Resolution Screenshot... | Opens dialog for multiplied-resolution screenshot (1x–8x render scale). Saves to `Saved/Screenshots/`. | `FHighResScreenshotConfig`, `r.HighResScreenshot.Multiplier` | **PARTIAL** — `ScreenshotCapture` exists, no multiplier/dialog | P1 |

---

## Menu 3: View Mode (Lit dropdown)

### Primary View Modes

| Item | Hotkey | What it does | UE5 CVar | UzEngine Status | Priority |
|------|--------|-------------|----------|-----------------|----------|
| Lit | ALT+4 | Full PBR deferred rendering — all lights, shadows, reflections. | `VMI_Lit` | **EXISTS** — `DebugViewMode::Lit` (default) | P0 |
| Unlit | ALT+3 | Shows only albedo (base color), no lighting computation. | `VMI_Unlit` | **EXISTS** — `DebugViewMode::Unlit` (alias for albedo) | P0 |
| Wireframe | ALT+2 | Draws triangle edges over black background. No fill. | `VMI_Wireframe` | **EXISTS** — `DebugViewMode::Wireframe` (rasterizer state toggle) | P0 |
| Lit Wireframe | — | Draws wireframe overlaid on top of lit scene. | `VMI_LitWireframe` | **MISSING** — only one or the other, not composited | P1 |
| Detail Lighting | ALT+5 | Shows only normal contribution to lighting (grey albedo, all lights). Isolates normal map quality. | `VMI_DetailLighting` | **MISSING** | P1 |
| Lighting Only | ALT+6 | Full lighting with white albedo. Shows light intensity and distribution. | `VMI_LightingOnly` | **MISSING** | P1 |
| Reflections | — | Shows only specular reflections (IBL + SSR). Black everywhere else. | `VMI_Reflections` | **MISSING** | P2 |

### Debug Modes

| Item | What it does | UE5 CVar | UzEngine Status | Priority |
|------|-------------|----------|-----------------|----------|
| Player Collision | Visualizes physics collision volumes (blue = blocking, green = overlap). | `SHOW_Collision` | **MISSING** — no physics debug viz | P1 |
| Visibility Collision | Visualizes visibility/line-of-sight collision only. | `SHOW_VisibilityCollision` | **MISSING** | P2 |
| Path Tracing | Progressive path-traced reference mode. Software/hardware RT. | `r.PathTracing 1` | **MISSING** — not planned for Phase 1-2 | P2 |

### Optimization Viewmodes Submenu

| Item | What it does | UE5 CVar | UzEngine Status | Priority |
|------|-------------|----------|-----------------|----------|
| Light Complexity | Heatmap of how many lights affect each pixel (blue=1, red=many). | `VMI_LightComplexity` | **MISSING** | P1 |
| Shader Complexity | Heatmap of estimated shader instruction count per pixel. | `VMI_ShaderComplexity` | **MISSING** | P2 |
| Quad Overdraw | Heatmap of pixel quad overdraw (wasted fill rate). | `VMI_QuadOverdraw` | **MISSING** | P2 |
| Stationary Light Overlap | Shows where stationary lights exceed the 4-light shadow overlap limit. | `VMI_StationaryLightOverlap` | **MISSING** | P2 |

### Buffer Visualization Submenu

| Item | What it does | UE5 CVar | UzEngine Status | Priority |
|------|-------------|----------|-----------------|----------|
| Overview (full G-Buffer grid) | 2×N grid of all G-Buffer channels labeled. | `VMI_VisualizeBuffer` | **EXISTS** — `DebugViewMode::Overview` | P0 |
| BaseColor | G-Buffer albedo channel only. | Per-channel visualizer | **EXISTS** — `DebugViewMode::Albedo` | P0 |
| Specular | Specular F0 channel (metallic path). | Per-channel visualizer | **MISSING** | P1 |
| Roughness | G-Buffer roughness channel (greyscale). | Per-channel visualizer | **EXISTS** — `DebugViewMode::Roughness` | P0 |
| Metallic | G-Buffer metallic channel (greyscale). | Per-channel visualizer | **EXISTS** — `DebugViewMode::Metallic` | P0 |
| WorldNormal | Decoded world-space normals, remapped [0,1]. | Per-channel visualizer | **EXISTS** — `DebugViewMode::WorldNormal` | P0 |
| AmbientOcclusion | GTAO/SSAO value channel. | Per-channel visualizer | **EXISTS** — `DebugViewMode::AO` | P0 |
| Depth | Linearized depth (white=near, black=far). | Per-channel visualizer | **EXISTS** — `DebugViewMode::Depth` | P0 |

### LOD Coloration Submenu

| Item | What it does | UE5 CVar | UzEngine Status | Priority |
|------|-------------|----------|-----------------|----------|
| Mesh LOD Coloration | Colors each mesh by current LOD level (green=LOD0, yellow=LOD1, red=LOD2+). | `VMI_LODColoration` | **MISSING** — LOD system exists but no color viz | P1 |
| Hierarchical LOD Coloration | Colors by HLOD cluster level. | `VMI_HLODColoration` | **MISSING** — no HLOD | P2 |

### Other Debug Submenus (brief)

| Submenu | What it does | UzEngine Status | Priority |
|---------|-------------|-----------------|----------|
| Nanite Visualization | Nanite cluster/page/material debugs. | N/A — no Nanite | P2 |
| Lumen | Lumen radiosity probe, surface cache, irradiance visualization. | N/A — no Lumen | P2 |
| Virtual Shadow Map | VSM page tables, caching, invalidation. | N/A — no VSM | P2 |
| Ray Tracing Debug | BVH heatmap, RT shadow quality. | N/A — no RT | P2 |
| GPU Skin Cache | Skinned mesh cache occupancy. | N/A — no skeletal in renderer yet | P2 |
| Substrate | Substrate material complexity. | N/A | P2 |
| Actor Coloration | Colors actors by type/layer/mobility. | **MISSING** | P2 |
| Landscape Visualizers | Heightmap, splatmap, LOD visualization. | N/A — no terrain | P2 |

---

## Menu 4: Show Flags (Eye Icon Dropdown)

Show flags are boolean toggles that enable/disable rendering of specific feature categories.
UE5 stores them in `FEngineShowFlags` (a bitfield struct with ~150 flags).
UzEngine should implement these as CVars (`r.Show.*`) polled by the relevant passes.

### Top-Level Controls

| Item | What it does | UE5 Flag | UzEngine CVar | Priority |
|------|-------------|----------|---------------|----------|
| Use Defaults | Resets all show flags to their default values. | Resets `FEngineShowFlags` | `CVarRegistry::ResetAll("r.Show.")` — **MISSING** | P0 |

### Common Show Flags

| Item | Hotkey | Default | What it hides when off | UE5 Flag | UzEngine Status | Priority |
|------|--------|---------|----------------------|----------|-----------------|----------|
| Anti-Aliasing | — | ON | Disables TAA/MSAA; shows raw jagged edges. | `EngineShowFlags.AntiAliasing` | **MISSING** — `r.TAA` CVar exists but no show-flag wiring | P1 |
| Atmosphere | — | ON | Hides sky atmosphere scattering. | `EngineShowFlags.Atmosphere` | **MISSING** — no per-flag, SkyAtmosphere always renders | P1 |
| BSP | — | ON | Hides BSP brush geometry (N/A for UzEngine). | `EngineShowFlags.BSP` | N/A | P2 |
| Cameras | — | ON | Shows/hides CameraActor icons in viewport. | `EngineShowFlags.Cameras` | **MISSING** | P2 |
| Cloud | — | ON | Hides volumetric cloud layer. | `EngineShowFlags.Cloud` | **MISSING** — clouds always render | P1 |
| Collision | ALT+C | OFF | Shows physics collision geometry wireframe. | `EngineShowFlags.Collision` | **MISSING** | P1 |
| Decals | — | ON | Disables decal rendering pass. | `EngineShowFlags.Decals` | **MISSING** — decal pass always runs | P1 |
| Fog | ALT+F | ON | Hides height/volumetric fog. | `EngineShowFlags.Fog` | **MISSING** — `r.Fog` CVar to be added | P0 |
| Grid | — | ON | Hides the editor grid floor. | `EngineShowFlags.Grid` | **MISSING** — `EditorGrid` always draws; needs toggle | P0 |
| Hair | — | ON | Hides hair/groom geometry (N/A). | `EngineShowFlags.Hair` | N/A | P2 |
| Landscape | ALT+L | ON | Hides terrain mesh. | `EngineShowFlags.Landscape` | N/A — no terrain | P2 |
| Navigation | P | OFF | Shows NavMesh debug overlay. | `EngineShowFlags.Navigation` | **MISSING** — no NavMesh yet | P2 |
| Particle Sprites | — | ON | Hides all CPU/GPU particle systems. | `EngineShowFlags.ParticleSprites` | **MISSING** — VFX always renders | P1 |
| Skeletal Meshes | — | ON | Hides all skeletal mesh actors. | `EngineShowFlags.SkeletalMeshes` | **MISSING** | P1 |
| Static Meshes | — | ON | Hides all static mesh actors. | `EngineShowFlags.StaticMeshes` | **MISSING** | P1 |
| Translucency | — | ON | Disables translucent material rendering. | `EngineShowFlags.Translucency` | **MISSING** | P1 |
| Widget Components | — | ON | Hides in-world UI widget 3D components. | `EngineShowFlags.WidgetComponents` | N/A | P2 |

### Post Processing Show Flags

| Item | Default | What it hides | UE5 Flag | UzEngine CVar | Priority |
|------|---------|--------------|----------|---------------|----------|
| Bloom | ON | Disables bloom pass. | `EngineShowFlags.Bloom` | `r.Bloom` — **MISSING** as show flag | P0 |
| Eye Adaptation / Auto Exposure | ON | Disables auto-exposure; uses fixed exposure. | `EngineShowFlags.EyeAdaptation` | **MISSING** | P1 |
| Tone Mapping | ON | Disables tonemap curve (raw HDR shown). | `EngineShowFlags.Tonemapper` | **MISSING** | P1 |
| Vignette | ON | Disables vignette effect. | `EngineShowFlags.Vignette` | **MISSING** | P1 |
| Depth of Field | ON | Disables DOF (N/A currently). | `EngineShowFlags.DepthOfField` | N/A | P2 |
| Motion Blur | ON | Disables motion blur (N/A). | `EngineShowFlags.MotionBlur` | N/A | P2 |
| Ambient Occlusion | ON | Disables GTAO pass. | `EngineShowFlags.AmbientOcclusion` | `r.GTAO` — **MISSING** as show flag | P0 |
| Screen Space Reflections | ON | Disables SSR pass. | `EngineShowFlags.ScreenSpaceReflections` | **MISSING** | P1 |
| Screen Space Ambient Occlusion | ON | Alias for GTAO. | `EngineShowFlags.SSAO` | Same as above | P0 |

### Light Types Flags

| Item | Default | What it hides | Priority |
|------|---------|--------------|----------|
| Directional Lights | ON | Hides sun/directional light contribution. | P1 |
| Point Lights | ON | Hides all point light contributions. | P1 |
| Spot Lights | ON | Hides all spot light contributions. | P1 |
| Sky Light | ON | Hides sky light (IBL ambient). | P1 |

### Lighting Component Flags

| Item | Default | What it hides | Priority |
|------|---------|--------------|----------|
| Diffuse | ON | Disables diffuse lighting term. | P2 |
| Specular | ON | Disables specular term. | P2 |
| Indirect Lighting | ON | Disables IBL/GI ambient term. | P1 |
| Direct Lighting | ON | Disables all direct lights. | P1 |
| Contact Shadows | ON | Disables screen-space contact shadows. | P1 |
| Subsurface Scattering | ON | Disables SSS term. | P2 |

**Implementation plan for UzEngine**: Create `engine/renderer/core/show_flags.h` with a `ShowFlags` struct (packed booleans), register each as `r.Show.<Name>` CVars, poll once per frame in pass setup. Passes check their relevant flag before executing.

---

## Menu 5: Scalability / Performance (Settings Gear Icon)

### Realtime Toggle

| Item | Default | What it does | UE5 Mapping | UzEngine Status | Priority |
|------|---------|-------------|-------------|-----------------|----------|
| Realtime Viewport | ON | When off, viewport only redraws on input. Saves GPU for content tasks. | `bIsRealtime` on viewport client | **MISSING** — render loop always runs | P1 |

### Viewport Scalability Panel

Each quality group has 5 levels: Low / Medium / High / Epic / Cinematic.

| Group | What it controls | UE5 CVars | UzEngine Status | Priority |
|-------|-----------------|-----------|-----------------|----------|
| View Distance | `r.LevelOfDetailDistanceFactor` — LOD switching threshold | `r.ViewDistanceScale` | **PARTIAL** — LOD system exists, no CVar-based distance scale | P1 |
| Anti-Aliasing | TSR / TAA / FXAA | `r.AntiAliasingMethod`, `r.TSR.*` | **PARTIAL** — TAA flag exists, no quality levels | P1 |
| Post Processing | Bloom quality, lens flare passes | `r.BloomQuality`, `r.LensFlareQuality` | **MISSING** as scalability group | P1 |
| Shadows | Shadow resolution, cascade count, PCF taps | `r.Shadow.MaxResolution`, `r.Shadow.CSM.MaxCascades` | **PARTIAL** — `QualityPresetManager::kGroupShadows` exists | P0 |
| Global Illumination | Lumen / SSGI quality | `r.Lumen.*` | N/A — no GI system | P2 |
| Reflections | SSR quality, ray count | `r.SSR.MaxRoughness`, `r.SSR.Quality` | **MISSING** as scalability group | P1 |
| Textures | Mip bias, streaming budget | `r.Streaming.MipBias` | **MISSING** | P1 |
| Effects | Particle count, emitter quality | `r.ParticleLODBias` | **MISSING** | P1 |
| Foliage | Foliage draw distance, density | `foliage.LODDistanceScale` | N/A | P2 |
| Shading | Material quality (Simplify) | `r.MaterialQualityLevel` | **MISSING** | P1 |
| Landscape | Terrain tessellation, streaming | `r.Landscape.LODBias` | N/A — no terrain | P2 |

**UzEngine mapping**: `QualityPresetManager` already has infrastructure. Needs: Anti-Aliasing, PostProcessing, Reflections, Textures, Effects, Shading groups registered. Groups "GlobalIllumination", "Foliage", "Landscape" can be stubs.

### PIE (Play-In-Editor) 3D Resolution

| Item | Range | What it does | UE5 Mapping | UzEngine Status | Priority |
|------|-------|-------------|-------------|-----------------|----------|
| Screen Percentage | 50–200% | Renders at this percentage of native resolution then upscales. Equivalent to `r.ScreenPercentage`. | `r.ScreenPercentage`, passed to TSR | **EXISTS** — `r.ScreenPercentage` CVar registered. No editor UI slider. | P1 |

### Quality Presets (row of buttons)

| Preset | What it sets | UzEngine Equivalent | Priority |
|--------|-------------|---------------------|----------|
| Default | All groups = High | `QualityPresetManager::ApplyPreset(QualityLevel::High)` | **EXISTS** — infra is there | P0 |
| Performance | All groups = Low/Medium | `QualityLevel::Low` | **EXISTS** | P0 |
| Balanced | All groups = Medium | `QualityLevel::Medium` | **EXISTS** | P0 |
| Quality | All groups = High/Ultra | `QualityLevel::Ultra` | **EXISTS** | P0 |
| Native | Screen percentage 100%, all groups = Cinematic | `QualityLevel::Cinematic` + SP=100 | **EXISTS** | P0 |

**Gap**: Presets exist in `QualityPresetManager` but the VSCode extension toolbar has no UI to trigger them. Need IPC commands: `"quality.setPreset"` with level enum.

---

## Menu 6: Viewport Settings (Wrench Icon)

### Audio

| Item | Default | What it does | UE5 Mapping | UzEngine Status | Priority |
|------|---------|-------------|-------------|-----------------|----------|
| Level Editor Volume | 0 dB | Master volume for audio that plays in the viewport (ambient sounds, SFX preview). | `GEditor->GetEditorVolumeMultiplier()` | **MISSING** — audio system planned but not viewport-integrated | P2 |

### Mouse Controls

| Item | Default | What it does | UE5 Mapping | UzEngine Status | Priority |
|------|---------|-------------|-------------|-----------------|----------|
| Mouse Sensitivity | 0.2 | Multiplier for look speed during RMB+drag navigation. | `GetMutableDefault<ULevelEditorViewportSettings>()->MouseSensitivty` | **PARTIAL** — `FlyCameraController` has speed but no sensitivity CVar | P1 |
| Mouse Scroll Zoom Speed | 5 | Dolly (zoom) speed when using mouse wheel. | `CameraSpeedScalar` | **MISSING** as separate setting | P1 |
| Invert Middle Mouse Pan | OFF | Inverts pan direction when MMB-dragging. | `bInvertMiddleMousePan` | **MISSING** | P2 |
| Invert Orbit Axis | OFF | Inverts Y axis when orbiting (ALT+LMB). | `bInvertOrbitY` | **MISSING** | P2 |
| Invert Right Mouse Dolly | OFF | Inverts RMB+drag dolly direction. | `bInvertRightMouseDolly` | **MISSING** | P2 |
| Scroll Gestures | submenu | Trackpad gesture sensitivity options. | Platform input settings | **MISSING** | P2 |
| Open Viewport Preferences | — | Opens the main Preferences dialog to the Viewport section. | `FModuleManager::LoadModuleChecked<ISettingsModule>` | **MISSING** — no preferences dialog | P1 |

---

## Menu 7: Material Editor Preview (Shader Graph Viewport)

This is a sub-viewport within the Material Editor, not the Level Viewport. Documented for completeness.

### Preview Controls

| Item | What it does | UzEngine Status | Priority |
|------|-------------|-----------------|----------|
| Sphere preview mesh | Renders the material on a sphere in an isolated IBL scene. Shows specular highlights + diffuse. | **MISSING** — no material editor exists | P1 |
| Lit mode dropdown | Same view mode list as Level viewport — lets user inspect albedo, normals, AO while authoring material. | **MISSING** | P1 |
| Realtime toggle | Enables/disables real-time update of the material preview (vs manual refresh). | **MISSING** | P1 |

### Material Properties Panel (right side inspector)

| Property | Type | Default | What it controls | UzEngine Status | Priority |
|----------|------|---------|-----------------|-----------------|----------|
| Material Domain | Enum | Surface | Surface / Deferred Decal / Light Function / Volume / Post Process / UI | **MISSING** — materials have no domain concept | P1 |
| Blend Mode | Enum | Opaque | Opaque / Masked / Translucent / Additive / Modulate / Alpha Composite | **PARTIAL** — translucent materials implied but no formal blend mode enum | P1 |
| Shading Model | Enum | Default Lit | Default Lit / Unlit / Subsurface / Preintegrated Skin / Clear Coat / Two-Layer / Eye / Single Layer Water / Thin Translucent / From Material Expression | **MISSING** — single shading model | P1 |
| Two Sided | bool | false | Disables backface culling for this material. | **MISSING** — always front-face cull | P1 |
| Is Thin Surface | bool | false | Treats material as infinitesimally thin (affects SSS, translucency). | **MISSING** | P2 |
| Use Material Attributes | bool | false | Replaces individual outputs with a single `FMaterialAttributes` struct output pin. | **MISSING** — shader graph not implemented | P2 |
| Cast Ray Traced Shadow | bool | true | Whether this material occludes in RT shadow systems. | N/A | P2 |

---

## UzEngine Implementation Priority Summary

### P0 — Must have for editor MVP

1. **Select mode (Q)** — no-gizmo selection-only mode
2. **World vs Local coordinate space** for gizmo
3. **Show/hide grid** toggle (`r.Show.Grid`)
4. **Show/hide fog** toggle (`r.Show.Fog`)
5. **Bloom show flag** (`r.Show.Bloom`)
6. **AO show flag** (`r.Show.AO`)
7. **Quality preset buttons** wired to `QualityPresetManager` via IPC
8. **View modes**: Detail Lighting, Lighting Only (ALT+5, ALT+6)

### P1 — Phase 2 polish

1. Orthographic views (Top, Left, Front + opposites)
2. Game View mode (G key — hides all editor chrome)
3. Show flags system (`engine/renderer/core/show_flags.h`)
4. All Post Processing show flags (Bloom, Tonemapper, EyeAdaptation, Vignette, AO, SSR)
5. Light type show flags (Directional, Point, Spot, Sky)
6. Lit Wireframe composite mode
7. LOD Coloration view mode
8. Buffer visualization: Specular channel
9. Preselection highlight on hover
10. Mouse sensitivity and scroll speed CVars
11. Realtime viewport toggle
12. High-res screenshot multiplier
13. Camera speed presets (1-8)
14. Material Editor domain / blend mode / shading model properties

### P2 — Phase 3+ or deferred

- Arcball rotation, screenspace rotation, bookmarks
- Cinematic viewport letterbox
- Camera actor / pilot mode
- Path tracing
- Nanite, Lumen, VSM visualizations (requires those systems first)
- Physics collision debug overlay
- Shader complexity, quad overdraw heatmaps
- Foliage, landscape, navmesh show flags

---

## Implementation Notes

### Show Flags Architecture (recommended)

```cpp
// engine/renderer/core/show_flags.h
struct ShowFlags {
    bool fog            : 1;  // r.Show.Fog          (default: true)
    bool grid           : 1;  // r.Show.Grid          (default: true)
    bool bloom          : 1;  // r.Show.Bloom         (default: true)
    bool ao             : 1;  // r.Show.AO            (default: true)
    bool atmosphere     : 1;  // r.Show.Atmosphere    (default: true)
    bool decals         : 1;  // r.Show.Decals        (default: true)
    bool staticMeshes   : 1;  // r.Show.StaticMeshes  (default: true)
    bool particles      : 1;  // r.Show.Particles     (default: true)
    bool directLight    : 1;  // r.Show.DirectLight   (default: true)
    bool indirectLight  : 1;  // r.Show.IndirectLight (default: true)
    bool shadows        : 1;  // r.Show.Shadows       (default: true)
    bool ssr            : 1;  // r.Show.SSR           (default: true)
    bool tonemapper     : 1;  // r.Show.Tonemapper    (default: true)
    bool vignette       : 1;  // r.Show.Vignette      (default: true)
    bool wireframe      : 1;  // r.Show.Wireframe     (default: false)
};

// Gathered once per frame from CVarRegistry, passed to each pass via FrameContext.
ShowFlags GatherShowFlags(CVarRegistry& cvars);
```

### Coordinate Space for Gizmo

```cpp
// editor/gizmo/gizmo_interaction.h — add:
enum class GizmoCoordSpace : uint8_t { World, Local };

// GizmoInteraction::BeginDrag() checks m_coordSpace:
// - World: drag axis = global XYZ
// - Local: drag axis = entity's rotation * XYZ
```

### View Mode Additions Needed

Add to `DebugViewMode` enum (debug_view_mode.h):
- `DetailLighting = 13` — grey albedo + all lights
- `LightingOnly   = 14` — white albedo + all lights
- `Reflections    = 15` — IBL + SSR only
- `LitWireframe   = 16` — composite wireframe over lit
- `LodColoration  = 17` — per-LOD color coding
- `LightComplexity = 18` — light count heatmap
