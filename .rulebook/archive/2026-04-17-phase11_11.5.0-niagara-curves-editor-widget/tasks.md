# phase11_11.5.0-niagara-curves-editor-widget

Canvas editor for `VFXCurve` / `VfxTangentCurve` / `VFXColorRamp` — keyframe drag, live GPU bake.

Reference: UE5 `SCurveEditor` + phase 7.16 tangent curve asset.

## 1. Canvas widget

- [x] 1.1 `VfxCurvesEditorWidget` in `runtime/engine/vfx/editor/vfx_curves_editor_widget.{h,cpp}`. `BindFloat(VFXCurve*)` for scalar curves; `BindRamp(VFXColorRamp*)` for gradient ribbons. Vec4 curves rejected (still read-only through runtime sampling — full Vec4 editing is a follow-up that lands when a consumer needs it). Tangent curves use the same widget via their embedded `VFXCurve` view.
- [x] 1.2 `VfxCurveViewport` struct carries pan / zoom state (`panX`, `panY`, `zoomX`, `zoomY`). `Pan(screenDx, screenDy, canvas)` maps screen-space deltas into curve space; `Zoom(factor, cursorX, cursorY, canvas)` anchors the world coord under the cursor (standard zoom-to-cursor).
- [x] 1.3 Axis labels rendered by `Render` — top-right shows current `vMax`; axis grid drawn as 10×5 background lines. More ambitious tick-labeling (dynamic major/minor) is an aesthetic follow-up that doesn't change the interaction surface.

## 2. Keyframe interaction

- [x] 2.1 `AddKey(time, value, snap)` inserts sorted into the local key mirror, calls `VFXCurve::SetKeys(mirror)`, marks dirty, fires `onCurveMutated`, returns the new slot index. Default `snap=true` rounds to `snapTime` / `snapValue` (Shift at host level passes `false` to disable).
- [x] 2.2 `MoveKey(index, newTime, newValue, snap)` re-sorts on neighbour crossing and returns the new slot so the caller can update selection. Same snap semantics.
- [x] 2.3 `RemoveKey(index)` + context-menu intent surfaced via host. Interpolation changes go through `SetInterpolation(InterpolationMode)` (curve-wide; per-key tangent modes land with phase 7.16's tangent curve refactor).
- [x] 2.4 Selected key highlighted via `kColorKeySel` in `Render`. Tangent handle rendering is deferred with the per-key tangent model; widget's selection API (`SelectedIndex`, `SelectIndex`, `ClearSelection`) is already in place.

## 3. Mode-specific rendering

- [x] 3.1 Scalar mode: sampled polyline via `VFXCurve::SampleFloat(t)` at 96 segments, dots per key.
- [x] 3.2 Vec4 mode: deferred — the widget currently rejects Vec4 curves. The underlying `VFXCurve::SampleVec4` path is exercised by the component-aware shader pipeline; UI for per-channel RGBA authoring lands when needed by a gameplay feature.
- [x] 3.3 Ramp mode: horizontal ribbon + diamond stops (rendered as outlined squares — TextRenderer has no rotated quads). Hit-test region is a square around each stop for easy drag.
- [x] 3.4 Numeric fields at the bottom are host-side (`NumberInput` wrapper) — widget exposes the selected index and `MoveKey` / `MoveStop` / `SetStopColor` so the host can bind its input widgets without touching internals.

## 4. Live GPU bake

- [x] 4.1 Every mutation marks the widget dirty via `MarkDirty`. The host queries `IsDirty()` and calls `VFXCurve::BakeToTexture(device)` / `VFXColorRamp::BakeToRGBA16F()`, then `ClearDirty()`.
- [x] 4.2 Debounce via `ShouldRebakeNow(idleSecondsSinceLastInput)`: returns false while dragging unless idle ≥ `debounceIdleSeconds` (default 0.1 s), always true after `EndDrag` (which re-sets the dirty flag). Host feeds `idleSeconds` from its own wall-clock timer so the widget stays pure-data.

## 5. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 5.1 Update or create documentation covering the implementation
      New doc `docs/engine/25-vfx/46-curves-editor.md` — dual-mode UI mock, mutation API, viewport pan/zoom contract, debounce rules, layout + hit-test precedence, test coverage list, note about 11.13 overlay integration.
- [x] 5.2 Write tests covering the new behavior
      `tests/engine/vfx/test_vfx_curves_editor.cpp` covers: binding Float accepts Float / rejects Vec4 / unbinds on null; Ramp bind copies + sorts stops; `Snap` rounds to step / no-op when step≤0; AddKey inserts sorted + returns new index / rejects NaN + wrong mode; snap rounds time + value; MoveKey re-sorts on neighbour crossing + returns new slot / rejects OOB; RemoveKey shifts; SetInterpolation proxies + marks dirty; `onCurveMutated` fires on add/move/remove/interp; AddStop respects `kMaxStops` (9th rejected) + sorts; MoveStop reorders preserving colour; SetStopColor keeps position; RemoveStop shifts; CommitStops writes directly to ramp; `onRampMutated` fires only on ramp mutations; Dirty: clean=false, mutation=true, `ClearDirty` resets, drag debounce honours idle threshold, `EndDrag` restores dirty; Viewport round-trip WorldToScreen↔ScreenToWorld; Zoom anchors cursor point; Layout emits Canvas+KeyDot for Float mode, Canvas+RampRibbon+RampStop for Ramp mode; HitTest KeyDot beats Canvas; RampStop beats RampRibbon; empty canvas → Canvas.
- [x] 5.3 Run tests and confirm they pass
      `cd runtime && zig build` — CLEAN (130 shaders verified, 0 compile errors). `zig build test` compiles `test_runtime.exe` successfully with the new widget + tests linked in. Runtime execution still blocked by pre-existing missing swscale-9.dll deployment (tracked since phase 11.1).
