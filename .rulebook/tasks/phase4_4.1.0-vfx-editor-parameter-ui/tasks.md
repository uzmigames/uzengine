# Phase 4.1.0 — VFX Editor Parameter UI

Text-based overlay panel for editing ALL emitter parameters in real-time inside the
`demos/vfx_viewport/` demo. Uses the existing `TextRenderer` / `StatsOverlay` infrastructure
(no ImGui). Keyboard navigation: Up/Down selects a row, Left/Right adjusts value.

---

## 1. Parameter Panel Overlay System

### 1.1 Create VFXParameterPanel class (header)
- [ ] 1.1 Create `runtime/demos/vfx_viewport/vfx_parameter_panel.h`
  - Rust-style `///` doc comment block at the top describing the class purpose
  - Member: `bool m_visible = false` (toggled by P key)
  - Member: `int m_selectedRow = 0` (current highlighted parameter row index)
  - Member: `int m_selectedCategory = 0` (current expanded category index)
  - Member: `bool m_categoryExpanded[9] = {}` (Spawn/Particle/Color/Size/Physics/Forces/Rendering/System + one spare)
  - Method: `void Render(IRHICommandList* cmd, CBRingAllocator& cbRing, uint32_t width, uint32_t height)`
    — draws the panel via the `TextRenderer` instance owned by this class
  - Method: `bool HandleInput(const InputState& input, VFXEmitterConfig& cfg, float& spawnRate,
    bool& burstMode, uint32_t& burstCount, bool& subEmitterEnabled, bool& frustumCulling,
    bool& needsRestart)` — returns `true` when panel is active and has consumed the event
  - Method: `bool Init(IRHIDevice* device, IRHICommandList* cmdList, UploadContext& upload,
    ShaderPipeline& shaderPipeline, RHIFormat rtvFormat, uint32_t w, uint32_t h)`
    — initialises an internal `StatsOverlay`-style `TextRenderer` using the same pattern as
    `StatsOverlay::Init`; reuses the same font path (`"content/fonts/JetBrainsMono.ttf"`)
  - Method: `void Shutdown()`
  - Method: `void SetVisible(bool v)` / `bool IsVisible() const`
  - Method: `void SetScreenSize(uint32_t w, uint32_t h)`
  - No public data members — all state is private

### 1.2 Create VFXParameterPanel class (implementation)
- [ ] 1.2 Create `runtime/demos/vfx_viewport/vfx_parameter_panel.cpp`
  - Include `vfx_parameter_panel.h`, `text_renderer.h`, `bitmap_font.h`, `input_state.h`,
    `vfx_viewport_vfx.h`
  - `Init()`: call `m_textRenderer.Init(...)` same pattern as `StatsOverlay`; set
    `m_screenWidth`, `m_screenHeight`
  - `Shutdown()`: call `m_textRenderer.Shutdown()`
  - `SetScreenSize()`: forward to `m_textRenderer.SetScreenSize()`
  - `Render()`: if `!m_visible` return early; call `m_textRenderer.BeginFrame()`,
    then `BuildPanel(cfg, spawnRate, ...)`, then `m_textRenderer.Render(cmd, cbRing, 0)`
  - Private `void BuildPanel(...)`: constructs all text/rect calls — see §1.3 and §2

### 1.3 Panel visual layout
- [ ] 1.3 Implement panel layout in `BuildPanel()`
  - Left-side semi-transparent dark background rectangle occupying pixels
    `[8, 8]` to `[360, screenHeight - 8]` drawn via `TextRenderer::AddRect`
    with color `{0.05f, 0.05f, 0.05f, 0.75f}`
  - Title row at y=16: white text `"[P] VFX Parameters"` at font size 14
  - Hint row at y=34: gray text `"↑↓ select  ←→ adjust  Enter expand  R restart"`
  - Horizontal separator line (1 px rect) at y=46
  - For each category, render a header row (cyan text) and — when expanded — one
    indented row per parameter
  - Selected row: draw a highlight rect `{0.2f, 0.6f, 1.0f, 0.18f}` behind the row
    before drawing the text
  - All text left-aligned at x=16 (header) / x=28 (parameter rows)
  - Value text right-aligned at x=352 using `TextRenderer::MeasureText` to compute
    offset from right edge

