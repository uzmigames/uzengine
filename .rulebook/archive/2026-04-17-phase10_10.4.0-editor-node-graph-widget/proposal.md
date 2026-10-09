# Proposal: phase10_10.4.0-editor-node-graph-widget

## Why
Phase 8.6 (clipboard), 8.10 (expressions), and the VfxNodeGraph data model all exist, but no widget renders the graph or accepts keyboard input on it. Phase 9 tasks 9.3 (clipboard keybindings) and 9.6 (pin expression editor) need a runtime graph-editor widget before they can ship.

## What Changes
New `VfxNodeGraphWidget` component: renders `VfxNodeGraph` as clickable nodes + wires on a pan-zoomable canvas. Node selection + multi-select with rubber-band rect. Pin hover / right-click context menu. Keyboard hooks for Ctrl+C/V/D/A (select all) that consumers bind to actions (9.3).

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: new runtime/engine/vfx/editor/vfx_node_graph_widget.*
- Breaking change: NO
- User benefit: Authoring node graphs directly in-engine.
- Estimated effort: XL

## Source
UE5 Blueprint / Niagara node graph editor.
