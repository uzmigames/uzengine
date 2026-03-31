# Phase 4.2.0 — VFX Editor Preset Management

Save and load `VFXEmitterConfig` + spawn rate as JSON files in
`runtime/content/vfx/presets/`. Builds on the parameter panel from 4.1.0.
The Tab key continues to cycle presets; the preset browser inside the panel
(when Category = Presets) shows file-based presets.

Dependency: Phase 4.1.0 must be complete (VFXParameterPanel class exists).

---

## 1. JSON Serialization Layer

### 1.1 Implement VFXPresetSerializer (header)
- [ ] 1.1 Create `runtime/demos/vfx_viewport/vfx_preset_serializer.h`
  - Rust-style `///` doc block: purpose, file format, thread safety
  - Static helper class (no instances needed) with only static methods:
    - `static bool Save(const VFXEmitterConfig& cfg, float spawnRate,
        const std::string& name, const std::string& filePath)`
    - `static bool Load(const std::string& filePath, VFXEmitterConfig& outCfg,
        float& outSpawnRate, std::string& outName)`
    - `static std::vector<std::string> ScanDirectory(const std::string& dir)`
      — returns sorted list of absolute `.json` file paths
  - No third-party JSON library — use a hand-written minimal serializer (see §1.3)

### 1.2 Implement VFXPresetSerializer (implementation)
- [ ] 1.2 Create `runtime/demos/vfx_viewport/vfx_preset_serializer.cpp`
  - Include: `vfx_preset_serializer.h`, `vfx_viewport_vfx.h`, `<fstream>`,
    `<sstream>`, `<filesystem>`, `<algorithm>`
  - `Save()`:
    - Open file with `std::ofstream`; write opening `{` on its own line
    - Serialize header fields first: `"name"`, `"spawnRate"` as `float`
    - Serialize every field of `VFXEmitterConfig` in declaration order
      (see field list in §1.3); use `"  \"key\": value,\n"` format
    - Last field has no trailing comma
    - Write closing `}` and flush; return `false` + `UZ_LOG(Warning)` on any I/O error
  - `Load()`:
    - Open file; read entire content into `std::string`
    - Parse with a hand-rolled key-value scanner (no regex, no JSON lib):
      scan for `"key"` then `:` then the value token (number/string/true/false)
    - For string values strip surrounding `"` quotes
    - For bool values: `"true"` → `1`, `"false"` → `0`
    - Apply each parsed value to the corresponding `VFXEmitterConfig` field
    - Unknown keys are silently ignored (forward-compat)
    - Return `false` + `UZ_LOG(Warning)` on file-not-found or parse error
  - `ScanDirectory()`:
    - Use `std::filesystem::directory_iterator`
    - Collect all paths with `.json` extension
    - Sort alphabetically by filename stem
    - Return sorted vector

### 1.3 JSON field list (ALL fields of VFXEmitterConfig + header)
- [ ] 1.3 The serialized JSON document contains exactly these keys, in this order:
  - `"name"` (string)
  - `"spawnRate"` (float)
  - `"posX"`, `"posY"`, `"posZ"` (position[3])
  - `"spawnRadius"` (float)
  - `"velX"`, `"velY"`, `"velZ"` (spawnVelocity[3])
  - `"velocityVariation"`, `"spreadAngle"` (float)
  - `"minLifetime"`, `"maxLifetime"` (float)
  - `"initialSize"` (float)
  - `"initColorR"`, `"initColorG"`, `"initColorB"`, `"initColorA"` (initialColor[4])
  - `"gravX"`, `"gravY"`, `"gravZ"` (gravity[3])
  - `"damping"` (float)
  - `"softFadeDistance"`, `"emissiveIntensity"` (float)
  - `"sizeOverLifeStart"`, `"sizeOverLifePeak"`, `"sizeOverLifeEnd"`,
    `"sizeOverLifePeakTime"` (float)
  - `"colorLifeStartR"`, `"colorLifeStartG"`, `"colorLifeStartB"`, `"colorLifeStartA"` (float)
  - `"colorLifeEndR"`, `"colorLifeEndG"`, `"colorLifeEndB"`, `"colorLifeEndA"` (float)
  - `"alignmentMode"`, `"stretchFactor"` (uint/float)
  - `"spawnShape"`, `"spawnOnSurface"` (uint)
  - `"shapeParam1"`, `"shapeParam2"`, `"shapeParam3"` (float)
  - `"blendMode"` (uint)
  - `"curlStrength"`, `"curlFrequency"`, `"curlSpeed"` (float)
  - `"vortexOriginX"`, `"vortexOriginY"`, `"vortexOriginZ"` (float)
  - `"vortexDirX"`, `"vortexDirY"`, `"vortexDirZ"` (float)
  - `"vortexTangential"`, `"vortexRadial"`, `"vortexMaxRadius"` (float)
  - `"windDirX"`, `"windDirY"`, `"windDirZ"` (float)
  - `"windStrength"` (float)
  - `"litMode"` (uint)
  - `"groundCollision"` (uint)
  - `"restitution"` (float)

