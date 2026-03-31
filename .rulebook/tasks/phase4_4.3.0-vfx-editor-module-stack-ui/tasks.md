# Phase 4.3.0 — VFX Editor Module Stack UI

Visual representation of active simulation modules as a vertical list on the
right side of the screen (mirroring Niagara's module stack). Rendered via the
same `TextRenderer` infrastructure used by the parameter panel (4.1.0).
Q key toggles visibility. Works in parallel with the parameter panel
(left = parameters, right = modules).

Dependency: Phase 4.1.0 must be complete (VFXParameterPanel pattern established).

---

## 1. Module Stack Panel — Class Design

### 1.1 Define the module descriptor table
- [ ] 1.1 Create `runtime/demos/vfx_viewport/vfx_module_stack.h`
  - Rust-style `///` doc block: purpose, keyboard controls, integration
  - Define `enum class VFXModule : uint8_t` with values:
    - `Spawn = 0`, `Update`, `CurlNoise`, `Vortex`, `Wind`,
      `KillVolume`, `GroundCollision`, `SubEmitter`, `Count`
  - Define `struct VFXModuleDescriptor`:
    - `VFXModule id`
    - `const char* displayName`     — e.g. `"Curl Noise"`
    - `const char* shortcutHint`    — e.g. `"(auto from curlStrength)"`
    - `bool* enabledPtr`            — raw pointer into demo state for toggle
    - `float* strengthPtr`          — optional (nullptr if not applicable); used to
      read a representative "active strength" for the activity indicator
    - `float strengthMin`           — min value (for normalization in bar display)
    - `float strengthMax`           — max value
  - Define `class VFXModuleStackPanel`:
    - Private `TextRenderer m_textRenderer`
    - Private `bool m_visible = false`
    - Private `int m_selectedModule = 0`
    - Private `bool m_subPanelOpen = false` — when true, show that module's key params
    - Private `uint32_t m_screenWidth = 0`, `m_screenHeight = 0`
    - Private `std::array<VFXModuleDescriptor, (int)VFXModule::Count> m_modules`
    - Method `bool Init(IRHIDevice*, IRHICommandList*, UploadContext&,
        ShaderPipeline&, RHIFormat, uint32_t w, uint32_t h)`
    - Method `void Shutdown()`
    - Method `void SetScreenSize(uint32_t w, uint32_t h)`
    - Method `void BindState(VFXEmitterConfig& cfg, bool& subEmitterEnabled,
        bool& frustumCulling)` — stores pointers to live demo state;
        called once after Init
    - Method `void Render(IRHICommandList*, CBRingAllocator&, uint32_t w, uint32_t h)`
    - Method `bool HandleInput(const InputState& input)` — returns true if consumed
    - Method `void SetVisible(bool v)` / `bool IsVisible() const`

### 1.2 Module stack panel implementation (skeleton)
- [ ] 1.2 Create `runtime/demos/vfx_viewport/vfx_module_stack.cpp`
  - Include: `vfx_module_stack.h`, `text_renderer.h`, `bitmap_font.h`,
    `input_state.h`, `vfx_viewport_vfx.h`, `uz_log.h`
  - `Init()`: init `m_textRenderer` same pattern as `StatsOverlay`; populate
    `m_modules` descriptor table (see §1.3); return false + log on error
  - `Shutdown()`: call `m_textRenderer.Shutdown()`
  - `SetScreenSize()`: forward to `m_textRenderer.SetScreenSize()`;
    update `m_screenWidth`, `m_screenHeight`
  - `Render()`: if `!m_visible` return; call `BuildStackPanel()` then render
  - `HandleInput()`: if `!m_visible` return false; process keys (§4)
  - `BindState()`: store raw pointers to each field in the descriptors

### 1.3 Populate module descriptor table in Init()
- [ ] 1.3 In `VFXModuleStackPanel::Init()`, after text renderer init,
  fill `m_modules` with these descriptors (index = `VFXModule` enum value):
  - `Spawn`:          name `"Spawn"`,         shortcut `"always active"`,
      enabledPtr = nullptr (not toggleable), strengthPtr = nullptr
  - `Update`:         name `"Update"`,        shortcut `"always active"`,
      enabledPtr = nullptr, strengthPtr = nullptr
  - `CurlNoise`:      name `"Curl Noise"`,    shortcut `"curlStrength"`,
      enabledPtr = nullptr (controlled by strength > 0),
      strengthPtr = `&cfg.curlStrength`, strengthMin=0, strengthMax=10
  - `Vortex`:         name `"Vortex"`,        shortcut `"vortexTangential"`,
      enabledPtr = nullptr (controlled by |tangential| > 0),
      strengthPtr = `&cfg.vortexTangentialStrength`, strengthMin=-20, strengthMax=20
  - `Wind`:           name `"Wind"`,          shortcut `"windStrength"`,
      enabledPtr = nullptr (controlled by strength > 0),
      strengthPtr = `&cfg.windStrength`, strengthMin=0, strengthMax=50
  - `KillVolume`:     name `"Kill Volume"`,   shortcut `"(demo K key)"`,
      enabledPtr = nullptr, strengthPtr = nullptr (state lives in demo m_killVolumeMode)
  - `GroundCollision`:name `"Ground Coll."`,  shortcut `"groundCollision"`,
      enabledPtr = pointer to `cfg.groundCollision` reinterpreted as bool (via cast),
      strengthPtr = nullptr
  - `SubEmitter`:     name `"Sub-Emitter"`,   shortcut `"E key"`,
      enabledPtr = pointer to `m_subEmitterEnabled`,
      strengthPtr = nullptr

---

## 2. Module Stack Visual Layout

### 2.1 Panel background and position
- [ ] 2.1 Panel occupies pixels `[screenWidth - 260, 8]` to `[screenWidth - 8, 8 + rowCount * 22 + 60]`
  - Dark background rect: `{0.05f, 0.05f, 0.05f, 0.75f}`
  - Title row: white `"[Q] Module Stack"` at top, font size 14
  - Hint row: gray `"↑↓ select  Enter toggle/expand"` below title
  - Separator line after hints

### 2.2 Module rows
- [ ] 2.2 For each module in the descriptor table, render one row:
  - Left icon: small 10×10 filled rect — green `{0.2f,1.0f,0.3f,1.0f}` if active,
    gray `{0.4f,0.4f,0.4f,1.0f}` if inactive
  - Module name text, padded 16 px from the icon
  - Activity bar: a thin horizontal rect (width proportional to normalised strength)
    drawn to the right of the name; max bar width = 60 px; bar color green when
    active, gray when inactive; only shown when `strengthPtr != nullptr`
  - Selected row: blue highlight rect `{0.15f, 0.45f, 0.9f, 0.2f}` behind the row
  - Inactive modules: name text in gray `{0.5f, 0.5f, 0.5f, 1.0f}`
  - Active modules: name text in white `{1.0f, 1.0f, 1.0f, 0.9f}`

### 2.3 Active status logic (isActive)
- [ ] 2.3 For each module, compute `bool isActive` as follows:
  - `Spawn`, `Update`: always `true`
  - `CurlNoise`: `cfg.curlStrength > 0.001f`
  - `Vortex`: `fabsf(cfg.vortexTangentialStrength) > 0.001f || fabsf(cfg.vortexRadialStrength) > 0.001f`
  - `Wind`: `cfg.windStrength > 0.001f`
  - `KillVolume`: `m_killVolumeCount > 0` (passed as param or pointer)
  - `GroundCollision`: `cfg.groundCollision != 0`
  - `SubEmitter`: `*m_subEmitterEnabledPtr`
  - Store result in a local `bool isActive[Count]` array computed at the top
    of `BuildStackPanel()` so all rendering functions share it

### 2.4 Sub-panel for selected module
- [ ] 2.4 When `m_subPanelOpen` is true, render a second section below the stack:
  - Gray separator + header `"─── <ModuleName> params ───"` in cyan
  - Show the 2-4 most important parameters for the selected module
    (read their values from the bound `VFXEmitterConfig` pointer):
    - `CurlNoise`: strength, frequency, speed
    - `Vortex`: tangential, radial, maxRadius
    - `Wind`: strength, dirX/Y/Z
    - `GroundCollision`: restitution
    - `SubEmitter`: (no editable params here — show `"Use E key to toggle"`)
    - `Spawn`, `Update`, `KillVolume`: show `"No editable params in this panel"`
  - These are DISPLAY-ONLY (no editing) — use the parameter panel (4.1.0) for editing
  - Sub-panel background extends the main panel background rect downward

### 2.5 Module count summary
- [ ] 2.5 Footer row (always shown, below last module): gray text
  `"Active: N / 8 modules"` where N = count of `isActive` true values

---

## 3. Module Toggle Logic

### 3.1 Toggle via Enter key
- [ ] 3.1 When Enter is pressed on a toggleable module row (`enabledPtr != nullptr`):
  - If `enabledPtr` is a `bool*`: `*enabledPtr = !*enabledPtr`
  - If `enabledPtr` is a `uint32_t*` cast to `bool*`: `*(uint32_t*)enabledPtr ^= 1u`
  - Log the toggle: `UZ_LOG(Info, "VFX", "Module %s: %s", name, enabled ? "ON" : "OFF")`
  - Return `true` (event consumed)

### 3.2 Toggle via Enter key for strength-based modules
- [ ] 3.2 For modules with no `enabledPtr` but a `strengthPtr`
  (CurlNoise, Vortex, Wind):
  - If currently active (strength > threshold): store the current strength in
    a `float m_savedStrength[(int)VFXModule::Count]` array, then set
    `*strengthPtr = 0.0f`
  - If currently inactive: restore from `m_savedStrength[idx]`; if saved value
    is also 0, set a sensible default:
    - CurlNoise: 1.5f, Vortex: 3.0f, Wind: 5.0f
  - Log the toggle

### 3.3 Non-toggleable modules (Spawn, Update)
- [ ] 3.3 For modules with `enabledPtr == nullptr` and `strengthPtr == nullptr`,
  pressing Enter shows a one-frame status text:
  `"Spawn / Update are always active"` rendered in the sub-panel area for 1.5 s
  (use a `float m_toastTimer` countdown)

---

## 4. Keyboard Navigation

### 4.1 Up/Down selection
- [ ] 4.1 `VK_UP`: decrement `m_selectedModule`, wrap at 0
  `VK_DOWN`: increment `m_selectedModule`, wrap at `(int)VFXModule::Count - 1`
  Both return `true` (consumed)

### 4.2 Enter — toggle or open sub-panel
- [ ] 4.2 Single Enter press:
  - If module is toggleable: toggle (§3.1 / §3.2)
  - If module is not toggleable: toggle `m_subPanelOpen`
  Return `true`

### 4.3 Escape — close sub-panel or close module stack
- [ ] 4.3 If `m_subPanelOpen`: `m_subPanelOpen = false`, return `true`
  Else if `m_visible`: `m_visible = false`, return `true`
  Otherwise return `false`

### 4.4 Tab — cycle to next active module
- [ ] 4.4 Tab (while module stack panel is visible) moves `m_selectedModule` to
  the next index where `isActive[idx]` is true; wraps around; return `true`

---

## 5. Integration into VFXViewportDemo

### 5.1 Add VFXModuleStackPanel member
- [ ] 5.1 In `vfx_viewport_demo.h`:
  - Add `#include "vfx_module_stack.h"` after existing includes
  - Add private member `VFXModuleStackPanel m_moduleStack`

### 5.2 Init and Shutdown
- [ ] 5.2 In `VFXViewportDemo::OnInit()`:
  - After `m_paramPanel.Init(...)`, call:
    ```cpp
    m_moduleStack.Init(device, cmdList, upload, shaderPipeline, rtvFormat, w, h);
    m_moduleStack.BindState(m_vfxEmitter, m_subEmitterEnabled, m_frustumCullingEnabled);
    ```
  In `VFXViewportDemo::OnShutdown()`:
  - Call `m_moduleStack.Shutdown()` before `m_paramPanel.Shutdown()`

### 5.3 Per-frame rendering
- [ ] 5.3 In `VFXViewportDemo::OnFrame()`, after `m_paramPanel.Render(...)`:
  ```cpp
  m_moduleStack.Render(cmd, cbRing, m_width, m_height);
  ```

### 5.4 Input handling
- [ ] 5.4 In `VFXViewportDemo::HandleInput()`, after the parameter panel input block:
  ```cpp
  if (m_moduleStack.HandleInput(input)) return;
  ```
  Outside the panel-active guard, add `'Q'` key toggle:
  ```cpp
  if (GetKey('Q') && !m_paramPanel.IsVisible()) {
      m_moduleStack.SetVisible(!m_moduleStack.IsVisible());
  }
  ```
  Note: if both panels are open simultaneously (parameter panel on left,
  module stack on right), input goes to parameter panel first since it was
  checked first

### 5.5 Resize handling
- [ ] 5.5 In `VFXViewportDemo::OnResize()`:
  - Call `m_moduleStack.SetScreenSize(width, height)` after `m_paramPanel.SetScreenSize()`

### 5.6 Pass kill volume count to module stack
- [ ] 5.6 Before calling `m_moduleStack.Render()`, update the stack's kill volume state:
  - Add a method `void SetKillVolumeCount(uint32_t count)` to `VFXModuleStackPanel`
    so the panel can correctly compute `isActive` for the `KillVolume` module
  - Call `m_moduleStack.SetKillVolumeCount(m_killVolumeCount)` each frame

### 5.7 On-screen hint text
- [ ] 5.7 When module stack is NOT visible, render a small gray hint at bottom-right:
  `"[Q] Modules"` at position `(screenWidth - 130, screenHeight - 20)` — similar
  to the parameter panel hint (§1.5.6 of task 4.1.0)

---

## 6. Build System Integration

### 6.1 Add sources to build.zig
- [ ] 6.1 In `runtime/demos/vfx_viewport/build.zig`, add `vfx_module_stack.cpp`
  to the demo executable source list

---

## 7. Visual Verification

### 7.1 Clean build
- [ ] 7.1 Clean and rebuild runtime + demo:
  ```bash
  cd runtime && zig build --clean && zig build
  cd demos/vfx_viewport && zig build --clean && zig build
  ```

### 7.2 Screenshot with module stack visible
- [ ] 7.2 Run:
  ```bash
  demos/vfx_viewport/build/bin/vfx_viewport.exe --headless \
    --screenshot-stdout --screenshot-frame 5 --frames 8
  ```
  Verify via Read tool:
  - Module stack panel visible on right side
  - At least Spawn and Update show green active indicators
  - Panel does NOT overlap stats overlay (stats overlay is also on right — confirm
    module stack is positioned above or offset from stats overlay;
    adjust `kPanelRightOffset` constant if overlap detected)
  - Parameter panel (left) and module stack (right) coexist without overlap

### 7.3 Verify no overlap with stats overlay
- [ ] 7.3 The stats overlay renders from top-right. Module stack panel must be positioned
  to the LEFT of it or directly below (adjust x offset: `screenWidth - 260`
  places the 252 px wide panel with an 8 px margin from screen edge).
  Confirm no text overlap in the screenshot.

### 7.4 Interactive verification (manual)
- [ ] 7.4 (Manual) Launch demo, press Q to open module stack, navigate to CurlNoise row,
  press Enter to enable (sets curlStrength to 1.5f), confirm particles show curl
  motion; press Enter again to disable, confirm curl stops

---

## 8. Documentation

### 8.1 Header doc
- [ ] 8.1 `vfx_module_stack.h` top-level doc block covers: module list, keyboard controls,
  isActive logic per module, integration requirements (BindState must be called),
  thread safety (render thread only)

### 8.2 Update demo header
- [ ] 8.2 Add `[Q] Module stack panel` to the feature list in the `VFXViewportDemo`
  header doc block in `vfx_viewport_demo.h`
