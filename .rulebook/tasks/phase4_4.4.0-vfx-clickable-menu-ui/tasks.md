## Phase 4-4.4.0 — VFX Clickable Menu UI

Replace keyboard-only VFX controls with a clickable visual UI using the existing
TextRenderer (AddRect + AddText) and mouse input system. Renders a menu bar,
collapsible panels, buttons, sliders, color pickers, and dropdown menus.

Reference: TextRenderer::AddRect(x,y,w,h,color) + AddText(font,text,x,y,color)
Input: InputSystem::GetMouseX/Y(), IsMouseJustPressed/Down/Released(Left)
Pattern: StatsOverlay (runtime/editor/stats/stats_overlay.cpp)

## 1. VFXEditorUI Core System

- [ ] 1.1 Create `runtime/demos/vfx_viewport/vfx_editor_ui.h/.cpp` — VFXEditorUI class.
      Init(device, shaderPipeline, rtvFormat, screenW, screenH) creates TextRenderer + BitmapFont.
      BeginFrame() / Render(cmd, cbRing) / HandleMouse(input) / SetScreenSize(w,h).
- [ ] 1.2 Add hit-testing: `bool RectContains(float rx, float ry, float rw, float rh, float mx, float my)`.
      All UI elements use this for mouse interaction.
- [ ] 1.3 Add UI state tracking: hoveredElement, activeElement (being dragged), focusedPanel.
      Mouse cursor changes appearance on hover (highlight rect color).

## 2. Top Menu Bar

- [ ] 2.1 Render a horizontal menu bar at the top of the screen (full width, 30px height).
      Dark background (0.1, 0.1, 0.15, 0.95). Items: [File] [Emitter] [Modules] [Presets] [View].
- [ ] 2.2 Click on menu item → opens dropdown panel below it.
      Click elsewhere or click same item → closes dropdown.
- [ ] 2.3 **File menu**: [Restart Effect] [Save Preset] [Load Preset...] separator [Screenshot].
- [ ] 2.4 **Emitter menu**: opens the parameter panel (left side).
- [ ] 2.5 **Modules menu**: opens the module stack panel (right side).
- [ ] 2.6 **Presets menu**: shows list of all presets (built-in + saved), click to load.
- [ ] 2.7 **View menu**: [Background: Sky/Dark/Black/White] [Bloom: On/Off] [Grid: On/Off]
      [Stats: On/Off] [Fullscreen].

## 3. Left Panel — Emitter Properties (Clickable)

- [ ] 3.1 Collapsible panel on the left side (280px wide). Dark semi-transparent background.
      Title bar "Emitter Properties" with [X] close button.
- [ ] 3.2 **Category headers** — clickable to expand/collapse. [▶ Spawn] [▼ Particle] etc.
      8 categories same as parameter panel: Spawn, Particle, Color, Size, Physics, Forces, Rendering, System.
- [ ] 3.3 **Float sliders** — horizontal bar showing value. Click and drag to adjust.
      Label on left, value on right, colored fill bar proportional to value in [min,max].
      Mouse drag: `newValue = min + (mouseX - sliderLeft) / sliderWidth * (max - min)`.
- [ ] 3.4 **Bool toggles** — clickable checkbox [✓] or [  ]. Click to flip.
- [ ] 3.5 **Enum dropdowns** — show current value, click to open dropdown list.
      Items: blendMode (Alpha/Additive/Premul), spawnShape (Point/Sphere/Box/Ring/Cone/Cylinder/Disc/Line),
      alignmentMode (Camera/Velocity).
- [ ] 3.6 **Color editors** — show colored rect preview. Click to expand 4 sliders (R/G/B/A).
      Each channel is a slider 0-1 with colored fill (red bar for R, green for G, etc.).

## 4. Right Panel — Module Stack (Clickable)

- [ ] 4.1 Collapsible panel on the right side (240px wide). Shows active modules.
- [ ] 4.2 Each module is a row: [●/○] Module Name [strength value]. Click ●/○ to toggle.
- [ ] 4.3 Click module name to expand inline params (same slider UI as left panel).
- [ ] 4.4 Modules: Gravity, Damping, Curl Noise, Vortex, Wind, Ground Collision,
      Sub-Emitter, Frustum Culling, Lit Mode.

## 5. Bottom Toolbar — Quick Actions

- [ ] 5.1 Horizontal toolbar at bottom (full width, 40px). Buttons:
      [▶ Play] [⏸ Pause] [🔄 Restart] [💥 Burst] [🚀 Fireball]
      [⬅ Prev Preset] [➡ Next Preset] [Preset Name Display].
- [ ] 5.2 Play/Pause toggles continuous emission vs frozen.
- [ ] 5.3 Restart = Delete key equivalent (reset particle pool).
- [ ] 5.4 Burst = fire a burst. Fireball = launch fireball projectile.
- [ ] 5.5 Prev/Next preset buttons cycle through presets (same as Tab).

## 6. Slider Widget Implementation

- [ ] 6.1 Slider struct: `{ float x, y, width; float* valuePtr; float min, max; const char* label; }`.
- [ ] 6.2 Render: background rect (dark), fill rect (colored, width proportional to normalized value),
      label text (left), value text (right, formatted to 2 decimal places).
- [ ] 6.3 Interaction: on mouse down inside slider rect, set activeSlider. While mouse down,
      update value from mouse X position. On mouse up, clear activeSlider.
- [ ] 6.4 Step snapping: hold Ctrl while dragging for fine control (0.001 step).

## 7. Button Widget Implementation

- [ ] 7.1 Button struct: `{ float x, y, width, height; const char* label; std::function<void()> onClick; }`.
- [ ] 7.2 Render: background rect (normal color). On hover: lighter color. On press: darker color.
      Centered text label.
- [ ] 7.3 Interaction: click fires onClick callback. Visual feedback on press.

## 8. Integration

- [ ] 8.1 VFXEditorUI owns its own TextRenderer and BitmapFont (separate from StatsOverlay).
- [ ] 8.2 Rendered AFTER everything else (on top of scene + stats). Uses backbuffer render pass.
- [ ] 8.3 Mouse interaction: when UI is hovered, suppress camera/gizmo mouse input.
      `VFXEditorUI::WantsMouseInput()` returns true when mouse is over any UI element.
- [ ] 8.4 Remove conflicting hotkeys: Space (was fireball, conflicts with camera), replace with UI button.
- [ ] 8.5 Keep essential hotkeys: RMB camera look, WASD movement, Delete restart.

## 9. Visual Style

- [ ] 9.1 Dark theme: background (0.12, 0.12, 0.16, 0.92), panel (0.15, 0.15, 0.2, 0.95),
      button normal (0.25, 0.25, 0.3, 1.0), hover (0.35, 0.35, 0.4, 1.0),
      active (0.2, 0.2, 0.25, 1.0), text (0.9, 0.9, 0.9, 1.0),
      accent (0.3, 0.6, 1.0, 1.0), slider fill (0.2, 0.5, 0.9, 0.8).
- [ ] 9.2 Font size: 14px for labels, 12px for values. Use BitmapFont loaded at init.
- [ ] 9.3 Spacing: 4px padding inside buttons, 6px between elements, 2px between label and slider.

## 10. Testing

- [ ] 10.1 Screenshot with all panels open — verify layout doesn't overlap scene excessively.
- [ ] 10.2 Test slider dragging — values update in real-time on particles.
- [ ] 10.3 Test dropdown menus — blendMode, spawnShape changes take effect.
- [ ] 10.4 Test preset loading via menu — same as Tab key.
- [ ] 10.5 Test with different window sizes — panels adjust position.