---

## 2. Preset Directory and Built-in JSON Files

### 2.1 Create preset directory
- [ ] 2.1 Create `runtime/content/vfx/presets/` directory (add a `.gitkeep` placeholder
  so the directory is tracked by git even when empty)

### 2.2 Export built-in presets to JSON at startup
- [ ] 2.2 In `VFXViewportDemo::OnInit()`, after loading all resources:
  - Enumerate `GetVFXPresets(count)` and for each preset:
    - Compute path `"content/vfx/presets/<name>.json"` (lowercase, spaces replaced with `_`)
    - If the file does NOT already exist, call `VFXPresetSerializer::Save()` to write it
    - This ensures the directory is populated on first run without overwriting user edits
  - Log the count of presets written with `UZ_LOG(Info, "VFX", "Wrote %d built-in presets", n)`

### 2.3 Load preset list at startup
- [ ] 2.3 In `VFXViewportDemo::OnInit()`, after §2.2:
  - Call `VFXPresetSerializer::ScanDirectory("content/vfx/presets/")` and store
    result in `std::vector<std::string> m_presetFiles` (new demo member)
  - Also store `std::vector<std::string> m_presetNames` — just the filename stems
    (strip directory and `.json` extension) for display
  - Update `m_vfxPresetIndex` clamp to `m_presetFiles.size()` instead of the
    old hardcoded `s_presets` count

### 2.4 Tab key loads from file
- [ ] 2.4 Replace the Tab-key preset cycling in `VFXViewportDemo::HandleInput()`:
  - Old: `m_vfxEmitter = s_presets[idx].config; m_vfxSpawnRate = s_presets[idx].spawnRate`
  - New: call `VFXPresetSerializer::Load(m_presetFiles[idx], m_vfxEmitter,
    m_vfxSpawnRate, name)` and set `m_vfxBurstPending = true`
  - If the presetFiles list is empty, fall back to the old `GetVFXPresets()` path

---

## 3. Preset Browser (inside Parameter Panel)

### 3.1 Add Presets category to VFXParameterPanel
- [ ] 3.1 Add Category 8 "Presets" to `VFXParameterPanel`:
  - Requires `VFXParameterPanel` to hold a `const std::vector<std::string>* m_presetNames`
    pointer set via `SetPresetList(const std::vector<std::string>& names)`
  - When expanded, shows one row per preset name (read-only text rows, not value-edit rows)
  - Selected preset row highlighted in yellow (same as parameter rows)
  - `Enter` on a preset row triggers a load callback: call `m_onPresetLoad(idx)` which
    is a `std::function<void(int)>` set by the demo via
    `m_paramPanel.SetPresetLoadCallback([this](int i){ LoadPreset(i); })`

### 3.2 Preset load callback
- [ ] 3.2 Add `void VFXViewportDemo::LoadPreset(int idx)`:
  - If `idx < 0 || idx >= (int)m_presetFiles.size()` return immediately with warning log
  - Call `VFXPresetSerializer::Load(m_presetFiles[idx], m_vfxEmitter,
    m_vfxSpawnRate, name)`
  - Set `m_vfxBurstPending = true`
  - Log: `UZ_LOG(Info, "VFX", "Loaded preset: %s", name.c_str())`

