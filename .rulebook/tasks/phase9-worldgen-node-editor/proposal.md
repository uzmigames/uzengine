# Proposal: phase4-worldgen-node-editor

## Why

The WorldGen compiler and MMORPG nodes provide a powerful generation backend, but without a
visual authoring interface they are only accessible to designers who are comfortable editing
JSON directly. This is a small fraction of any game development team. Level designers and
world artists — who are the primary users of terrain generation tools — work visually: they
need to see what a node does, understand how it connects to other nodes, and observe the
effect of a parameter change in real time. The `.uzworldgen` JSON format is intentionally
AI-legible and human-readable, but a graph with 15 nodes and 200 parameter lines is not
comfortably authored in a text editor when the goal is to understand spatial relationships
between plateau placement, erosion patterns, and biome boundaries.

The WorldGen Panel Webview solves this with a node graph canvas that makes the execution
structure of a world generation graph immediately legible. The node color coding (gold headers
for MMORPG nodes, blue for generators, green for modifiers) ensures that a designer can scan
a graph and instantly understand which nodes encode gameplay constraints versus which provide
visual variation. The Properties Inspector panel allows parameter editing with appropriate
input widgets — range sliders for floats, steppers for integers, dynamic lists for the
`target_heights` array — rather than raw JSON values. Most importantly, every parameter
change triggers an immediate 256×256 recompile and updates the Three.js heightmap preview,
giving designers sub-second visual feedback on how a parameter change affects the terrain
shape. This interactive loop is the core value of the WorldGen Panel.

The Biome Editor addresses a secondary authoring pain point: biomes in the `.uzworldgen` format
are JSON dictionaries with nested foliage arrays and splatmap layer references. Visually
inspecting or comparing two biomes' properties in JSON requires careful manual reading. The
Biome Editor renders each biome as a card with a color swatch (ambient_tint), a height-range
bar, a foliage mesh list, and density sliders — a format that game artists find natural.
Editing biome density directly from a slider that updates the Three.js preview removes the
entire authoring loop of "edit JSON → save → recompile → look at result" and replaces it
with a direct manipulation interface. The AI Refine integration within the panel means that
when a designer looks at the Three.js preview and thinks "the plateaus should be flatter",
they can type that observation into the AI Refine panel and have it translated into a
parameter change automatically, closing the feedback loop without requiring them to know
which node parameter controls plateau flatness.

## What Changes

- **`WorldGenPanel.ts`** (`vscode-extension/src/worldgenPanel.ts`) — `WebviewPanel` host;
  opens via command `uzengine.openWorldGen`, via `.uzworldgen` file double-click (registered
  via `vscode.window.registerCustomEditorProvider`), and via Terrain node right-click in Scene
  Tree; manages bidirectional message protocol between Webview and extension host; routes
  `worldgen.generate` messages to `WorldGenCompiler.exe` process (spawned as child process);
  routes `world_generate` and `world_refine` messages to `worldGenMcpBridge.ts`; routes
  `engine.screenshot` and `terrain.hotReload` messages to the engine IPC channel; handles
  `worldgen.preview`, `worldgen.progress`, `worldgen.complete` events from the compiler and
  forwards them to the Webview

- **`worldGenMcpBridge.ts`** (`vscode-extension/src/worldGenMcpBridge.ts`) — TypeScript
  module that wraps `world_generate`, `world_refine`, and `world_list_templates` MCP calls;
  emits progress events as `generate_progress` messages to the Webview; handles error
  responses from the MCP server and formats them into user-visible error messages in the panel

- **React Webview application** (`vscode-extension/src/webview/worldgen-editor/`) — bundled
  with esbuild; uses `reactflow` for node canvas; key components:
  - `App.tsx`: top-level state (current graph JSON, selected node ID, compile status, biome
    list); message router for all host↔webview protocol messages; saves graph to disk on
    Ctrl+S with 2-second debounce
  - `NodeCanvas.tsx`: reactflow canvas with custom node renderers per node type; node color
    coding by category (MMORPG=gold, generator=blue, modifier=green, combiner=purple,
    biome=teal, output=red); port type indicators; per-node 64×64 preview thumbnail updated
    from `worldgen.progress` events; `Ctrl+Space` node search palette; rubber-band selection;
    auto-layout button (hierarchical DAG layout using dagre)
  - `InspectorPanel.tsx`: parameter editor for the selected node; renders appropriate input
    widget per parameter type: `<input type="range">` + number for float, stepper for int,
    toggle for bool, dropdown for string enum, `<input type="color">` for RGB color, dynamic
    row list for float[] arrays (target_heights, positions), sub-form for FoliageDef[] with
    mesh file picker button (opens Asset Browser via `assetBrowser.pick` IPC); every field
    change fires `param_patch` message; 300ms debounce to batch rapid slider drag events into
    single compile triggers
  - `PreviewPanel.tsx`: Three.js canvas at the panel bottom; creates a `PlaneGeometry`
    (256×256 segments) and updates vertex Y positions from the compiler's heightmap preview
    data; splatmap channels mapped to vertex colors; orbit controls (left drag = rotate, scroll
    = zoom); water plane at `water_level` height; toggle: "Height" (greyscale) / "Biome"
    (color-coded by biome_map) / "Splatmap" (RGBA channels) view modes; updates triggered by
    `worldgen.preview` IPC events
  - `BiomeEditor.tsx`: tabbed panel showing all biomes as cards; each card: color swatch
    (editable via color picker), name field, horizontal height-range bar (drag handles to
    adjust range), slope indicator, foliage mesh list with density sliders and max_slope
    inputs; "Add Biome" button; "Delete Biome" button with confirmation; changes update the
    `biomes{}` dict in graph state and trigger a recompile
  - `AiGenerateModal.tsx`: modal dialog with description textarea (multiline, 500 char limit),
    style dropdown (mmorpg/survival/city/island/dungeon), world_size_km dropdown, seed input
    with dice randomizer button, plateau count stepper, level count stepper, resolution dropdown,
    biome checkbox grid (pre-populated from `world_list_templates` response); 5-step progress
    bar during generation; cancel button; on completion: closes modal, loads generated graph
    into canvas, triggers preview compile
  - `AiRefinePanel.tsx`: slide-in right panel (250px); "Capture Current Viewport" button
    triggers `engine.screenshot` IPC and stores base64 in state; feedback textarea; "Apply
    Refinement" button sends `world_refine` MCP call; after completion: shows diff summary
    (list of changed node/parameter pairs with old→new values); "Accept" button (keeps
    changes), "Revert" button (restores previous graph)
  - `ToolBar.tsx`: New, Open, Save, Generate (F5), AI Generate, AI Refine, Resolution selector,
    Seed input, Export to .uzterrain; tooltip labels on all buttons; keyboard shortcuts
    displayed in tooltips