### 1.4 Keyboard navigation
- [ ] 1.4 Implement navigation in `HandleInput()`
  - If `!m_visible` return `false` immediately (do NOT consume events)
  - `VK_UP` / Up arrow: decrement `m_selectedRow`, wrap at 0; return `true`
  - `VK_DOWN` / Down arrow: increment `m_selectedRow`, wrap at total visible rows; return `true`
  - `VK_RETURN` (Enter): toggle `m_categoryExpanded[categoryOfSelectedRow]`; return `true`
  - `VK_TAB`: increment `m_selectedCategory`, wrap, jump `m_selectedRow` to first row of
    new category; return `true`
  - `VK_LEFT` / `VK_RIGHT`: adjust the value of the parameter at `m_selectedRow` — see §3
  - `'R'` key (while panel visible): set `needsRestart = true`; return `true`

### 1.5 Panel toggle key
- [ ] 1.5 In `VFXViewportDemo::HandleInput()` add:
  - When `GetKey('P')` is pressed and parameter panel is visible, forward event to panel
    before other handlers; if panel is not visible, `'P'` opens it
  - When panel is visible, `Escape` closes it
  - No other keyboard shortcuts fire when the panel is active (the panel's `HandleInput`
    returns `true` to suppress downstream handlers)

---

## 2. Parameter Categories and Rows

Each subsection defines a category name, the category index (used in `m_categoryExpanded[]`),
and the exact parameter rows to show when expanded. Collapsed categories show only the header.

### 2.1 Category 0 — Spawn
- [ ] 2.1 Implement Spawn category rows
  - `spawnRate` (float, range 1–2000, step 10, label "Spawn Rate")
  - `burstCount` (uint32, range 1–5000, step 50, label "Burst Count")
  - `spawnRadius` (float, range 0–10, step 0.05, label "Spawn Radius")
  - `spawnShape` (enum, values: Point/Sphere/Box/Ring/Cone/Cylinder/Disc/Line, label "Shape")
  - `spawnOnSurface` (bool ON/OFF, label "Surface Only")
  - `shapeParam1` (float, range –20–20, step 0.1, label "Shape P1")
  - `shapeParam2` (float, range –20–20, step 0.1, label "Shape P2")
  - `shapeParam3` (float, range –20–20, step 0.1, label "Shape P3")

### 2.2 Category 1 — Particle
- [ ] 2.2 Implement Particle category rows
  - `minLifetime` (float, range 0.1–30, step 0.1, label "Min Lifetime")
  - `maxLifetime` (float, range 0.1–30, step 0.1, label "Max Lifetime")
  - `initialSize` (float, range 0.001–5, step 0.005, label "Initial Size")
  - `velocityVariation` (float, range 0–20, step 0.1, label "Vel Variation")
  - `spreadAngle` (float, range 0–3.14159, step 0.01, label "Spread Angle")
  - `spawnVelocity[2]` / Z component (float, range –50–50, step 0.5, label "Velocity Z")

### 2.3 Category 2 — Color
- [ ] 2.3 Implement Color category rows (individual RGBA channels)
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
- [ ] 2.4 Implement Size category rows
  - `sizeOverLifeStart`    (float 0–5, step 0.05, label "Size Start")
  - `sizeOverLifePeak`     (float 0–5, step 0.05, label "Size Peak")
  - `sizeOverLifeEnd`      (float 0–5, step 0.05, label "Size End")
  - `sizeOverLifePeakTime` (float 0–1, step 0.01, label "Peak Time")

### 2.5 Category 4 — Physics
- [ ] 2.5 Implement Physics category rows
  - `gravity[2]` / Z component (float –100–100, step 0.5, label "Gravity Z")
  - `damping` (float 0–1, step 0.01, label "Damping")
  - `curlStrength`  (float 0–10, step 0.1, label "Curl Strength")
  - `curlFrequency` (float 0.01–5, step 0.05, label "Curl Frequency")
  - `curlSpeed`     (float 0–5, step 0.05, label "Curl Speed")
  - `groundCollision` (bool ON/OFF, label "Ground Collision")
  - `restitution`   (float 0–1, step 0.05, label "Restitution")

