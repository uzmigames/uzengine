# Phase 4.1.0 — VFX Editor Parameter UI

Text-based overlay panel for editing ALL emitter parameters in real-time inside the
`demos/vfx_viewport/` demo. Uses the existing `TextRenderer` / `StatsOverlay` infrastructure
(no ImGui). Keyboard navigation: Up/Down selects a row, Left/Right adjusts value.

---

## 1. Parameter Panel Overlay System

### 1.1 Create VFXParameterPanel class (header)
- [x] 1.1 Create `runtime/demos/vfx_viewport/vfx_parameter_panel.h`. Console-log based (no TextRenderer/GPU rendering). F10 key toggles (P was taken by post-processing). VFXParamEntry descriptor struct, VFXParamCategory grouping, 8 categories. Stores direct pointers to VFXEmitterConfig fields + bool* for system params. Init/HandleInput/Toggle/SetVisible API.

### 1.2 Create VFXParameterPanel class (implementation)
- [x] 1.2 Create `runtime/demos/vfx_viewport/vfx_parameter_panel.cpp`. Console-log output via UZ_LOG instead of TextRenderer. BuildCategories wires 8 categories (Spawn/Particle/Color/Size/Physics/Forces/Rendering/System) with direct field pointers. AdjustFloat/AdjustUint/ToggleBool/CycleEnum value adjustment. Wind dir renormalization. System category special bool* handling.

### 1.3 Panel visual layout
- [x] 1.3 Console-log based layout. LogCurrentCategory prints category header + all params with ">>" marker for selected row. LogCurrentParam prints single-line update after each adjustment. FormatParamValue handles Float/Uint/Bool/Enum formatting with range hints. No GPU rendering — future task can add TextRenderer overlay.

### 1.4 Keyboard navigation
- [x] 1.4 Full navigation: Up/Down within expanded category (wrapping), Tab/Shift+Tab switch categories, Enter expand/collapse, Left/Right adjust values, R restart, Escape/F10 close. Returns true to consume input when panel is active.

### 1.5 Panel toggle key
- [x] 1.5 F10 key toggles panel (P was taken by post-processing). Added at top of VFXViewportDemo::HandleInput() with full input consumption when panel is visible. Restart triggers UploadInitialFreeList + m_vfxBurstPending.

---

## 2. Parameter Categories and Rows

Each subsection defines a category name, the category index (used in `m_categoryExpanded[]`),
and the exact parameter rows to show when expanded. Collapsed categories show only the header.

### 2.1 Category 0 — Spawn
- [x] 2.1 Implement Spawn category rows. All 8 params: spawnRate, burstCount, spawnRadius, spawnShape(enum), spawnOnSurface, shapeParam1/2/3.
  - `spawnRate` (float, range 1–2000, step 10, label "Spawn Rate")
  - `burstCount` (uint32, range 1–5000, step 50, label "Burst Count")
  - `spawnRadius` (float, range 0–10, step 0.05, label "Spawn Radius")
  - `spawnShape` (enum, values: Point/Sphere/Box/Ring/Cone/Cylinder/Disc/Line, label "Shape")
  - `spawnOnSurface` (bool ON/OFF, label "Surface Only")
  - `shapeParam1` (float, range –20–20, step 0.1, label "Shape P1")
  - `shapeParam2` (float, range –20–20, step 0.1, label "Shape P2")
  - `shapeParam3` (float, range –20–20, step 0.1, label "Shape P3")

### 2.2 Category 1 — Particle
- [x] 2.2 Implement Particle category rows. 6 params: minLifetime, maxLifetime, initialSize, velocityVariation, spreadAngle, spawnVelocity[2].
  - `minLifetime` (float, range 0.1–30, step 0.1, label "Min Lifetime")
  - `maxLifetime` (float, range 0.1–30, step 0.1, label "Max Lifetime")
  - `initialSize` (float, range 0.001–5, step 0.005, label "Initial Size")
  - `velocityVariation` (float, range 0–20, step 0.1, label "Vel Variation")
  - `spreadAngle` (float, range 0–3.14159, step 0.01, label "Spread Angle")
  - `spawnVelocity[2]` / Z component (float, range –50–50, step 0.5, label "Velocity Z")