- **Auto-layout** (`vscode-extension/src/webview/worldgen-editor/dagre-layout.ts`) — uses
  `dagre` library to compute hierarchical DAG layout positions for all nodes; triggered by
  "Auto-Layout" button or on first open of a graph that lacks stored `position` fields;
  assigns positions top-to-bottom in topological order with 120px horizontal spacing and 180px
  vertical spacing; preserves user-placed positions if `position` fields are present in JSON

- **Local undo/redo** — 50-step history in Webview state (not connected to engine undo stack);
  `Ctrl+Z` restores previous graph state; `Ctrl+Shift+Z` re-applies; history entries are full
  graph JSON snapshots (compressed via JSON diff for memory efficiency); undo/redo is local
  to the WorldGen Panel — it does not interact with the VSCode global undo or the engine
  `UndoStack`

- **Terrain node integration in Scene Tree** — `SceneTreeProvider` updated to show "Open
  WorldGen Editor" in the right-click context menu for `Terrain` nodes that have an associated
  `.uzworldgen` file (detected via the `worldgen_source` field in the `.uzterrain` INI, written
  by `TerrainOutput`); this closes the loop: from the Scene Tree, a designer can navigate
  directly from a terrain in the scene to the WorldGen graph that generated it

- **Asset Browser `.uzworldgen` entry** — `AssetBrowserProvider` updated to show a
  "WorldGen Graphs" tree node; each `.uzworldgen` file entry shows a thumbnail (the
  `{name}_preview.png` generated during last compile); double-click opens WorldGen Panel;
  right-click menu: "Open in WorldGen Editor", "Duplicate", "Generate Variant with AI"

- **End-to-end extension tests** (`vscode-extension/src/test/worldgenPanel.test.ts`) — uses
  VSCode extension test harness with mock WorldGenCompiler (returns canned PNG data), mock
  engine IPC (returns canned screenshot), mock MCP server (returns canned `.uzworldgen`
  JSON); tests cover: `.uzworldgen` file open loads graph into canvas, param_patch produces
  correct JSON mutation in graph state, AI Generate flow calls worldGenMcpBridge with correct
  parameters, worldgen.preview event updates PreviewPanel vertex data, Biome Editor changes
  propagate to graph state, Ctrl+Z restores previous graph state after param change

## Impact

- **Affected specs**: `docs/specs/engine/procedural-world-gen.md` (this task implements
  §§ 6 fully — all WorldGen Panel UI components), `docs/specs/editor/editor-panels.md`
  (WorldGen Panel added to the editor panel registry), `docs/specs/engine/ai-native-features.md`
  (WorldGen Panel surfaces `world_generate` and `world_refine` with a full GUI)
- **Affected code**: `vscode-extension/src/worldgenPanel.ts` (new),
  `vscode-extension/src/worldGenMcpBridge.ts` (new),
  `vscode-extension/src/webview/worldgen-editor/` (new React app, ~2500 lines),
  `vscode-extension/src/sceneTreeProvider.ts` (add "Open WorldGen Editor" context menu item),
  `vscode-extension/src/assetBrowserProvider.ts` (WorldGen Graphs tree section),
  `vscode-extension/src/test/worldgenPanel.test.ts` (new test suite)
- **Breaking change**: NO — the WorldGen Panel is a new additive VSCode panel; no existing
  panels are modified beyond receiving new context menu items; the Scene Tree and Asset Browser
  changes are purely additive (new menu items and tree sections); no engine C++ changes required
- **User benefit**: World designers with no JSON or node-graph experience can open the WorldGen
  Panel, click "AI Generate", type a description, and have a playable terrain previewed in Three.js
  within 60 seconds; experienced designers get a full node graph canvas with sub-second parameter
  feedback, a visual biome editor, and AI-assisted refinement — a complete authoring environment
  for procedural worlds without leaving VSCode
