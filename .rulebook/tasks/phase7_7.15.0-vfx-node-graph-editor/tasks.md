# Phase 7-7.15.0 - VFX Node Graph Editor

Visual node/wire editor for VFX graphs, integrated into the existing editor overlay.

Reference: UE5 NiagaraEditor/SNiagaraGraph.cpp

## 1. Layout

- [ ] 1.1 Auto-layout algorithm (Sugiyama layered or hierarchical).
- [ ] 1.2 Cached manual node positions serialised into .uzvfx under an editor key.

## 2. Node rendering

- [ ] 2.1 Draw module header + parameter pins (input/output) + inline parameter edits.
- [ ] 2.2 Type-coloured pin circles (float, vec3, vec4, color).

## 3. Wire rendering

- [ ] 3.1 Bezier curves colored by data type.
- [ ] 3.2 Highlight on hover; show preview value on live graphs.

## 4. Interaction

- [ ] 4.1 Click to select, drag to create or reroute wires.
- [ ] 4.2 Right-click context menu (add module, delete, disable, duplicate).
- [ ] 4.3 Keyboard: Delete to remove, Ctrl+D to duplicate, F to frame selection.

## 5. Persistence + hot-reload

- [ ] 5.1 Save node positions into .uzvfx JSON under editor.node_positions.
- [ ] 5.2 Hot-reload refreshes UI when file changes on disk.

## 6. Docs and tests

- [ ] 6.1 Artist guide with key bindings + workflow.
- [ ] 6.2 Tests: layout determinism, save/load round-trip of node positions.

## 7. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 7.1 Update or create documentation covering the implementation
- [ ] 7.2 Write tests covering the new behavior
- [ ] 7.3 Run tests and confirm they pass
