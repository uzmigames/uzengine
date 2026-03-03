# Proposal: tools-spline-editor

## Why

Splines are a fundamental world-building primitive. Roads, rivers, power lines, fences, train tracks, canyon paths, and NPC patrol routes all share the same underlying need: a smooth curve through 3D space that an artist can author interactively and a game system can sample at runtime. Without an in-engine spline editor, every road in a game level requires placing dozens of individual mesh nodes by hand, manually rotating each to follow terrain curvature. This approach produces jagged roads, takes 10x longer than spline-based authoring, and is nearly impossible for AI agents to use effectively.

The `tools-spline-editor` adds a Bezier spline authoring tool to the viewport. When a node with a `SplineComponent` is selected, spline editing mode activates: control points appear as white spheres, tangent handles as smaller colored spheres. The developer clicks in the viewport to add control points, drags handles to adjust curve shape, and the spline updates in real-time. The spline data is serialized to the `.uzscene` file as a `spline_component` block with a list of control points and tangents (plain text, AI-friendly).

Splines become immediately useful for the terrain system (Phase 6): roads can be carved into heightmaps by projecting the spline path onto terrain geometry. They also enable gameplay systems: a `PatrolPath` component that reads from a SplineComponent for NPC waypoints, a `CameraRail` component for cinematic camera paths. The core spline data structure and editor are shared infrastructure for all of these use cases — which is why it belongs in the Tools track rather than any single game-system phase.

## What Changes

- `SplineComponent` C++ class: stores array of `BezierControlPoint{position, tangent_in, tangent_out}`, provides `Sample(t)` → `float3` and `GetLength()` APIs, serializes to/from `.uzscene` INI-like format
- Spline evaluation: cubic Bezier interpolation with arc-length parameterization (lookup table, 1000 samples) for uniform speed sampling
- `SplineEditor` viewport mode: activates when node with SplineComponent selected in editor
  - Control point handles: white spheres, click-drag to move
  - Tangent handles: colored spheres (in=blue, out=orange), drag to adjust curve curvature
  - Click in empty space: append new control point at end of spline
  - Shift+click between points: insert control point
  - Delete key on selected point: remove control point
  - C key toggle: switch between auto-smooth (tangents computed by Catmull-Rom) and manual tangent mode
- Gizmo rendering: spline curve drawn as DX12 line primitive in editor pass, 200 segments per spline segment
- Scene Tree indicator: SplineComponent nodes show spline icon; length and point count shown in Inspector
- Undo/Redo integration: all spline edits are EditorCommands (add point, move point, remove point)
- `ctx.spline:sample(node_id, t)` Lua API: returns world-space position along spline at normalized parameter t
- `ctx.spline:get_length(node_id)` Lua API: returns arc length in world units
- Export helper: `tools.export_spline_points(node_id, count)` IPC handler samples N evenly spaced points for road generation or NPC path baking
- Unit tests: spline evaluation correctness (known control points, expected sampled positions); undo/redo round-trip; scene serialization round-trip

## Impact

- Affected specs: `docs/specs/scene-system.md` (add SplineComponent), `docs/specs/editor-gizmos.md` (add spline editing mode), `docs/specs/terrain-system.md` (add road carving via spline)
- Affected code: `engine/src/scene/components/spline_component.cpp` (new), `engine/src/editor/spline_editor.cpp` (new), `engine/src/renderer/editor_pass.cpp` (add line rendering for splines), `vscode-extension/src/extension.ts` (register SplineComponent Inspector panel)
- Breaking change: NO (new component type; no existing components modified)
- User benefit: Enables road/river/path authoring in minutes instead of hours; provides reusable spline infrastructure for NPC patrol paths, camera rails, and terrain road carving; makes world-building more accessible to AI agents via the simple SplineComponent IPC API
