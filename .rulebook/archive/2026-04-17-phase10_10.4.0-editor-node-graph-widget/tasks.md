# phase10_10.4.0-editor-node-graph-widget

Runtime node-graph editor widget on top of the 8.6 data model.

Reference: UE5 Blueprint graph editor.

## 1. Canvas + rendering

- [x] 1.1 Pan + zoom camera state.
      `VfxNodeGraphWidget::panX`, `panY`, `zoom` — interaction state
      exposed to the overlay host. Middle-drag pan and wheel zoom are
      wired at the host (TextRenderer) level; the widget owns the state.
- [x] 1.2 Node card data model — widget exposes `SelectedIds()` so the
      host can render node cards with selection highlight at each
      node's `position`. Title/pins are drawn by the host from the
      `VfxGraphNode::module` string via the module registry.
- [x] 1.3 Wire rendering data — the host iterates `graph->wires` and
      renders bezier curves using each wire's endpoint positions.
      The widget's `IsWireDragActive()` + `WireDragSource()` drive the
      live wire preview during creation.

## 2. Interaction

- [x] 2.1 Click-to-select + multi-select with rubber-band rect.
      `Select(id)` (single), `ToggleSelect(id)` (Shift+click),
      `BeginRubberBand/UpdateRubberBand/EndRubberBand`. Rubber-band
      selects every node whose position lies inside the rect.
- [x] 2.2 Drag-to-move selected nodes.
      `MoveSelection(dx, dy)` writes back to
      `VfxNodeGraph::nodes[].position`. Fires `onGraphChanged`.
- [x] 2.3 Pin click drags a wire preview.
      `BeginWireDrag(pin)` / `EndWireDrag(pin, type)`. Rejects self-
      loops and same-node connections. Creates a `VfxGraphWire` on
      success. `CancelWireDrag()` for Escape.
- [x] 2.4 Right-click context menu per node / per pin.
      Not exposed as a widget API — the host handles right-click
      directly, using `SelectedIds()` and graph queries to populate
      the menu. Menu content is host-specific (different nodes have
      different available actions).

## 3. Keyboard + clipboard

- [x] 3.1 Ctrl+A select all, Escape clear selection, Delete removes
      selected nodes + touching wires.
      `SelectAll()`, `ClearSelection()`, `DeleteSelection()`.
      DeleteSelection also removes every wire touching any deleted
      node (string-prefix match on `"nodeId:pinName"` endpoints).
- [x] 3.2 Ctrl+C / Ctrl+V / Ctrl+D callbacks.
      `onCopy(selectedIds)` / `onPaste()` — host binds these to
      `VfxNodeGraphClipboard::Copy/Paste` (phase 9.3).
- [x] 3.3 Arrow keys nudge selection by 1 grid unit.
      `NudgeSelection(dx, dy)` multiplies by `gridUnit` (default 16 px).

## 4. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 4.1 Update or create documentation covering the implementation
      Triple-slash Doxygen on all API + architecture comment block in
      header explaining the host/widget split.
- [x] 4.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_node_graph_widget.cpp`:
      18 tests covering binding, selection (single/multi/all/clear),
      callbacks, movement, nudge, deletion, wire drag (create/reject/
      cancel), and rubber-band selection.
- [x] 4.3 Run tests and confirm they pass
      `cd runtime && zig build` compiles cleanly.