### 2.6 Category 5 — Forces
- [ ] 2.6 Implement Forces category rows
  - `vortexTangentialStrength` (float –20–20, step 0.5, label "Vortex Swirl")
  - `vortexRadialStrength`     (float –20–20, step 0.5, label "Vortex Radial")
  - `vortexMaxRadius`          (float 0.1–50, step 0.5, label "Vortex Radius")
  - `windStrength`  (float 0–50, step 0.5, label "Wind Strength")
  - `windDir[0]`    (float –1–1, step 0.05, label "Wind Dir X")
  - `windDir[1]`    (float –1–1, step 0.05, label "Wind Dir Y")
  - `windDir[2]`    (float –1–1, step 0.05, label "Wind Dir Z")
  - After editing windDir components, renormalize the wind direction vector

### 2.7 Category 6 — Rendering
- [ ] 2.7 Implement Rendering category rows
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
- [ ] 2.8 Implement System category rows
  - `burstMode`         (bool ON/OFF, label "Burst Mode", sets `needsRestart = true` on change)
  - `subEmitterEnabled` (bool ON/OFF, label "Sub Emitter")
  - `frustumCulling`    (bool ON/OFF, label "Frustum Cull")
  - Active particle count display (read-only uint, label "Active Particles") — reads from
    `VFXParticleSystem::GetAliveCount()` if that method exists; otherwise display `"N/A"`

---

## 3. Real-time Update Logic

### 3.1 Value adjustment step
- [ ] 3.1 On Left/Right arrow key press, apply the per-parameter step to the live
  `VFXEmitterConfig` reference (passed into `HandleInput()` by pointer); clamp to
  per-parameter min/max; mark the corresponding parameters dirty so the CB is re-uploaded
  next frame (the demo's existing `UpdateVFXCBs()` path already re-reads the config
  every frame so no extra dirty flag is needed)

### 3.2 Parameters that require restart
- [ ] 3.2 Define a `kNeedsRestart` flag for the following parameter rows:
  - `burstMode` toggle
  - `spawnShape` enum change
  When these are adjusted, set `needsRestart = true` in `HandleInput()` and display a
  yellow `"[R to restart]"` hint text in the panel's footer row

### 3.3 Restart shortcut
- [ ] 3.3 When panel is visible and `'R'` key is pressed, `HandleInput()` sets
  `needsRestart = true` and returns `true`; `VFXViewportDemo::HandleInput()` checks this
  flag after calling `m_paramPanel.HandleInput()` and triggers `m_vfxBurstPending = true`
  (or resets burst accumulator for continuous mode)

### 3.4 Boolean and enum adjustments
- [ ] 3.4 For `bool` rows: Left/Right both toggle the value (either direction flips ON↔OFF)
  For `enum` rows: Right increments mod N, Left decrements mod N (wraps)
  Display enum value as its string name from a `static const char* const kNames[]` array
  defined locally in `vfx_parameter_panel.cpp`

---

## 4. Value Display Formatting

### 4.1 Float formatting
- [ ] 4.1 Display all float parameters as `"%.2f"` (snprintf into a 32-char stack buffer)
  Append the range hint in gray: `"  [min, max]"` on the same line if the row is selected

### 4.2 Integer formatting
- [ ] 4.2 Display `uint32_t` parameters using `"%u"` format; bool as `"ON"` / `"OFF"` with
  green / gray tint respectively; enum names as plain strings

### 4.3 Color channel formatting
- [ ] 4.3 Display color channels as `"%.2f"` with a small 8×8 color swatch rect rendered
  inline next to the value using `TextRenderer::AddRect` filled with the current RGBA

### 4.4 Row color coding
- [ ] 4.4 Unselected rows: white text `{1,1,1,0.85f}`
  Selected row text: yellow `{1.0f, 0.92f, 0.23f, 1.0f}` with blue highlight rect behind
  Category headers: cyan `{0.4f, 0.85f, 1.0f, 1.0f}`
  Read-only rows: gray `{0.6f, 0.6f, 0.6f, 1.0f}`
  ON values: green `{0.3f, 1.0f, 0.4f, 1.0f}`, OFF values: gray

---

## 5. Integration into VFXViewportDemo

