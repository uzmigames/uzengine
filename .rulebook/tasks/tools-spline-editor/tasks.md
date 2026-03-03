# Tasks: tools-spline-editor

## 1. SplineComponent Data Structure

- [ ] 1.1 Create `engine/src/scene/components/spline_component.h` defining `SplineComponent` with `vector<BezierControlPoint>` storage
- [ ] 1.2 Define `BezierControlPoint` struct: `{position: float3, tangent_in: float3, tangent_out: float3}`
- [ ] 1.3 Implement `SplineComponent::Sample(t) -> float3`: cubic Bezier interpolation between control points at normalized parameter t ∈ [0, 1]
- [ ] 1.4 Implement arc-length parameterization lookup table: precompute 1000 samples on `Sample(t_uniform)` and build a `t_arc[1000]` table mapping uniform t to arc-length t
- [ ] 1.5 Implement `SplineComponent::GetLength() -> float`: return total arc length computed from the lookup table
- [ ] 1.6 Implement `SplineComponent::SampleAtDistance(d) -> float3`: sample at world-space arc distance d using binary search into the lookup table
- [ ] 1.7 Implement `.uzscene` serialization: write `spline_component` INI block with control point array (position and tangents as space-separated floats)
- [ ] 1.8 Implement `.uzscene` deserialization: parse `spline_component` INI block back into `vector<BezierControlPoint>`

## 2. Spline Editor Viewport Mode

- [ ] 2.1 Create `engine/src/editor/spline_editor.h` and `engine/src/editor/spline_editor.cpp` defining `SplineEditor` class
- [ ] 2.2 Implement `SplineEditor::Activate(entity_id)`: enter spline editing mode when a node with `SplineComponent` is selected
- [ ] 2.3 Implement control point handle rendering: draw white sphere gizmos at each `BezierControlPoint.position` using editor gizmo API
- [ ] 2.4 Implement tangent handle rendering: draw blue spheres for `tangent_in` and orange spheres for `tangent_out` at each control point
- [ ] 2.5 Implement click-drag: raycast from mouse into world; move nearest handle to hit position; mark spline dirty
- [ ] 2.6 Implement click-in-empty-space: append new control point at the end of the spline at the raycast hit position
- [ ] 2.7 Implement Shift+click between existing points: insert a new control point at the nearest midpoint
- [ ] 2.8 Implement Delete key on selected control point: remove the selected point from the array; prevent removal if only 1 point remains
- [ ] 2.9 Implement C key toggle: switch between auto-smooth mode (tangents computed by Catmull-Rom formula from neighbors) and manual tangent mode

## 3. Gizmo Rendering

- [ ] 3.1 Add spline curve line rendering to `engine/src/renderer/editor_pass.cpp`: draw 200 line segments per spline segment using `DrawCallRecorder`-compatible DX12 line primitive
- [ ] 3.2 Compute 200 evenly-spaced positions along the spline using `SplineComponent::Sample` for each segment
- [ ] 3.3 Render the curve in a distinct editor color (e.g., bright green) that does not conflict with other gizmo colors

## 4. Scene Tree and Inspector Integration

- [ ] 4.1 Add spline icon to Scene Tree for nodes with `SplineComponent`
- [ ] 4.2 Show arc length and control point count in the Inspector panel for the selected `SplineComponent`
- [ ] 4.3 Add "Add Control Point" and "Remove Selected Point" buttons to the Inspector panel for `SplineComponent`

## 5. Undo/Redo Integration

- [ ] 5.1 Wrap "add control point" in an `EditorCommand` with `Execute` (append point) and `Undo` (remove last point)
- [ ] 5.2 Wrap "move control point" in an `EditorCommand` with `Execute` (update position) and `Undo` (restore previous position)
- [ ] 5.3 Wrap "remove control point" in an `EditorCommand` with `Execute` (remove) and `Undo` (re-insert at original index)
- [ ] 5.4 Push all spline edit commands to the engine's `UndoStack`

## 6. Lua API

- [ ] 6.1 Add `ctx.spline:sample(node_id, t) -> float3` Lua binding that calls `SplineComponent::Sample(t)` on the entity's spline component
- [ ] 6.2 Add `ctx.spline:get_length(node_id) -> float` Lua binding that calls `SplineComponent::GetLength()`
- [ ] 6.3 Add `ctx.spline:get_point_count(node_id) -> int` Lua binding

## 7. IPC Handler

- [ ] 7.1 Add `tools.export_spline_points(node_id, count)` IPC handler that calls `SplineComponent::SampleAtDistance` at `count` evenly-spaced arc distances and returns a JSON array of `float3` world positions
- [ ] 7.2 Register the IPC handler in the engine's IPC dispatch table

## 8. Testing (≥95% line + branch coverage)

- [ ] 8.1 Write `tests/tools/test_spline_sample.cpp`: create a straight-line spline with 2 control points; assert `Sample(0.5)` returns the midpoint; create a curved spline with known control points; assert `Sample(t)` at t=0, 0.5, 1.0 matches analytically-computed Bezier positions within floating-point tolerance
- [ ] 8.2 Write `tests/tools/test_spline_arc_length.cpp`: create a unit-circle spline approximation; assert `GetLength()` is within 1% of 2π
- [ ] 8.3 Write `tests/tools/test_spline_serialization.cpp`: create a spline with 4 control points; serialize to `.uzscene` block; deserialize; assert all `BezierControlPoint` fields match original values within 1e-6 tolerance
- [ ] 8.4 Write `tests/tools/test_spline_undo_redo.cpp`: add a control point; call Undo; assert point count decremented; call Redo; assert point count restored
- [ ] 8.5 Write `tests/tools/test_spline_catmull_rom.cpp`: enable auto-smooth mode; place 4 control points in a line; assert tangents are computed (non-zero) and produce a smooth curve
- [ ] 8.6 Verify ≥95% line and branch coverage across all `engine/src/scene/components/spline_component.cpp` and `engine/src/editor/spline_editor.cpp` using OpenCppCoverage
