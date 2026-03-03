## 1. AccessibilitySettings Singleton

- [ ] 1.1 Create `src/accessibility/accessibility_settings.h` declaring `AccessibilitySettings` struct with fields: `colorblind_mode` (enum: None, Deuteranopia, Protanopia, Tritanopia), `reduced_motion` (bool), `large_text_scale` (float 1.0–2.0), `high_contrast_ui` (bool), `subtitles_enabled` (bool)
- [ ] 1.2 Create `src/accessibility/accessibility_settings.cpp` implementing `AccessibilitySettings::get()` singleton accessor and `load()` / `save()` methods reading/writing `%APPDATA%/UzEngine/accessibility.ini`
- [ ] 1.3 Implement `AccessibilitySettings::load()` to parse INI before the first frame is rendered; call from engine startup sequence
- [ ] 1.4 Implement `AccessibilitySettings::save()` persisting all fields to `accessibility.ini`; call when any setting changes
- [ ] 1.5 Define `ColorblindMode` enum in `accessibility_settings.h` with values `None = 0`, `Deuteranopia`, `Protanopia`, `Tritanopia`

## 2. Colorblind Post-Process Pass

- [ ] 2.1 Add Daltonize 3x3 LUT matrices as engine constants in `src/accessibility/colorblind_luts.h` for Deuteranopia, Protanopia, and Tritanopia correction
- [ ] 2.2 Add colorblind correction pass in `src/renderer/post_process.cpp` after tone mapping; pass applies `LUT_matrix * rgb` per pixel using the active mode matrix
- [ ] 2.3 Skip colorblind pass entirely (zero overhead) when `colorblind_mode == None`; guard with early return before shader dispatch
- [ ] 2.4 Upload selected LUT matrix to a constant buffer `cbColorblindMode` bound to the post-process shader
- [ ] 2.5 Write `shaders/post/colorblind_correction.hlsl` computing matrix-vector multiply on `float3 color`

## 3. Subtitle System

- [ ] 3.1 Create `src/ui/subtitle_manager.h` declaring `SubtitleManager` with `show(const char* text, uint32_t duration_ms, const char* speaker_name)` and `hide()` methods
- [ ] 3.2 Create `src/ui/subtitle_manager.cpp` implementing subtitle rendering via the existing UI system; render styled overlay at screen bottom center
- [ ] 3.3 Apply `AccessibilitySettings::large_text_scale` as a multiplier to subtitle font size on every `show()` call
- [ ] 3.4 Auto-hide subtitle after `duration_ms` milliseconds; track with an engine timer
- [ ] 3.5 Suppress subtitle display entirely when `subtitles_enabled == false`

## 4. Reduced Motion Integration

- [ ] 4.1 In the screen shake system, check `AccessibilitySettings::reduced_motion` before applying any positional offset; skip shake entirely when `true`
- [ ] 4.2 In the motion blur post-process pass, check `reduced_motion` before dispatching the motion blur shader; skip when `true`
- [ ] 4.3 In the particle camera effects (radial blur, chromatic aberration), check `reduced_motion` before applying; skip when `true`

## 5. Large Text Mode

- [ ] 5.1 In the UI text layout pass, multiply all `ctx.ui` text element font sizes by `large_text_scale` from `AccessibilitySettings`
- [ ] 5.2 In the UI layout system, reflow container sizes after font size scaling to prevent clipping at 2.0x scale
- [ ] 5.3 Clamp `large_text_scale` to the range [1.0, 2.0] during load and save operations

## 6. High Contrast UI Mode

- [ ] 6.1 In `uz_ui_html.dll`, detect `high_contrast_ui == true` and inject the CSS class `.uz-high-contrast` on the `<body>` element of all loaded HTML documents
- [ ] 6.2 Create `assets/ui/themes/high_contrast.css` providing black/white/yellow palette overrides for all `uz-*` custom elements; minimum 4.5:1 contrast ratio for all text
- [ ] 6.3 Remove `.uz-high-contrast` class from body when `high_contrast_ui` transitions to `false`

## 7. Lua Read API

- [ ] 7.1 Create `src/scripting/lua_accessibility.cpp` exposing `ctx.accessibility` table with read-only fields: `colorblind_mode`, `reduced_motion`, `large_text_scale`, `high_contrast_ui`, `subtitles_enabled`
- [ ] 7.2 Bind `ctx.accessibility` table in the Lua state initialization; register all five fields as read-only properties returning current `AccessibilitySettings` values
- [ ] 7.3 Return `colorblind_mode` as a string ("none", "deuteranopia", "protanopia", "tritanopia") to Lua rather than an integer enum value

## 8. Testing

- [ ] 8.1 Create `tests/accessibility/test_accessibility_settings.cpp` with tests for: default values after construction, INI round-trip (save then load), clamping of `large_text_scale` to [1.0, 2.0], and all enum values of `colorblind_mode`
- [ ] 8.2 Test `SubtitleManager::show()` stores text and speaker name correctly; test auto-hide fires after `duration_ms`; test no-op when `subtitles_enabled == false`
- [ ] 8.3 Test `AccessibilitySettings::load()` gracefully handles a missing INI file (uses defaults) and a corrupt INI file (uses defaults, no crash)
- [ ] 8.4 Test colorblind LUT matrices: verify Deuteranopia, Protanopia, and Tritanopia matrices are non-identity and differ from each other; verify None mode produces identity behavior
- [ ] 8.5 Test Lua API bindings: verify `ctx.accessibility.reduced_motion` reflects the current setting; verify write attempt raises a Lua error
- [ ] 8.6 Test reduced-motion integration: assert screen shake `apply()` returns zero offset when `reduced_motion == true`; assert motion blur pass is skipped when `reduced_motion == true`
- [ ] 8.7 Achieve >= 95% line and branch coverage across all accessibility source files

## 9. Documentation

- [ ] 9.1 Update `docs/specs/ui-system.md` with subtitle overlay API and high contrast CSS class documentation
- [ ] 9.2 Update `docs/specs/screen-space-effects.md` noting reduced motion and colorblind mode pass conditions
- [ ] 9.3 Update `docs/specs/localization.md` documenting `SubtitleManager::show()` parameters and Lua `ctx.accessibility` read API
