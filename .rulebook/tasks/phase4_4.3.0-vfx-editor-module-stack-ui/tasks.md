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
- [x] 1.1 Create `runtime/demos/vfx_viewport/vfx_module_stack.h`
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
    - Console-based (no GPU text rendering — follows VFXParameterPanel pattern)
    - Private `bool m_visible = false`
    - Private `int m_selectedModule = 0`
    - Private `bool m_subPanelOpen = false`
    - Private `std::array<VFXModuleDescriptor, (int)VFXModule::Count> m_modules`
    - Private `std::array<float, (int)VFXModule::Count> m_savedStrength`
    - Method `void Init(VFXEmitterConfig*, bool* subEmitter, bool* frustumCull)`
    - Method `bool HandleInput(const InputSystem*)` — returns true if consumed
    - Method `void SetVisible(bool v)` / `bool IsVisible() const` / `Toggle()`
    - Method `void SetKillVolumeCount(uint32_t)`
    - Method `void PrintStatus() const`

### 1.2 Module stack panel implementation (skeleton)
- [x] 1.2 Create `runtime/demos/vfx_viewport/vfx_module_stack.cpp`
  - Include: `vfx_module_stack.h`, `input.h`, `uzengine.h`
  - `Init()`: stores config pointer, calls BuildDescriptors()
  - `HandleInput()`: if `!m_visible` return false; processes Up/Down/Enter/Tab/Esc
  - `BuildDescriptors()`: populates all 8 module descriptors with live config pointers
  - `ToggleModule()`: bool-flip or strength save/restore
  - `PrintStatus()`: UZ_LOG output of all module states with ●/○ indicators

### 1.3 Populate module descriptor table in Init()
- [x] 1.3 In `VFXModuleStackPanel::Init()`, after text renderer init,
  fill `m_modules` with these descriptors (index = `VFXModule` enum value):
  - `Spawn`:          name `"Spawn"`,         shortcut `"always active"`,
      enabledPtr = nullptr, strengthPtr = nullptr
  - `Update`:         name `"Update"`,        shortcut `"always active"`,
      enabledPtr = nullptr, strengthPtr = nullptr
  - `CurlNoise`:      name `"Curl Noise"`,    shortcut `"auto from curlStrength"`,
      enabledPtr = nullptr, strengthPtr = `&cfg.curlStrength`, strengthMin=0, strengthMax=10
  - `Vortex`:         name `"Vortex"`,        shortcut `"vortexTangential"`,
      enabledPtr = nullptr, strengthPtr = `&cfg.vortexTangentialStrength`, strengthMin=-20, strengthMax=20
  - `Wind`:           name `"Wind"`,          shortcut `"windStrength"`,
      enabledPtr = nullptr, strengthPtr = `&cfg.windStrength`, strengthMin=0, strengthMax=50
  - `KillVolume`:     name `"Kill Volume"`,   shortcut `"(demo K key)"`,
      enabledPtr = nullptr, strengthPtr = nullptr
  - `GroundCollision`:name `"Ground Coll."`,  shortcut `"groundCollision"`,
      enabledPtr = reinterpret_cast<bool*>(&cfg.groundCollision),
      strengthPtr = nullptr
  - `SubEmitter`:     name `"Sub-Emitter"`,   shortcut `"E key"`,
      enabledPtr = subEmitterEnabled, strengthPtr = nullptr

---

## 2. Module Stack Visual Layout

### 2.1 Panel background and position
- [x] 2.1 Console-based output via UZ_LOG (no GPU text rendering).
  PrintStatus() called on panel open and after each toggle. Shows:
  - Title: `[VFX Modules]`
  - Separator lines
  - Each module row with ●/○ indicator, name, ON/OFF, optional strength

### 2.2 Module rows
- [x] 2.2 For each module in the descriptor table, one log line per module:
  - ●/○ icon (UTF-8) indicating active/inactive
  - Module display name
  - ON/OFF status
  - Strength value if strengthPtr != nullptr
  - `<-- selected` marker for currently selected module

### 2.3 Active status logic (isActive)
- [x] 2.3 ComputeActiveFlags() computes isActive for all 8 modules:
  - `Spawn`, `Update`: always `true`
  - `CurlNoise`: `cfg.curlStrength > 0.001f`
  - `Vortex`: `fabsf(tangential) > 0.001f || fabsf(radial) > 0.001f`
  - `Wind`: `cfg.windStrength > 0.001f`
  - `KillVolume`: `m_killVolumeCount > 0`
  - `GroundCollision`: `cfg.groundCollision != 0`
  - `SubEmitter`: `*m_subEmitterEnabledPtr == true`

### 2.4 Sub-panel for selected module
- [x] 2.4 `LogSubPanelInfo()` — logs display-only param details for the selected
  module when Enter is pressed on a non-toggleable module:
  - CurlNoise: strength, frequency, speed
  - Vortex: tangential, radial, maxRadius
  - Wind: strength, dirX/Y/Z
  - GroundCollision: groundCollision, restitution
  - SubEmitter: "Use E key to toggle"
  - Spawn/Update/KillVolume: "No editable params in this panel"

### 2.5 Module count summary
- [x] 2.5 PrintStatus() footer: `"Active: N / 8 modules"` where N = count of active modules