### 3.3 Save current preset (Shift+S)
- [ ] 3.3 In `VFXParameterPanel::HandleInput()` (or `VFXViewportDemo::HandleInput()`),
  when panel is visible and `Shift+S` is pressed:
  - Generate filename from current timestamp: `snprintf(buf, "preset_%Y%m%d_%H%M%S.json")`
    using `<ctime>`
  - Build full path `"content/vfx/presets/" + buf`
  - Show a one-line confirmation overlay: `"Saving preset_YYYYMMDD_HHMMSS.json..."`
    displayed for 2 seconds (use a `float m_saveHintTimer` countdown in the panel)
  - Call `VFXPresetSerializer::Save(m_vfxEmitter, m_vfxSpawnRate, buf_stem, fullPath)`
  - After save, call `VFXPresetSerializer::ScanDirectory()` again to refresh
    `m_presetFiles` and `m_presetNames` in the demo, then call
    `m_paramPanel.SetPresetList(m_presetNames)` to update the browser list

### 3.4 Delete preset (Delete key in preset browser)
- [ ] 3.4 When Category 8 is selected, panel is visible, and `VK_DELETE` is pressed:
  - Show a one-frame confirmation prompt: `"Delete <name>? Press Del again to confirm"`
    stored in `bool m_deleteConfirmPending` + `int m_deleteConfirmIdx`
  - If `VK_DELETE` pressed again on the same row within 2 seconds:
    - Call `std::filesystem::remove(m_presetFiles[idx])`
    - Log deletion with `UZ_LOG(Info, "VFX", "Deleted preset: %s", path)`
    - Refresh preset list (same pattern as §3.3)
    - Clear `m_deleteConfirmPending`
  - If a different key is pressed, cancel the confirmation

---

## 4. Refresh and Hot-reload

### 4.1 Re-scan preset directory (F6 key)
- [ ] 4.1 When `F6` is pressed (not captured by panel), call
  `VFXPresetSerializer::ScanDirectory()` to refresh the file list in the demo
  and `m_paramPanel.SetPresetList(m_presetNames)`; display a 1-second toast:
  `"Presets refreshed: N found"` via the stats overlay hint text

### 4.2 Preset count in panel footer
- [ ] 4.2 At the bottom of the Presets category (always visible, even when collapsed),
  show a gray row: `"N presets  [Shift+S save]  [Del delete]  [F6 refresh]"`

---

## 5. Build System Integration

### 5.1 Add sources to build.zig
- [ ] 5.1 In `runtime/demos/vfx_viewport/build.zig`, add `vfx_preset_serializer.cpp`
  to the demo executable source list

### 5.2 Add content deployment
- [ ] 5.2 In `runtime/demos/vfx_viewport/build.zig`, ensure the content install step
  includes the presets directory:
  ```zig
  .{ .source = "../../content/vfx/presets", .dest = "content/vfx/presets" },
  ```
  so built-in `.json` files are copied to the binary output directory at build time

---

## 6. Visual Verification

### 6.1 Clean build
- [ ] 6.1 Clean and rebuild runtime + demo:
  ```bash
  cd runtime && zig build --clean && zig build
  cd demos/vfx_viewport && zig build --clean && zig build
  ```

### 6.2 Verify preset files exported
- [ ] 6.2 After first launch, check `demos/vfx_viewport/build/bin/content/vfx/presets/`
  contains at least one `.json` file per built-in preset; spot-check one file's JSON
  content via `Read` tool to confirm field names match §1.3

### 6.3 Screenshot preset browser
- [ ] 6.3 Run:
  ```bash
  demos/vfx_viewport/build/bin/vfx_viewport.exe --headless \
    --screenshot-stdout --screenshot-frame 5 --frames 8
  ```
  Confirm (via Read tool on screenshot): panel visible with Presets category showing
  preset names, preset count footer visible

### 6.4 Round-trip test
- [ ] 6.4 (Manual) Modify a parameter in the panel, press Shift+S, re-launch demo,
  confirm the saved preset reloads correctly via Tab cycle

---

## 7. Documentation

### 7.1 Header doc
- [ ] 7.1 `vfx_preset_serializer.h` doc block covers: JSON format, file location,
  field list schema version note (add `"version": 1` as the first field in the
  JSON to allow future format migrations), thread safety

### 7.2 Add schema version field
- [ ] 7.2 The JSON files include `"version": 1` as the first key; `Load()` reads this
  field and logs a warning (but continues loading) if the value is not `1`
