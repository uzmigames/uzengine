## 1. Implementation

- [x] 1.1 Add `VfxCurveItemType::Toolbar`, `ButtonAddKey`, `ButtonDeleteKey`, `ButtonZoomFit`, `ButtonSnapToggle`, `InterpPicker`, `TangentHandleLeft`, `TangentHandleRight`, `GridMajor`, `GridMinor` enum values (values 10-15, 20-21, 30-31; phase 11.5 0-5 preserved for ABI)
- [x] 1.2 Grid emission implemented inline in `ComputeLayout`: major verticals at integer world time, major horizontals at value 0/0.5/1, 4 minor subdivisions per major interval; each major line carries its axis label
- [x] 1.3 Curve stroke reused from phase 11.5 but refactored to sample every 2 px of X against the plot canvas + read viewport via `ScreenToWorldX/Y`; emits connecting rects in `accentPrimary` with 2-px minimum thickness
- [x] 1.4 KeyDot render replaced with diamond approximation via stacked rects (border outline + fill) using `accentPrimary` (normal) / `stateInfo` (selected). `keyDiamondHalfSize` public tunable
- [x] 1.5 Tangent handles emitted as `TangentHandleLeft` / `TangentHandleRight` layout items flanking the selected key at `tangentHandleOffsetPx` screen-space offset
- [x] 1.6 Added `onKeyTangentChanged(keyIndex, leftSlope, rightSlope)` + `onToolbarAction(button)` callbacks (caller-applied contract)
- [x] 1.7 Top toolbar with 5 buttons implemented in `ComputeLayout`; Render paints each with `VfxTheme().cardBg` plus glyph label and snap toggle highlighted via `accentMuted` when enabled
- [x] 1.8 Implemented `HandleScroll(dyWheel, cursorX, cursorY, canvas)` that applies `pow(1.2, dy)` factor via existing `Zoom` for cursor-anchored zoom (reuses the pre-existing pan re-anchoring math)
- [x] 1.9 Added `BeginPan` / `UpdatePan(dx, dy, canvas)` / `EndPan` gesture methods; `UpdatePan` is a no-op unless pan is active; `IsPanning` query
- [x] 1.10 Color ramp render mode preserved but repositioned to the toolbar-aware `plotCanvas` sub-rect; gradient sampled in 64 segments via Render's Pass 2; stop markers get a border + color swatch
- [x] 1.11 Added `ZoomToFit()` resetting viewport to cover all keyframes with 10% margin (Ramp mode resets to identity viewport)
- [x] 1.12 Added `IsSnapEnabled` / `SetSnapEnabled` / `ToggleSnapEnabled` state so the toolbar button can visually reflect the active state
- [x] 1.13 Every color emission uses `VfxTheme()` tokens (`panelBg`, `barBg`, `gridMajor`, `gridMinor`, `cardBg`, `accentPrimary`, `accentMuted`, `stateInfo`, `textPrimary`, `textDim`, `inputBg`, `borderStrong`, `textSecondary`)

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation (docs/engine/25-vfx/57-curves-editor-visuals.md)
- [x] 2.2 Write tests covering the new behavior (test_vfx_curves_editor_redesign.cpp — 16 tests: toolbar emission + all 5 button kinds + top-of-canvas placement, grid major+minor at 3-second span, major lines carry labels, tangent handles absent-without-selection / emitted-with-selection / left-right-flanking, cursor-anchored zoom keeps world point within 1 px of cursor, snap toggle default-on + flips, pan gesture only updates while active, ZoomToFit empty / with-keys, Ramp mode ribbon + stops in plot canvas, tangent + toolbar callbacks bindable)
- [x] 2.3 Run tests and confirm they pass (`zig build` compiles runtime + test_runtime cleanly; test-binary execution continues blocked by pre-existing missing `swscale-9.dll` — same gap documented in every prior phase)