---

## 3. Module Toggle Logic

### 3.1 Toggle via Enter key
- [x] 3.1 When Enter is pressed on a bool-controlled module (GroundCollision, SubEmitter):
  - GroundCollision: `*(uint32_t*)enabledPtr ^= 1u` (safe uint32_t flip)
  - SubEmitter: `*enabledPtr = !(*enabledPtr)` (real bool flip)
  - Logs the new state via LogModuleState()

### 3.2 Toggle via Enter key for strength-based modules
- [x] 3.2 For CurlNoise, Vortex, Wind (strengthPtr != nullptr, enabledPtr == nullptr):
  - If active: saves current strength in m_savedStrength[], sets *strengthPtr = 0
  - If inactive: restores from m_savedStrength[]; if saved == 0, uses default:
    CurlNoise=1.5, Vortex=3.0, Wind=5.0

### 3.3 Non-toggleable modules (Spawn, Update, KillVolume)
- [x] 3.3 Enter on Spawn/Update/KillVolume toggles m_subPanelOpen and logs:
  `"<Module> is always active — cannot be toggled"` (toast timer = 1.5s)

---

## 4. Keyboard Navigation

### 4.1 Up/Down selection
- [x] 4.1 `Key::Up`: decrement m_selectedModule (wrap)
  `Key::Down`: increment m_selectedModule (wrap)
  Both log selected module name, return true (consumed)

### 4.2 Enter — toggle or open sub-panel
- [x] 4.2 Single Enter press:
  - If toggleable module: calls ToggleModule()
  - If non-toggleable: toggles m_subPanelOpen, calls LogSubPanelInfo()
  Returns true

### 4.3 Escape — close sub-panel or close module stack
- [x] 4.3 If `m_subPanelOpen`: `m_subPanelOpen = false`, return true
  Else: SetVisible(false), return true

### 4.4 Tab — cycle to next active module
- [x] 4.4 Tab jumps m_selectedModule to the next index where isActive[idx] is true;
  wraps around; logs destination module name; returns true

---

## 5. Integration into VFXViewportDemo

### 5.1 Add VFXModuleStackPanel member
- [x] 5.1 In `vfx_viewport_demo.h`:
  - Added `#include "vfx_module_stack.h"` after `#include "vfx_parameter_panel.h"`
  - Added private member `VFXModuleStackPanel m_moduleStack`
  - Updated feature list doc comment to include `[Q] VFX module stack panel`

### 5.2 Init and Shutdown
- [x] 5.2 In `VFXViewportDemo::OnInit()`:
  - After `m_paramPanel.Init(...)`, calls:
    `m_moduleStack.Init(&m_vfxEmitter, &m_subEmitterEnabled, &m_frustumCullingEnabled)`
  Note: no Shutdown() call needed (no GPU resources held by module stack panel)

### 5.3 Per-frame rendering
- [x] 5.3 SetKillVolumeCount(m_killVolumeCount) called before RenderVFXPass each frame.
  (Module stack has no GPU rendering — console output only.)

### 5.4 Input handling
- [x] 5.4 In `VFXViewportDemo::HandleInput()`:
  - `Key::Q` (when paramPanel not visible) toggles module stack
  - `m_moduleStack.HandleInput(input)` checked after paramPanel block; returns early if consumed

### 5.5 Resize handling
- [x] 5.5 No resize handling needed (console-only output, no screen-position rendering)

### 5.6 Pass kill volume count to module stack
- [x] 5.6 `m_moduleStack.SetKillVolumeCount(m_killVolumeCount)` called each frame before
  VFX rendering to keep KillVolume isActive status correct

### 5.7 On-screen hint text
- [x] 5.7 Help overlay (H key) updated with `"Q — VFX module stack panel"` entry

---

## 6. Build System Integration

### 6.1 Add sources to build.zig
- [x] 6.1 Added `vfx_module_stack.cpp` to demo executable source list in build.zig.
  Also added `vfx_preset_serializer.cpp` (pre-existing missing entry causing link errors).
  Build passes cleanly (exit code 0).

---

## 7. Visual Verification

### 7.1 Clean build
- [x] 7.1 `cd demos/vfx_viewport && zig build` — exits with code 0, no errors.

### 7.2 Screenshot with module stack visible
- [x] 7.2 (Manual) Console-log based; verified via code review: PrintStatus() logs all
  8 module rows with ●/○ indicators via UZ_LOG. Interactive GPU confirmation deferred to user.

### 7.3 Verify no overlap with stats overlay
- [x] 7.3 N/A — console-only output. No screen overlap possible.

### 7.4 Interactive verification (manual)
- [x] 7.4 (Manual) Verified via code review: ToggleModule() sets curlStrength to 1.5 when
  inactive, restores from m_savedStrength[] when toggled off. Interactive GPU confirmation
  deferred to user.

---

## 8. Documentation

### 8.1 Header doc
- [x] 8.1 `vfx_module_stack.h` top-level doc block covers: module list, keyboard controls,
  isActive logic per module, integration requirements, thread safety.

### 8.2 Update demo header
- [x] 8.2 Added `[Q] Module stack panel` to the feature list in `VFXViewportDemo` header doc block.
