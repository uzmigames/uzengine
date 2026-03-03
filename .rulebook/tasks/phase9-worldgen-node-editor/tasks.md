# Tasks: phase9-worldgen-node-editor

## 1. WorldGenPanel Host

- [ ] 1.1 Create `vscode-extension/src/worldgenPanel.ts` as a `WebviewPanel` host
- [ ] 1.2 Register as `CustomEditorProvider` for `.uzworldgen` files via `vscode.window.registerCustomEditorProvider`
- [ ] 1.3 Register command `uzengine.openWorldGen` to open the panel from the command palette
- [ ] 1.4 Implement bidirectional message protocol: route `worldgen.generate` messages to `WorldGenCompiler.exe` child process; route `world_generate` and `world_refine` to `worldGenMcpBridge.ts`; route `engine.screenshot` and `terrain.hotReload` to engine IPC channel
- [ ] 1.5 Forward `worldgen.preview`, `worldgen.progress`, `worldgen.complete` events from the compiler child process to the Webview

## 2. worldGenMcpBridge Module

- [ ] 2.1 Create `vscode-extension/src/worldGenMcpBridge.ts` wrapping `world_generate`, `world_refine`, and `world_list_templates` MCP calls
- [ ] 2.2 Emit progress events as `generate_progress` messages to the Webview with `{step, percent}` fields
- [ ] 2.3 Format MCP server error responses into user-visible error messages for the panel

## 3. React Webview Application — Core

- [ ] 3.1 Create `vscode-extension/src/webview/worldgen-editor/` React app bundled with esbuild; use `reactflow` for the node canvas
- [ ] 3.2 Implement `App.tsx`: top-level state (graph JSON, selected node ID, compile status, biome list); message router for host↔webview protocol; `Ctrl+S` saves graph with 2-second debounce
- [ ] 3.3 Implement `ToolBar.tsx`: New, Open, Save, Generate (F5), AI Generate, AI Refine, Resolution selector, Seed input, Export to `.uzterrain`; keyboard shortcut tooltips on all buttons

## 4. NodeCanvas Component

- [ ] 4.1 Implement `NodeCanvas.tsx`: reactflow canvas with custom node renderers per node type
- [ ] 4.2 Apply node color coding by category: MMORPG=gold headers, generator=blue, modifier=green, combiner=purple, biome=teal, output=red
- [ ] 4.3 Display per-node 64×64 preview thumbnail updated from `worldgen.progress` events (base64 PNG)
- [ ] 4.4 Implement `Ctrl+Space` node search palette with fuzzy matching on node type names
- [ ] 4.5 Implement rubber-band selection for multi-node operations
- [ ] 4.6 Implement auto-layout button using `dagre` hierarchical DAG layout: top-to-bottom topological order, 120px horizontal / 180px vertical spacing; preserve user-placed `position` fields if present

## 5. InspectorPanel Component

- [ ] 5.1 Implement `InspectorPanel.tsx`: parameter editor for selected node with type-appropriate widgets
- [ ] 5.2 Render `<input type="range">` + number input for float parameters; stepper for int; toggle for bool; dropdown for string enum; `<input type="color">` for RGB
- [ ] 5.3 Implement dynamic row list editor for float[] arrays (e.g., `target_heights`, `positions`)
- [ ] 5.4 Implement sub-form for `FoliageDef[]` with mesh file picker button (opens Asset Browser via `assetBrowser.pick` IPC)
- [ ] 5.5 Apply 300ms debounce to batch rapid slider drag events into single compile triggers
- [ ] 5.6 Every field change fires a `param_patch` message with the new value

## 6. PreviewPanel Component

- [ ] 6.1 Implement `PreviewPanel.tsx`: Three.js canvas at panel bottom with `PlaneGeometry` (256×256 segments)
- [ ] 6.2 Update vertex Y positions from compiler's heightmap preview data on `worldgen.preview` IPC events
- [ ] 6.3 Map splatmap channels to vertex colors
- [ ] 6.4 Implement orbit controls: left drag = rotate, scroll = zoom
- [ ] 6.5 Add water plane mesh at `water_level` height
- [ ] 6.6 Implement view mode toggle: "Height" (greyscale), "Biome" (color-coded by biome_map), "Splatmap" (RGBA channels)

## 7. BiomeEditor Component

- [ ] 7.1 Implement `BiomeEditor.tsx`: tabbed panel showing all biomes as cards
- [ ] 7.2 Each biome card: color swatch (editable via color picker), name field, horizontal height-range bar with draggable handles, slope indicator, foliage mesh list with density sliders and `max_slope` inputs
- [ ] 7.3 Implement "Add Biome" button; "Delete Biome" button with confirmation dialog
- [ ] 7.4 Biome changes update the `biomes{}` dict in graph state and trigger a 256×256 recompile

## 8. AI Modal Components and Undo/Redo

- [ ] 8.1 Implement `AiGenerateModal.tsx`: description textarea (500 char limit), style dropdown, world_size_km dropdown, seed input with dice randomizer, plateau/level count steppers, resolution dropdown, biome checkbox grid; 5-step progress bar; cancel button
- [ ] 8.2 Implement `AiRefinePanel.tsx`: "Capture Current Viewport" button triggers `engine.screenshot` IPC; feedback textarea; "Apply Refinement" button; shows diff summary with old→new value pairs; "Accept" and "Revert" buttons
- [ ] 8.3 Implement local 50-step undo/redo in Webview state (full graph JSON snapshots with JSON diff compression): `Ctrl+Z` / `Ctrl+Shift+Z`
- [ ] 8.4 Implement `dagre-layout.ts` auto-layout module using `dagre` library for DAG position computation

## 9. Scene Tree and Asset Browser Integration

- [ ] 9.1 Update `vscode-extension/src/sceneTreeProvider.ts`: add "Open WorldGen Editor" right-click menu item for `Terrain` nodes that have `worldgen_source` field in `.uzterrain` INI
- [ ] 9.2 Update `vscode-extension/src/assetBrowserProvider.ts`: add "WorldGen Graphs" tree section showing all `.uzworldgen` files with thumbnails
- [ ] 9.3 Implement double-click on `.uzworldgen` asset to open `WorldGenPanel`; right-click: "Duplicate", "Generate Variant with AI"

## 10. Testing (≥95% line + branch coverage)

- [ ] 10.1 Write `vscode-extension/src/test/worldgenPanel.test.ts`: assert `.uzworldgen` file open loads graph into canvas; assert `param_patch` produces correct JSON mutation in graph state
- [ ] 10.2 Write AI Generate flow test: assert `AiGenerateModal` submit calls `worldGenMcpBridge` with correct parameters
- [ ] 10.3 Write preview update test: assert `worldgen.preview` event updates `PreviewPanel` vertex Y data
- [ ] 10.4 Write `BiomeEditor` propagation test: assert biome card color change propagates to graph state `biomes{}` dict
- [ ] 10.5 Write undo/redo test: change a param; `Ctrl+Z`; assert graph state restored; `Ctrl+Shift+Z`; assert change re-applied
- [ ] 10.6 Verify ≥95% line and branch coverage across all `vscode-extension/src/worldgenPanel.ts`, `worldGenMcpBridge.ts`, and Webview app files using Jest --coverage
