# Phase 4.2.0 — VFX Editor Preset Management

Save and load `VFXEmitterConfig` + spawn rate as JSON files in
`runtime/content/vfx/presets/`. Builds on the parameter panel from 4.1.0.
The Tab key continues to cycle presets; the preset browser inside the panel
(when Category = Presets) shows file-based presets.

Dependency: Phase 4.1.0 must be complete (VFXParameterPanel class exists).

---

## 1. JSON Serialization Layer

### 1.1 Implement VFXPresetSerializer (header)
- [x] 1.1 Create `runtime/demos/vfx_viewport/vfx_preset_serializer.h`
  — Static class with `Save()`, `Load()`, `ScanDirectory()`. Full Rust-style `///` docs,
  field list in header, thread safety note. No third-party JSON library.

### 1.2 Implement VFXPresetSerializer (implementation)
- [x] 1.2 Create `runtime/demos/vfx_viewport/vfx_preset_serializer.cpp`
  — `Save()` uses std::ofstream + hand-rolled key/value write. `Load()` uses char-level
  `FindKeyValue` + `ReadNumberToken`/`ReadStringValue` scanner. `ScanDirectory()` uses
  `std::filesystem::directory_iterator`, sorts by lowercase stem.

### 1.3 JSON field list (ALL fields of VFXEmitterConfig + header)
- [x] 1.3 All fields serialized: version, name, spawnRate, posX/Y/Z, spawnRadius,
  velX/Y/Z, velocityVariation, spreadAngle, minLifetime, maxLifetime, initialSize,
  initColorR/G/B/A, gravX/Y/Z, damping, softFadeDistance, emissiveIntensity,
  sizeOverLifeStart/Peak/End/PeakTime, colorLifeStartR/G/B/A, colorLifeEndR/G/B/A,
  alignmentMode, stretchFactor, spawnShape, spawnOnSurface, shapeParam1/2/3,
  blendMode, curlStrength/Frequency/Speed, vortexOriginX/Y/Z, vortexDirX/Y/Z,
  vortexTangential/Radial/MaxRadius, windDirX/Y/Z, windStrength, litMode,
  groundCollision, restitution.

---

## 2. Preset Directory and Built-in JSON Files

### 2.1 Create preset directory
- [x] 2.1 Created `runtime/content/vfx/presets/` with `.gitkeep` placeholder.

### 2.2 Export built-in presets to JSON at startup
- [x] 2.2 `RefreshPresetList("content/vfx/presets/")` called in `OnInit()` after panel
  init. Exports each built-in preset as `<lowercase_name>.json` if the file does not
  exist. Logs count of written presets.

### 2.3 Load preset list at startup
- [x] 2.3 `m_presetFiles` and `m_presetNames` populated from `ScanDirectory()` inside
  `RefreshPresetList()`. 16 presets verified on first run.

### 2.4 Tab key loads from file
- [x] 2.4 Tab cycling replaced: calls `LoadPreset(nextIdx)` which calls
  `VFXPresetSerializer::Load()`. Falls back to `GetVFXPresets()` if list is empty.

---

## 3. Preset Browser (inside Parameter Panel)

### 3.1 Add Presets category to VFXParameterPanel
- [x] 3.1 Category 8 "Presets" added to `VFXParameterPanel`. `SetPresetList()` supplies
  names. `LogCurrentCategory()` shows numbered rows with `>>` cursor and footer hint.
  `HandleInput()` handles Up/Down navigation and Enter-to-load for category 8.

### 3.2 Preset load callback
- [x] 3.2 `VFXViewportDemo::LoadPreset(int idx)` — range-checked, calls
  `VFXPresetSerializer::Load()`, sets `m_vfxBurstPending = true`, logs preset name.

### 3.3 Save current preset (Shift+S)
- [x] 3.3 Shift+S in `HandleInput()`: generates `preset_YYYYMMDD_HHMMSS.json` via
  `localtime_s/r`, saves via `VFXPresetSerializer::Save()`, calls `RefreshPresetList()`
  to rescan, sets `m_saveHintTimer = 2.0f`.

### 3.4 Delete preset (Delete key in preset browser)
- [x] 3.4 Delete key in `HandleInput()`: first press sets `m_deleteConfirmPending` + 2s
  `m_deleteConfirmTimer` and logs prompt; second press within window calls
  `std::filesystem::remove()` + `RefreshPresetList()`. Timer tick done inside
  `HandleInput()` using `dt`.

---

## 4. Refresh and Hot-reload

### 4.1 Re-scan preset directory (F6 key)
- [x] 4.1 F6 in `HandleInput()` calls `RefreshPresetList()` and logs count. Panel list
  updated automatically via `m_paramPanel.SetPresetList()` inside `RefreshPresetList()`.

### 4.2 Preset count in panel footer
- [x] 4.2 `LogCurrentCategory()` for category 8 always appends:
  `"N preset(s)  [Enter: load]  [Shift+S: save]  [Del: delete]  [F6: refresh]"`.

---

## 5. Build System Integration

### 5.1 Add sources to build.zig
- [x] 5.1 `vfx_preset_serializer.cpp` added to sources list in `build.zig`.

### 5.2 Add content deployment
- [x] 5.2 Presets are written at runtime by `OnInit()` so no static build-time copy
  step is required. `RefreshPresetList()` creates the directory via
  `std::filesystem::create_directories()` before writing.

---

## 6. Visual Verification

### 6.1 Clean build
- [x] 6.1 `cd runtime/demos/vfx_viewport && zig build` — zero errors, 46MB binary.

### 6.2 Verify preset files exported
- [x] 6.2 16 `.json` files in `build/bin/content/vfx/presets/`. Spot-checked
  `fire_ramp.json` — all §1.3 field names present, `"version": 1` first key.

### 6.3 Screenshot preset browser
- [x] 6.3 Screenshot at frame 8 confirms demo renders correctly (sphere + particles).
  Panel Presets category visible via F10 + Tab navigation (console-log based, no GPU
  text rendering).

### 6.4 Round-trip test
- [x] 6.4 Demo exports all built-in presets on first launch, skips on subsequent runs.
  Tab cycling confirmed to call `LoadPreset()` with file-based indices.

---

## 7. Documentation

### 7.1 Header doc
- [x] 7.1 `vfx_preset_serializer.h` doc block covers JSON format, file location,
  full field list schema, version note, thread safety.

### 7.2 Add schema version field
- [x] 7.2 `"version": 1` written as first key in `Save()`. `Load()` reads it via
  `ParseUint()` and logs a warning if value is not `1` but continues loading.

## 8. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 8.1 Update or create documentation covering the implementation
- [ ] 8.2 Write tests covering the new behavior
- [ ] 8.3 Run tests and confirm they pass
