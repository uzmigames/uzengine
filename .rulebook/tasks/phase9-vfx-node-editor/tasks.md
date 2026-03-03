# Tasks: phase9-vfx-node-editor

## 1. VFXEditorPanel Host

- [ ] 1.1 Create `vscode-extension/src/vfxEditorPanel.ts` implementing a `WebviewPanel` host class
- [ ] 1.2 Register `VFXEditorPanel` as a `CustomEditorProvider` for `.uzfx` files via `vscode.window.registerCustomEditorProvider`
- [ ] 1.3 Implement bidirectional message routing: Webview → host for `vfx_generate`, `vfx_refine`, `param_patch`, `set_playback_time`, `capture_screenshot`; host → Webview for `graph_loaded`, `generate_progress`, `patch_ack`, `thumbnail_result`
- [ ] 1.4 Route `param_patch` messages to `vfx.patchParam` IPC (for scalar/vector changes not requiring shader recompile)
- [ ] 1.5 Route structural change messages (module added/removed, renderer type changed) to `vfx.hotReload` IPC
- [ ] 1.6 Add Scene Tree right-click context menu "Open in VFX Editor" for nodes with `ParticleEmitter` component

## 2. vfxMcpBridge Module

- [ ] 2.1 Create `vscode-extension/src/vfxMcpBridge.ts` as a thin TypeScript module dispatching `vfx_generate` and `vfx_refine` calls to the MCP server over the existing IPC channel
- [ ] 2.2 Emit progress events as `generate_progress` messages to the Webview with `{step, percent}` fields
- [ ] 2.3 Handle MCP server error responses and format them into user-visible error messages sent to the Webview

## 3. React Webview Application

- [ ] 3.1 Create `vscode-extension/src/webview/vfx-editor/` React application directory bundled with esbuild
- [ ] 3.2 Implement `App.tsx`: top-level state management (current graph JSON, selected node ID, inspector data); message router for host↔webview protocol
- [ ] 3.3 Implement `EmitterStackCard.tsx`: render one emitter as a vertical card with ordered module list; drag-to-reorder modules; expand/collapse individual module rows
- [ ] 3.4 Implement `ModulePalette.tsx`: searchable popup listing all modules from `vfx_list_modules()` grouped by category; click adds module to current emitter
- [ ] 3.5 Implement `InspectorPanel.tsx`: scalar fields as `<input type="number">`; float4 fields as 4-channel color pickers; gradient fields as bespoke gradient editor with draggable stops; curve fields as bespoke bezier curve editor; every field change fires `param_patch`
- [ ] 3.6 Implement `AiGenerateModal.tsx`: description textarea, style dropdown, color palette pickers, reference image file drop; 3-phase progress bar; forward to `vfxMcpBridge.vfx_generate`
- [ ] 3.7 Implement `AiRefinePanel.tsx`: feedback textarea, "Capture Viewport" button (calls `capture_screenshot` message), "Apply Refinement" button; show diff summary after completion
- [ ] 3.8 Implement `TimelineScrubber.tsx`: range input mapped to effect duration; send `set_playback_time` IPC messages on drag for one-shot effect preview
- [ ] 3.9 Implement `TextureThumbnail.tsx`: 48×48 thumbnail per renderer texture slot loaded via extension host file bridge; Nano Banana spinner during AI generation; click opens texture picker with "Generate with AI" sub-action

## 4. Parameter Hot-Patch Path

- [ ] 4.1 Implement `VFXGraphSystem::set_param` in C++: apply float/float4 value overrides to live cbuffer without shader recompile
- [ ] 4.2 Implement diff logic in the Webview: compare old and new graph snapshots before sending IPC message; send `vfx.patchParam` for scalar/vector changes; send `vfx.hotReload` for structural changes

## 5. VFXIndex

- [ ] 5.1 Create `src/vfx/VFXIndex.h` and `src/vfx/VFXIndex.cpp`: scans all `.uzfx` files in `res://vfx/` at project open
- [ ] 5.2 Extract per-effect metadata: name, tags, emitter count, module types, renderer types
- [ ] 5.3 Write index to `res://.uzindex/vfx_index.json`
- [ ] 5.4 Implement `VFXIndex` file system watcher: re-index on `.uzfx` file change events using `ReadDirectoryChangesW`

## 6. Asset Browser VFX Section

- [ ] 6.1 Update `vscode-extension/src/assetBrowserProvider.ts` to show a "VFX" tree node under Assets root
- [ ] 6.2 Show animated GIF preview on hover for each `.uzfx` entry (from `.uzfx.meta` `preview_gif` field)
- [ ] 6.3 Implement drag-to-scene: dragging a `.uzfx` to Scene Tree creates a `ParticleEmitter` node
- [ ] 6.4 Add right-click menu: "Open in VFX Editor", "Duplicate", "Generate Variant with AI"

## 7. vfx.renderThumbnail IPC Handler

- [ ] 7.1 Implement `vfx.renderThumbnail(effect_path, t)` IPC handler in the engine: render effect at time `t` to offscreen 256×256 target; encode as JPEG base64
- [ ] 7.2 Add the handler to the MCP server IPC dispatch table in `mcp-server/src/ipc/vfxHandlers.ts`

## 8. Keyboard Shortcuts and UX Polish

- [ ] 8.1 Implement local 50-step undo/redo in Webview state: `Ctrl+Z` / `Ctrl+Shift+Z`; history entries are full graph JSON snapshots
- [ ] 8.2 Implement `Ctrl+S`: save graph to disk and trigger `vfx.hotReload`
- [ ] 8.3 Implement `Delete` key: remove selected module from emitter stack with confirmation dialog
- [ ] 8.4 Implement `Ctrl+D`: duplicate selected emitter and append to canvas

## 9. Testing (≥95% line + branch coverage)

- [ ] 9.1 Write `vscode-extension/src/test/vfxEditor.test.ts`: assert `.uzfx` file open triggers graph load message; assert `param_patch` produces correct JSON diff and routes to correct IPC handler
- [ ] 9.2 Write `vscode-extension/src/test/vfxMcpBridge.test.ts`: mock MCP server; assert `vfx_generate` call forwards correct parameters and progress events reach Webview
- [ ] 9.3 Write `vscode-extension/src/test/VFXIndex.test.ts`: mock file system with 5 `.uzfx` files; assert index contains correct metadata; assert re-index fires on file change event
- [ ] 9.4 Write `mcp-server/src/test/vfxHandlers.test.ts`: call `vfx.renderThumbnail` handler with mock engine; assert JPEG base64 string returned with non-zero length
- [ ] 9.5 Verify ≥95% line and branch coverage across all `vscode-extension/src/vfxEditorPanel.ts`, `vfxMcpBridge.ts`, and `src/vfx/VFXIndex.cpp` using Jest --coverage (TypeScript) and OpenCppCoverage (C++)
