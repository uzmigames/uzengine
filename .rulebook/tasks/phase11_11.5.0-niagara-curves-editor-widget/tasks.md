# phase11_11.5.0-niagara-curves-editor-widget

Canvas editor for `VFXCurve` / `VfxTangentCurve` / `VFXColorRamp` — keyframe drag, live GPU bake.

Reference: UE5 `SCurveEditor` + phase 7.16 tangent curve asset.

## 1. Canvas widget

- [ ] 1.1 `VfxCurvesEditorWidget` in
      `runtime/engine/vfx/editor/vfx_curves_editor_widget.{h,cpp}`.
      `Bind(VFXCurve*)` / `Bind(VfxTangentCurve*)` / `Bind(VFXColorRamp*)`
      variants — same widget, different render mode.
- [ ] 1.2 Pan (middle-drag) + zoom (wheel) camera state (`panX`, `panY`,
      `zoomX`, `zoomY`). Axis grid lines rendered in the background.
- [ ] 1.3 Value/time axes labels updating with zoom level.

## 2. Keyframe interaction

- [ ] 2.1 Click empty canvas → `AddKey(time, value)` at clicked position,
      default interp = Cubic (tangent mode matches neighbours).
- [ ] 2.2 Drag a key → `MoveKey(id, time, value)` with snapping to 0.01s
      / 0.001 value grid (Shift disables snap).
- [ ] 2.3 Right-click key → context menu (Delete / Set Time / Set Value /
      Interp: Constant / Linear / Cubic / Custom tangents).
- [ ] 2.4 Selected key highlighted accent colour; tangent handles visible
      for Cubic+ keys, draggable.

## 3. Mode-specific rendering

- [ ] 3.1 Scalar mode (`VFXCurve`): single polyline between keys.
- [ ] 3.2 Vec4 mode (`VFXCurve<Vec4>`): four polylines (RGBA) on same canvas,
      toggle per-channel visibility via legend.
- [ ] 3.3 Color ramp mode (`VFXColorRamp`): horizontal 1D ribbon with
      diamond markers at each stop; drag horizontally to move stops.
- [ ] 3.4 Numeric fields at the canvas bottom for the selected key:
      time input, value input(s).

## 4. Live GPU bake

- [ ] 4.1 After any curve mutation, call the curve's existing bake method
      (`BakeToTexture()` / `BakeToRGBA16F()`) and re-upload to the GPU
      texture the renderer samples from — guarded by a dirty flag to
      avoid per-frame re-bakes.
- [ ] 4.2 Debounce: only re-bake after the mouse is released (drag end) or
      after 100ms of idle during a drag, to avoid stutter.

## 5. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 5.1 Update or create documentation covering the implementation
      New doc `docs/engine/25-vfx/46-curves-editor.md` with UI mock.
- [ ] 5.2 Write tests covering the new behavior
      Keyframe math (add/move/delete/sort), tangent-mode round-trip,
      re-bake dirty tracking.
- [ ] 5.3 Run tests and confirm they pass
      `zig build` clean; QA: edit fireball head's color-over-life,
      watch preview update live.
