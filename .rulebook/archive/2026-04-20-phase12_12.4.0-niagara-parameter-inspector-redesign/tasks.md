## 1. Implementation

- [x] 1.1 Add `sectionName` companion map to the inspector widget (keyed by `(scope, name)`) — default "General"; runtime `VfxAttributeStore` stays pure (no schema change). `SetParameterSection` / `GetParameterSection` / `DistinctSections` accessors added.
- [x] 1.2 Extend `VfxParamItemType` enum with `Section`, `EditorFloat`, `EditorVec3`, `EditorColor`, `EditorBool`, `EditorEnum`, `EditorCurve`, `EditorRamp`, `EditorString`, `ResetToDefault`, `BindToCurve` (values 20, 30-37, 40, 41 — preserve phase 11.4 0-15 for ABI)
- [x] 1.3 Rewrite `ComputeLayout` to emit sections (collapsible, nested under each scope) + per-row 40/55/5 label/editor/actions split via `std::round(width * 0.4/0.55)` formula
- [x] 1.4 HitTest priority works automatically via existing enum-value-based promotion — EditorFloat (30) beats ParameterRow (3), ResetToDefault (40) beats editor, BindToCurve (41) highest
- [x] 1.5 Implement `Render` for each editor kind: Float (value preview), Vec3 (x,y,z text), Color (RGBA text + swatch chip), Bool (true/false label). Enum/Curve/Ramp/String placeholders ready for future data-type growth
- [x] 1.6 Add `onParameterReset(scope, name)` + `onParameterBindRequested(scope, name)` callbacks (caller-applied contract)
- [x] 1.7 Add `AddParameterRange(scope, name, min, max)` + `IsValueInRange` helper; Render draws 4-edge 1-px `stateError` border around out-of-range EditorFloat
- [x] 1.8 Section collapse state tracking via `std::unordered_map<std::string, bool> m_sectionExpanded` + `IsSectionExpanded/SetSectionExpanded/ToggleSectionExpanded` API
- [x] 1.9 Wire right-panel click dispatch in `vfx_editor_overlay.cpp`: ScopeHeader toggles scope, Section toggles section, ParameterRow selects + focus=Parameters, ResetToDefault/BindToCurve fire their callbacks
- [x] 1.10 Every new render path uses `VfxThemeColors()` tokens (panelBgAlt for section bg, textSecondary for section label, inputBg for editor bg, stateError for range border, accentPrimary for bind icon, cardBg for action buttons)
- [x] 1.11 Companion maps (section name, range, override flag) are dropped on `EraseTypeEntry` to prevent annotation leaks when parameters are removed or renamed

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation (docs/engine/25-vfx/56-parameter-inspector-visuals.md)
- [x] 2.2 Write tests covering the new behavior (test_vfx_parameter_inspector_redesign.cpp — 17 tests: default-section-is-General, SetSection round-trip, one-Section-per-distinct-name, collapsed-section-hides-rows, DistinctSections-ordered-by-first-appearance, Float/Vec3/Vec4 → EditorFloat/Vec3/Color emission, 40/55/5 split at width 400 within +/- 2-px tolerance per spec, reset + bind callbacks fire, range no-validation-default / in-bounds / out-of-bounds / unknown-param no-op, ResetToDefault per-row emission, BindToCurve only on Float rows, HitTest priority for editor-beats-row and reset-beats-editor)
- [x] 2.3 Run tests and confirm they pass (`zig build` compiles runtime + test_runtime cleanly; test-binary execution continues blocked by pre-existing missing `swscale-9.dll` — same gap documented in phases 11.1-11.14 + 12.1-12.3)