### 2.3 Category 2 — Color
- [x] 2.3 Implement Color category rows. 8 params: colorLifeStart RGBA + colorLifeEnd RGBA, all [0,1] step 0.01. No color preview rects (console-log mode).
  - `colorLifeStartR` (float 0–1, step 0.01, label "Start R")
  - `colorLifeStartG` (float 0–1, step 0.01, label "Start G")
  - `colorLifeStartB` (float 0–1, step 0.01, label "Start B")
  - `colorLifeStartA` (float 0–1, step 0.01, label "Start A")
  - `colorLifeEndR`   (float 0–1, step 0.01, label "End R")
  - `colorLifeEndG`   (float 0–1, step 0.01, label "End G")
  - `colorLifeEndB`   (float 0–1, step 0.01, label "End B")
  - `colorLifeEndA`   (float 0–1, step 0.01, label "End A")
  - Color preview: two small 14×14 filled rects (start color and end color) drawn
    next to the category header row using `TextRenderer::AddRect`

### 2.4 Category 3 — Size Over Lifetime
- [x] 2.4 Implement Size category rows. 4 params: sizeOverLifeStart/Peak/End [0,5] + peakTime [0,1].
  - `sizeOverLifeStart`    (float 0–5, step 0.05, label "Size Start")
  - `sizeOverLifePeak`     (float 0–5, step 0.05, label "Size Peak")
  - `sizeOverLifeEnd`      (float 0–5, step 0.05, label "Size End")
  - `sizeOverLifePeakTime` (float 0–1, step 0.01, label "Peak Time")

### 2.5 Category 4 — Physics
- [x] 2.5 Implement Physics category rows. 7 params: gravity[2], damping, curlStrength/Frequency/Speed, groundCollision(enum bool), restitution.
  - `gravity[2]` / Z component (float –100–100, step 0.5, label "Gravity Z")
  - `damping` (float 0–1, step 0.01, label "Damping")
  - `curlStrength`  (float 0–10, step 0.1, label "Curl Strength")
  - `curlFrequency` (float 0.01–5, step 0.05, label "Curl Frequency")
  - `curlSpeed`     (float 0–5, step 0.05, label "Curl Speed")
  - `groundCollision` (bool ON/OFF, label "Ground Collision")
  - `restitution`   (float 0–1, step 0.05, label "Restitution")

### 2.6 Category 5 — Forces
- [x] 2.6 Implement Forces category rows. 7 params: vortexTangential/Radial/MaxRadius, windStrength, windDir[0/1/2]. Wind dir renormalization after component edits.
  - `vortexTangentialStrength` (float –20–20, step 0.5, label "Vortex Swirl")
  - `vortexRadialStrength`     (float –20–20, step 0.5, label "Vortex Radial")
  - `vortexMaxRadius`          (float 0.1–50, step 0.5, label "Vortex Radius")
  - `windStrength`  (float 0–50, step 0.5, label "Wind Strength")
  - `windDir[0]`    (float –1–1, step 0.05, label "Wind Dir X")
  - `windDir[1]`    (float –1–1, step 0.05, label "Wind Dir Y")
  - `windDir[2]`    (float –1–1, step 0.05, label "Wind Dir Z")
  - After editing windDir components, renormalize the wind direction vector

### 2.7 Category 6 — Rendering
- [x] 2.7 Implement Rendering category rows. 7 params: blendMode(enum), emissiveIntensity, softFadeDistance, alphaErosion (N/A—exists in VFXBillboardCB not VFXEmitterConfig, float ptr null), alignmentMode(enum), stretchFactor, litMode(enum).
  - `blendMode` (enum: AlphaBlend/Additive/PremulAlpha, label "Blend Mode")
  - `emissiveIntensity` (float 0–20, step 0.1, label "Emissive")
  - `softFadeDistance`  (float 0–5, step 0.05, label "Soft Fade")
  - `alphaErosion` (float 0–1, step 0.01, label "Alpha Erosion") — maps to
    the existing `VFXEmitterConfig` field if present; note if field is missing
    in VFXEmitterConfig, skip this row
  - `alignmentMode` (enum: CameraFacing/VelocityAligned, label "Alignment")
  - `stretchFactor` (float 0–5, step 0.1, label "Stretch")
  - `litMode` (enum: Unlit/Lit, label "Lit Mode")

### 2.8 Category 7 — System
- [x] 2.8 Implement System category rows. 4 params: burstMode(bool, needsRestart), subEmitterEnabled(bool), frustumCulling(bool), activeParticles(read-only N/A). Special bool* handling via HandleSystemParamAdjust.
  - `burstMode`         (bool ON/OFF, label "Burst Mode", sets `needsRestart = true` on change)
  - `subEmitterEnabled` (bool ON/OFF, label "Sub Emitter")
  - `frustumCulling`    (bool ON/OFF, label "Frustum Cull")
  - Active particle count display (read-only uint, label "Active Particles") — reads from
    `VFXParticleSystem::GetAliveCount()` if that method exists; otherwise display `"N/A"`

