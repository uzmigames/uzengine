# Proposal: phase12_12.5.0-niagara-curves-editor-redesign

## Why

The phase 11.5.0 `VfxCurvesEditorWidget` implements curve key CRUD +
interpolation picking + debounced re-bake, but renders as a minimal
plot with keys as small dots. Niagara's curves editor is a full
graph editor with:

- **Grid** — major + minor gridlines in time (X) and value (Y),
  auto-scaled to the visible range.
- **Axes** — X axis shows time labels (e.g. "0.00s", "0.25s", "0.50s"),
  Y axis shows value labels (e.g. "0.0", "0.5", "1.0"). Both have a
  1-px baseline in `borderSubtle` with tick marks every major division.
- **Curve stroke** — the curve line is drawn anti-aliased (as best a
  rectangle-based renderer can) by tracing many short segments through
  sampled points, colored in `accentPrimary` with 2-px thickness.
- **Key handles** — each key is a 10-px diamond (`accentPrimary` fill,
  `borderStrong` outline). Selected keys switch to
  `stateInfo` fill and get two 6-px tangent handles (for cubic
  interpolation) that can be dragged to change slope.
- **Interp indicator** — each key shows a small glyph above it
  indicating its interpolation mode (● linear, ◆ cubic, ~ smoothstep).
- **Toolbar** — top strip with Add Key, Delete Selected, Zoom-to-fit,
  Snap toggle, Interp picker (Linear / Cubic / Smoothstep / Const).
- **Viewport zoom + pan** — scroll wheel zooms around cursor, middle-
  mouse drag pans. Shift+drag on Y axis to zoom Y only.
- **Color ramp mode** — when bound to a `VFXColorRamp`, the editor
  switches to a horizontal gradient strip with stop markers instead
  of a 2D plot.

This task delivers the full visual curves editor.

## What Changes

1. **Grid rendering** — new `Render` helper `DrawGrid(canvas,
   viewXMin, viewXMax, viewYMin, viewYMax)` that emits horizontal +
   vertical major/minor lines + axis labels.

2. **Curve stroke** — sample the bound curve at every 1-2 px of X
   resolution and emit a sequence of short rectangles connecting the
   points. Visual thickness 2 px.

3. **Key handles** — diamonds 10 px wide centered on
   `(time, value)` in viewport space. Selected keys render a
   `borderStrong` outline + 2 tangent handle rects at
   `leftTangent` and `rightTangent`.

4. **Tangent editing** — new `VfxCurveItemType::TangentHandleLeft` /
   `TangentHandleRight` layout items with `HitTest` dispatch. The
   widget fires `onKeyTangentChanged(keyIndex, leftSlope, rightSlope)`
   on drag end.

5. **Toolbar** — new layout item `Toolbar` at the top of the canvas
   containing a row of buttons: Add Key, Delete Selected, Zoom Fit,
   Snap Toggle, Interp Picker (dropdown). Each has its own hit item.

6. **Zoom / pan gestures** — `HandleMouse` gets a new
   `HandleScroll(dx, dy, cursorX, cursorY)` method that
   geometrically scales the viewport around the cursor.

7. **Color ramp mode rendering** — when `m_mode == Ramp`, `Render`
   draws a horizontal gradient strip sampled from the ramp + triangular
   stop markers below the strip.

## Impact

- Affected specs: `specs/vfx-curves-editor-redesign/spec.md` (new).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_curves_editor_widget.{h,cpp}` —
    layout + render rewrite, tangent handle support, toolbar.
  - `runtime/tests/engine/vfx/test_vfx_curves_editor_redesign.cpp`
    — new.
  - `docs/engine/25-vfx/57-curves-editor-visuals.md` — new.
- Breaking change: MINOR — enum grows with new item kinds, new
  callbacks added. No existing APIs change.
- User benefit: editing animation curves is finally usable with
  direct-manipulation handles, scannable grid, and visible interp
  modes.
