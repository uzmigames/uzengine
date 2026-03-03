# Proposal: phase4-vfx-node-editor

## Why

The VFX graph format and AI generation pipeline (tasks `phase4-vfx-graph-system` and
`phase4-vfx-ai-generator`) produce JSON files that are machine-readable and technically
correct, but the authoring experience of opening a raw JSON file in a text editor to adjust
a color gradient or a noise frequency is a significant regression from even the basic Phase 3
INI format. VFX artists are accustomed to visual tools — Unreal's Niagara, Unity's VFX
Graph, Houdini's node editor — where the structure of an effect is immediately legible at a
glance, and parameter adjustments produce instant visual feedback. The VSCode VFX Node Editor
Webview bridges this gap without requiring a standalone application: the editor lives inside
the same IDE window where the artist writes Lua scripts and browses assets, keeping context
switches to zero.

The design choice to represent emitters as vertical module stack cards rather than a free-form
graph is intentional and important. VFX emitters have a fixed execution order (spawn → init
→ update → render → lighting). A free-form node graph implies that connections are arbitrary
and that re-ordering nodes matters in complex ways, which is misleading for this domain.
The vertical stack card makes execution order visually obvious, keeps the canvas uncluttered
for effects with many emitters, and mirrors the mental model that artists already have from
Niagara's emitter stack. Side-by-side emitter cards connected by sub-emitter arrows provide
the graph-level overview without imposing graph complexity on the module-level editing flow.

The real-time preview connection is the hardest part of this task and the most valuable.
Every parameter field in the Inspector must produce a visible update in the engine viewport
within one frame (< 16 ms) of the user releasing the input. This requires the Webview-to-IPC
plumbing to serialize JSON patch diffs at the field-granularity level and send them to the
engine via `vfx.patchParam` IPC messages without triggering a full shader recompile.
Achieving this for all scalar and vector parameters (excluding structural changes like adding
modules) is the primary engineering challenge of this task.

## What Changes

- **`VFXEditorPanel`** (`vscode-extension/src/vfxEditorPanel.ts`): `WebviewPanel` host;
  handles `vfx_generate`, `vfx_refine`, `param_patch`, `set_playback_time`, and
  `capture_screenshot` messages from the Webview; routes IPC calls to the engine and MCP
  calls to the MCP server; opens on `.uzfx` file double-click (registered via
  `vscode.window.registerCustomEditorProvider`) and via Scene Tree right-click context menu.
- **`vfxMcpBridge`** (`vscode-extension/src/vfxMcpBridge.ts`): thin TypeScript module that
  dispatches `vfx_generate` and `vfx_refine` calls to the MCP server over the existing IPC
  channel (same WebSocket / named pipe used by the engine); handles progress events and
  forwards them to the Webview as `generate_progress` messages.
- **React Webview app** (`vscode-extension/src/webview/vfx-editor/`): bundled with esbuild;
  uses `reactflow` for the node canvas; key components:
  - `App.tsx`: top-level state management (current graph, selected node, inspector data),
    message router for host↔webview protocol.
  - `EmitterStackCard.tsx`: renders one emitter as a vertical card with a module list;
    drag-to-reorder modules within the stack; expand/collapse individual module rows.
  - `ModulePalette.tsx`: searchable popup showing all modules from `vfx_list_modules()`
    grouped by category; filters as user types; click adds module to current emitter.
  - `InspectorPanel.tsx`: renders parameter fields for the selected module node; scalar
    fields are `<input type="number">`; float4 fields are 4-channel color pickers; gradient
    fields are a bespoke gradient editor with draggable stops; curve fields are a bespoke
    bezier curve editor; every field change fires a `param_patch` message.
  - `AiGenerateModal.tsx`: modal with description textarea, style dropdown, color palette
    pickers, and reference image file drop; shows 3-phase progress bar during generation.
  - `AiRefinePanel.tsx`: slide-in panel with feedback textarea and "Capture Viewport" button;
    displays a diff summary after refinement completes.
  - `TimelineScrubber.tsx`: range input slider mapped to effect duration; sends
    `set_playback_time` IPC messages on drag for previewing one-shot effects at any time
    offset.
  - `TextureThumbnail.tsx`: 48×48 thumbnail for each renderer texture slot; loads from
    `res://` path via extension host file bridge; shows Nano Banana spinner during AI
    generation; clicking opens a texture picker with Asset Browser search and "Generate with
    AI" sub-action.
- **`VFXIndex`** (`src/vfx/VFXIndex.h/.cpp`): scans all `.uzfx` files in `res://vfx/` at
  project open; extracts name, tags, emitter count, module types, renderer types from the
  JSON; writes index to `res://.uzindex/vfx_index.json`; used by Asset Browser search and
  the Node Editor's preset browser; re-indexes on file system change events.
- **Asset Browser VFX section**: VSCode extension `AssetBrowserProvider` updated to show
  a "VFX" tree node under the Assets root; each `.uzfx` entry displays an animated GIF
  preview on hover (loaded from `.uzfx.meta` `preview_gif` field); drag-to-scene creates a
  `ParticleEmitter` node; right-click menu: "Open in VFX Editor", "Duplicate",
  "Generate Variant with AI".
- **`vfx.renderThumbnail` IPC handler**: engine renders the effect to an offscreen 256×256
  target at the specified time `t`, encodes as JPEG, returns base64; used by the Node Editor
  to show per-emitter thumbnail previews and by the Asset Browser GIF generation pipeline.
- **Parameter hot-patch path**: `VFXGraphSystem::set_param` applies float/float4 value
  overrides to the live cbuffer without shader recompile; structural changes (module added,
  module removed, renderer type changed) trigger a full `hot_reload` call; the Webview
  distinguishes these cases by comparing old and new graph snapshots before sending the
  IPC message.
- **Keyboard shortcuts and UX polish**: `Ctrl+Z` / `Ctrl+Shift+Z` undo/redo in the Node
  Editor (local history, 50 steps, not connected to the engine undo stack); `Ctrl+S` saves
  the graph to disk and triggers hot-reload; `Delete` key removes selected module from stack
  with confirmation; `Ctrl+D` duplicates selected emitter.
- **End-to-end integration tests** (`vscode-extension/src/test/vfxEditor.test.ts`): tests
  that `.uzfx` file open triggers graph load message, that param_patch produces correct JSON
  patch output, and that the generate/refine flows call the correct bridge methods; uses
  VSCode's extension test harness with a mock engine IPC server.

## Impact

- **Affected specs**: `docs/specs/rendering/vfx-system-ai.md` (section 7 is this task), `docs/specs/editor-panels.md` (Asset Browser VFX section addition), `docs/specs/ai-native-features.md` (VFX Node Editor surfaces the AI generation tools)
- **Affected code**: `vscode-extension/src/vfxEditorPanel.ts` (new), `vscode-extension/src/vfxMcpBridge.ts` (new), `vscode-extension/src/webview/vfx-editor/` (new React app), `vscode-extension/src/assetBrowserProvider.ts` (VFX section), `src/vfx/VFXIndex.h/.cpp` (new), `mcp-server/src/ipc/vfxHandlers.ts` (renderThumbnail handler)
- **Breaking change**: NO — the editor is an additive VSCode panel; no existing panels are modified beyond the Asset Browser receiving a new tree node; the engine C++ API gains `render_thumbnail` but no existing methods change signatures
- **User benefit**: Visual authoring of multi-emitter VFX graphs without touching JSON; instant parameter feedback in the engine viewport; one-click AI generation and iterative refinement from inside the same editor used for scripting; animated previews in the Asset Browser for all VFX effects