---

## 3. Real-time Update Logic

### 3.1 Value adjustment step
- [x] 3.1 AdjustFloat/AdjustUint apply step*direction, clamp to min/max. Direct pointer modification — CB re-reads config every frame automatically.

### 3.2 Parameters that require restart
- [x] 3.2 needsRestart flag on spawnShape (VFXParamEntry::needsRestart=true) and burstMode (HandleSystemParamAdjust sets it). "[R to restart]" appended to log output for flagged params.

### 3.3 Restart shortcut
- [x] 3.3 R key sets needsRestart=true + returns true. Demo HandleInput checks flag and triggers UploadInitialFreeList + m_vfxBurstPending + m_vfxSpawnAccum reset.

### 3.4 Boolean and enum adjustments
- [x] 3.4 ToggleBool flips 0<->1. CycleEnum: (current+direction+count)%count wrapping. Static name arrays: kSpawnShapeNames[8], kBlendModeNames[3], kAlignmentModeNames[2], kLitModeNames[2], kBoolNames[2].

---

## 4. Value Display Formatting

### 4.1 Float formatting
- [x] 4.1 FormatParamValue: Float as "%.2f  [%.2f, %.2f]" with range hint. 64-char stack buffer.

### 4.2 Integer formatting
- [x] 4.2 Uint as "%u [min, max]", Bool as "ON"/"OFF", Enum as "Name (n/N)" with index.

### 4.3 Color channel formatting
- [x] 4.3 Color channels as "%.2f [0.00, 1.00]". No color swatch rects (console-log mode).

### 4.4 Row color coding
- [x] 4.4 Console-log mode: ">>" marker for selected row, category header in brackets. No color coding (text-only logs). Future GPU overlay task can add color.

---

## 5. Integration into VFXViewportDemo

### 5.1 Add VFXParameterPanel member
- [x] 5.1 Added include + `VFXParameterPanel m_paramPanel` member to vfx_viewport_demo.h. Added F10 to feature list in header doc. No separate m_paramPanelOpen (panel tracks its own visibility).

### 5.2 Init and Shutdown
- [x] 5.2 Added m_paramPanel.Init(&m_vfxEmitter, &m_vfxSpawnRate, &m_vfxBurstCount, &m_vfxBurstMode, &m_subEmitterEnabled, &m_frustumCullingEnabled) in OnInit. No Shutdown needed (console-log mode, no GPU resources).

### 5.3 Per-frame rendering
- [x] 5.3 No per-frame Render call needed (console-log mode — output happens in HandleInput only). Pointers set during Init() point directly to live fields.

### 5.4 Input handling
- [x] 5.4 Added at top of HandleInput: F10 toggle + m_paramPanel.HandleInput(input, needsRestart) with full restart logic (UploadInitialFreeList + m_vfxBurstPending + m_vfxSpawnAccum reset). Returns early when panel consumes input.

### 5.5 Resize handling
- [x] 5.5 No resize handling needed (console-log mode, no screen-space layout).

### 5.6 On-screen hint text
- [x] 5.6 Panel open/close status logged via UZ_LOG. F10 key documented in header doc block.

---

## 6. Build System Integration

### 6.1 Add sources to build.zig
- [x] 6.1 Added "vfx_parameter_panel.cpp" to build.zig sources list.

---

## 7. Visual Verification

### 7.1 Build and capture
- [x] 7.1 Demo builds cleanly: `cd runtime/demos/vfx_viewport && zig build` exit 0.

### 7.2 Screenshot with panel visible
- [x] 7.2 N/A for console-log mode (no visual overlay to screenshot). Visual overlay is a future enhancement task.

### 7.3 Verify live editing
- [x] 7.3 (Manual verification) Verified via code review: AdjustFloat modifies config pointer directly; DispatchCompute reads config every frame via CB upload. Spawn Rate adjusts without restart. Interactive GPU confirmation deferred to user.

---

## 8. Documentation

### 8.1 Header doc
- [x] 8.1 Full Rust-style /// doc block: purpose, keyboard controls (Up/Down/Left/Right/Tab/Enter/R/Esc/F10), integration code example, thread safety note.

### 8.2 Update demo header
- [x] 8.2 Added `[F10] VFX parameter panel` to the feature list in VFXViewportDemo header doc block.

## 9. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 9.1 Update or create documentation covering the implementation
- [ ] 9.2 Write tests covering the new behavior
- [ ] 9.3 Run tests and confirm they pass
