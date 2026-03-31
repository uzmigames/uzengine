## Phase 4-4.4.0 — VFX Clickable Menu UI

Replace keyboard-only VFX controls with a clickable visual UI using the existing
TextRenderer (AddRect + AddText) and mouse input system. Renders a menu bar,
collapsible panels, buttons, sliders, color pickers, and dropdown menus.

Reference: TextRenderer::AddRect(x,y,w,h,color) + AddText(font,text,x,y,color)
Input: InputSystem::GetMouseX/Y(), IsMouseJustPressed/Down/Released(Left)
Pattern: StatsOverlay (runtime/editor/stats/stats_overlay.cpp)

## 1. VFXEditorUI Core System

- [x] 1.1 Create `runtime/demos/vfx_viewport/vfx_editor_ui.h/.cpp` — VFXEditorUI class.
      Init(device, cmd, upload, shaderPipeline, rtvFormat, screenW, screenH, bindless) creates TextRenderer + BitmapFont.
      BeginFrame() / Render(cmd, cbRing) / HandleMouse(input) / BuildUI() / SetScreenSize(w,h).
      Integrated into demo: OnInit (with UploadContext flush), OnFrame (render pass after stats), OnResize, OnShutdown, HandleInput.
- [x] 1.2 Add hit-testing: `bool HitTest(float x, float y, float w, float h)` member function.
      All UI elements use this for mouse interaction.
- [x] 1.3 Add UI state tracking: m_openMenu, m_dragSliderPtr, m_dropdownPtr, m_leftPanelOpen, m_rightPanelOpen.
      Mouse hover highlights rect color on buttons, sliders, menu items, dropdowns.

## 2. Top Menu Bar

- [x] 2.1 Render a horizontal menu bar at the top of the screen (full width, 30px height).
      Dark background (0.12, 0.12, 0.16, 0.95). Items: [File] [Emitter] [Modules] [Presets] [View].
- [x] 2.2 Click on menu item → opens dropdown panel below it.
      Click elsewhere or click same item → closes dropdown. Hover-open adjacent menus.
- [x] 2.3 **File menu**: [Restart Effect] [Burst] [Fireball] [Screenshot].
- [x] 2.4 **Emitter menu**: toggles left properties panel.
- [x] 2.5 **Modules menu**: toggles right modules panel.
- [x] 2.6 **Presets menu**: shows list of all presets, click to load via onLoadPreset callback.
- [x] 2.7 **View menu**: [Toggle Bloom] [Toggle Grid] [Toggle Stats] [Toggle Panels].

## 3. Left Panel — Emitter Properties (Clickable)

- [x] 3.1 Collapsible panel on the left side (280px wide). Dark semi-transparent background.
      Title bar "Emitter Properties" with [X] close button.
- [x] 3.2 **Category headers** — clickable to expand/collapse. [+]/[-] prefix.
      7 categories: Spawn, Particle, Color, Size Over Life, Physics, Forces, Rendering.
- [x] 3.3 **Float sliders** — horizontal bar showing value. Click and drag to adjust.
      Label on left, value on right, colored fill bar proportional to value in [min,max].
      Drag interaction via m_dragSliderPtr state.
- [x] 3.4 **Bool toggles** — clickable checkbox with accent-colored inner rect. Click to flip.
- [x] 3.5 **Enum dropdowns** — show current value, click to open dropdown overlay list.
      Items: blendMode, spawnShape, spawnOnSurface, groundCollision, alignmentMode, litMode.
- [x] 3.6 **Color editors** — 8 individual R/G/B/A sliders for start and end color over lifetime.

## 4. Right Panel — Module Stack (Clickable)

- [x] 4.1 Collapsible panel on the right side (240px wide). Shows active modules.
- [x] 4.2 Each module is a row: Module Name label + inline slider for strength parameter.
- [x] 4.3 Inline sliders directly visible (no expand/collapse per module — cleaner UX).
- [x] 4.4 Modules: Gravity Z, Damping, Curl Noise, Vortex Swirl, Vortex Radial, Wind,
      Restitution, Emissive, Soft Fade.

## 5. Bottom Toolbar — Quick Actions

- [x] 5.1 Horizontal toolbar at bottom (full width, 40px). Buttons:
      [VFX: ON/OFF] [Restart] [Burst] [Fireball] [Mode:Burst/Cont]
      [< Prev] [> Next] [Props] [Modules].
- [x] 5.2 VFX On/Off toggles m_vfxEnabled pointer.
- [x] 5.3 Restart button fires onRestart callback (resets particle pool + free list).
- [x] 5.4 Burst = fire burst via onBurst. Fireball = launch projectile via onFireball.
- [x] 5.5 Prev/Next preset buttons cycle through presets via onLoadPreset callback.

## 6. Slider Widget Implementation

- [x] 6.1 Slider as `bool Slider(x, y, w, float*, min, max, label)` method with drag state.
- [x] 6.2 Render: background rect (dark), fill rect (colored, width proportional to normalized value),
      label text (left), value text (right, formatted to 2 decimal places).
- [x] 6.3 Interaction: on mouse down inside slider rect, set m_dragSliderPtr. While mouse down,
      update value from mouse X position. On mouse up, clear m_dragSliderPtr.
- [ ] 6.4 Step snapping: hold Ctrl while dragging for fine control (0.001 step).

## 7. Button Widget Implementation

- [x] 7.1 Button as `bool Button(x, y, w, h, label)` method returning true on click.
- [x] 7.2 Render: background rect (normal color). On hover: lighter color. On press: darker color.
      Left-aligned text label with padding.
- [x] 7.3 Interaction: returns true on mouseJustPressed when hovered. Visual feedback on press.

## 8. Integration

- [x] 8.1 VFXEditorUI owns its own TextRenderer and BitmapFont (separate from StatsOverlay).
      Init creates UploadContext, uploads font atlas, flushes, re-opens cmdList.
- [x] 8.2 Rendered AFTER stats overlay (on top of scene + stats). Uses backbuffer render pass.
- [x] 8.3 Mouse interaction: HandleMouse() at top of HandleInput(). WantsMouseInput() returns
      true when mouse is over menu bar, toolbar, panels, or dropdown.
- [ ] 8.4 Remove conflicting hotkeys: Space (was fireball, conflicts with camera), replace with UI button.
- [x] 8.5 Keep essential hotkeys: RMB camera look, WASD movement, Delete restart.

## 9. Visual Style

- [x] 9.1 Dark theme: all colors defined as static constexpr Vec4 members (kColorBarBg, kColorPanelBg,
      kColorBtnNormal, kColorBtnHover, kColorBtnPress, kColorText, kColorAccent, kColorSliderBg,
      kColorSliderFill, kColorCatHeader, kColorMenuHover, kColorMenuActive, kColorDropBg).
- [x] 9.2 Font size: 14px Consolas (same as StatsOverlay). Single BitmapFont at init.
- [x] 9.3 Spacing: 8px button padding, 6px panel padding, 2px gap between sliders, 4px panel top padding.

## 10. Testing

- [ ] 10.1 Screenshot with all panels open — verify layout doesn't overlap scene excessively.
- [ ] 10.2 Test slider dragging — values update in real-time on particles.
- [ ] 10.3 Test dropdown menus — blendMode, spawnShape changes take effect.
- [ ] 10.4 Test preset loading via menu — same as Tab key.
- [ ] 10.5 Test with different window sizes — panels adjust position.
