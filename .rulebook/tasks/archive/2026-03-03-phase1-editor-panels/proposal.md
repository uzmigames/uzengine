# Proposal: phase1-editor-panels

## Why

The current VSCode extension exposes only a single viewport panel that streams JPEG frames from the DX12 renderer. There is no Scene Tree showing what nodes exist in the scene, no Inspector for editing node properties, and no play controls. This means the engine is effectively a viewer only — developers cannot author scenes, tweak component values, or start/stop game simulation from inside VSCode.

The Scene Tree and Inspector are the two highest-frequency interaction surfaces in any game editor. Without the Scene Tree, users cannot see the node hierarchy or select objects. Without the Inspector, any property change requires manually editing the `.uzscene` file as raw text and reloading the scene. This bottleneck makes interactive scene authoring impossible and forces every edit through a slow file-save-reload cycle.

Play/Pause/Stop controls in the status bar complete the minimum viable editor loop: the developer places objects, adjusts properties in the Inspector, presses Play to run game logic, observes behavior in the JPEG viewport, presses Stop to return to edit mode. All of this must be integrated into the VSCode UI via the existing IPC/WebSocket infrastructure without requiring the user to touch a terminal.

## What Changes

- Register two new VSCode activity bar views in `package.json`: `uzengine.sceneTree` (Scene Tree panel) and `uzengine.inspector` (Inspector panel), placed in a dedicated UzEngine view container
- Implement `SceneTreeProvider` as a `vscode.TreeDataProvider<NodeItem>` that fetches the live node hierarchy via `scene.get_tree` IPC on activation and refreshes on `scene.node_changed` push events from the engine
- Add node selection in the Scene Tree: clicking a node sends `scene.select_node` IPC and fires a VS Code selection event that the Inspector panel subscribes to; selected node is highlighted in the tree
- Add scene tree context menu actions: Add Node (with type submenu), Delete Node, Duplicate Node, Rename Node — each wired to the corresponding `scene.*` IPC command
- Add a search/filter input at the top of the Scene Tree that filters displayed nodes by name substring in real time on the TypeScript side without additional IPC round-trips
- Implement `InspectorPanel` as a `vscode.WebviewPanel` using the Webview UI Toolkit (`@vscode/webview-ui-toolkit`); it renders component properties for the currently selected node with type-appropriate widgets: text fields for strings, number spinners for floats/ints, checkboxes for booleans, color pickers for `float4` color properties, and vec3 XYZ fields for transform position/rotation/scale
- Wire Inspector property changes to debounced `scene.set_property` IPC calls (100ms debounce) so the viewport updates in near-real-time as the user drags a slider or types a value
- Add Play, Pause, and Stop buttons to the VS Code status bar; Play spawns `UzEngine.exe --play` as a child process and monitors its lifecycle; Stop kills the play process and sends `engine.return_to_editor` IPC; Pause sends `scene.set_paused` IPC; all buttons are enabled/disabled based on current engine state
- Implement a status bar info segment showing the current scene name, selected node name, and live FPS pulled from `engine.status` IPC on a 500ms interval
- Implement an `IpcEventBus` in TypeScript that demultiplexes push events arriving over the WebSocket connection and lets each panel subscribe to specific event types, decoupling panels from raw WebSocket message handling

## Impact

- Affected specs: `docs/specs/editor-panels.md`, `docs/specs/scene-system.md`, `docs/specs/play-mode.md`
- Affected code: `vscode-extension/src/panels/SceneTreePanel.ts`, `vscode-extension/src/panels/InspectorPanel.ts`, `vscode-extension/src/providers/SceneTreeProvider.ts`, `vscode-extension/src/ipc/IpcEventBus.ts`, `vscode-extension/src/statusbar/PlayControls.ts`, `vscode-extension/package.json`
- Breaking change: NO
- User benefit: Developers get a fully integrated game editor inside VSCode — they can browse and select scene nodes, edit component properties with type-safe widgets, and start/stop game simulation, all without leaving the editor or touching raw text files
