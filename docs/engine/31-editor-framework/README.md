# Editor Framework

All VSCode editor panels, tools, and editor-specific systems.

> **Status**: Partial (Phase 5 — Extension + IPC ✅, Viewport ✅, Hierarchy/Inspector/Gizmos planned)
> **Source**: `extension/src/panels/`, `runtime/engine/editor/`

| Document | Topic |
|----------|-------|
| [01-overview.md](01-overview.md) | Editor framework overview, panel registration, VSCode integration |
| [02-gizmos.md](02-gizmos.md) | Transform gizmos — translate, rotate, scale, DX12 gizmo pass |
| [03-undo-redo.md](03-undo-redo.md) | `EditorCommand` pattern, `UndoStack` (100 levels) |
| [04-inspector.md](04-inspector.md) | Property inspector — reflection-driven UI, component editing |
| [05-hierarchy-panel.md](05-hierarchy-panel.md) | Scene hierarchy panel — tree view, drag-and-drop reparenting |
| [06-asset-browser.md](06-asset-browser.md) | Asset browser panel — VFS navigation, import, preview |
| [07-tool-modes.md](07-tool-modes.md) | Editor tool modes — select, move, rotate, scale, paint |
| [08-output-panel.md](08-output-panel.md) | Output/log panel — log streaming from runtime |
| [09-node-graph-framework.md](09-node-graph-framework.md) | Shared node graph canvas for material, VFX, behavior tree editors |
| [10-material-editor.md](10-material-editor.md) | Material editor — node-based .uzmat authoring |
| [11-animcontroller-editor.md](11-animcontroller-editor.md) | Animation controller editor — state machine visual editor |
| [12-controlrig-editor.md](12-controlrig-editor.md) | Control rig editor — procedural animation authoring |
| [13-vfx-editor.md](13-vfx-editor.md) | VFX editor — .uzfx particle system authoring |
| [14-sequencer-editor.md](14-sequencer-editor.md) | Sequencer editor — cinematic timeline authoring |
| [15-behavior-tree-editor.md](15-behavior-tree-editor.md) | Behavior tree editor — AI behavior visual authoring |
| [16-pcg-editor.md](16-pcg-editor.md) | PCG editor — procedural content generation graph |
| [17-debug-tools.md](17-debug-tools.md) | Debug visualization tools — physics, navmesh, AI overlays |
| [18-viewport-modes.md](18-viewport-modes.md) | Viewport rendering modes — lit, unlit, wireframe, normal, AO |
| [19-editor-preferences.md](19-editor-preferences.md) | Editor preferences — theme, shortcuts, panel layout |
| [20-editor-ipc.md](20-editor-ipc.md) | Editor-to-runtime IPC — command protocol, event streaming |
| [21-editor-testing.md](21-editor-testing.md) | Editor automation testing — headless mode, snapshot testing |