### 5.1 Add VFXParameterPanel member
- [ ] 5.1 In `vfx_viewport_demo.h`:
  - Add `#include "vfx_parameter_panel.h"` after existing includes
  - Add private member `VFXParameterPanel m_paramPanel;`
  - Add private member `bool m_paramPanelOpen = false;`

### 5.2 Init and Shutdown
- [ ] 5.2 In `VFXViewportDemo::OnInit()`:
  - Call `m_paramPanel.Init(device, cmdList, upload, shaderPipeline, rtvFormat, w, h)`
    immediately after the `m_statsOverlay.Init(...)` call (same arguments pattern)
  - Log success / failure with `UZ_LOG`
  In `VFXViewportDemo::OnShutdown()`:
  - Call `m_paramPanel.Shutdown()` before `m_statsOverlay.Shutdown()`

### 5.3 Per-frame rendering
- [ ] 5.3 In `VFXViewportDemo::OnFrame()`:
  - After the `m_statsOverlay.Render(...)` call, add:
    ```cpp
    m_paramPanel.Render(cmd, cbRing, m_width, m_height);
    ```
  - Pass the live references to `m_vfxEmitter`, `m_vfxSpawnRate`, `m_vfxBurstMode`,
    `m_vfxBurstCount`, `m_subEmitterEnabled`, `m_frustumCullingEnabled` into the panel
    for display (store pointers in the panel set before `Render()`)

### 5.4 Input handling
- [ ] 5.4 In `VFXViewportDemo::HandleInput()`:
  - At the VERY TOP, before any existing key checks:
    ```cpp
    bool needsRestart = false;
    if (m_paramPanel.HandleInput(input, m_vfxEmitter, m_vfxSpawnRate,
                                 m_vfxBurstMode, m_vfxBurstCount,
                                 m_subEmitterEnabled, m_frustumCullingEnabled,
                                 needsRestart)) {
        if (needsRestart) { m_vfxBurstPending = true; }
        return; // panel consumed the event
    }
    // existing key handling follows...
    ```
  - Add `'P'` key toggle for `m_paramPanel.SetVisible(!m_paramPanel.IsVisible())`
    outside the panel-active guard above

### 5.5 Resize handling
- [ ] 5.5 In `VFXViewportDemo::OnResize()`:
  - Call `m_paramPanel.SetScreenSize(width, height)` after `m_statsOverlay.SetScreenSize()`

### 5.6 On-screen hint text
- [ ] 5.6 When panel is NOT visible, add a small gray hint line rendered by the stats
  overlay or a separate `TextRenderer::AddText()` call:
  `"[P] Parameters"` at bottom-left of screen (y = screenHeight - 20)
  so users know the panel exists

---

## 6. Build System Integration

### 6.1 Add sources to build.zig
- [ ] 6.1 In `runtime/demos/vfx_viewport/build.zig`, add `vfx_parameter_panel.cpp`
  to the demo executable source list alongside `vfx_viewport_demo.cpp`

---

## 7. Visual Verification

### 7.1 Build and capture
- [ ] 7.1 Clean-build runtime + demo:
  ```bash
  cd runtime && zig build --clean && zig build
  cd demos/vfx_viewport && zig build --clean && zig build
  ```

### 7.2 Screenshot with panel visible
- [ ] 7.2 Run:
  ```bash
  demos/vfx_viewport/build/bin/vfx_viewport.exe --headless \
    --screenshot-stdout --screenshot-frame 5 --frames 8
  ```
  Verify via Read tool on the base64 screenshot:
  - Panel appears on left side with dark background
  - Category headers visible (cyan)
  - At least one expanded category showing parameter rows
  - Selected row has yellow text + blue highlight rect
  - Stats overlay still visible on right side unobstructed

### 7.3 Verify live editing
- [ ] 7.3 (Manual verification step — document in PR description)
  Launch demo interactively, press P to open panel, navigate with arrows, adjust
  `Spawn Rate`, confirm particles visibly change density without restart

---

## 8. Documentation

### 8.1 Header doc
- [ ] 8.1 `vfx_parameter_panel.h` top-level doc block covers: purpose, keyboard controls,
  integration steps, thread safety (render thread only)

### 8.2 Update demo header
- [ ] 8.2 Add `[P] Parameter panel` entry to the feature list in the `VFXViewportDemo`
  header doc block in `vfx_viewport_demo.h`
