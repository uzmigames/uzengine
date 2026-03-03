## 1. Engine — ViewportRegistry

- [ ] 1.1 Define `ViewportId` (uint32_t), `ViewportState` enum (INACTIVE/ACTIVE/SUSPENDED/DESTROYED) in `src/renderer/viewport_registry.h`
- [ ] 1.2 Define `ViewportType` enum (scene, vfx_preview, material, mesh, texture, prefab, animation)
- [ ] 1.3 Define `ViewportDesc` struct (type, width, height, target_fps, jpeg_quality, debug_name)
- [ ] 1.4 Define `Viewport` struct (id, state, desc, color_target, depth_target, render_graph, ws_client_id, frame_count, vram_bytes)
- [ ] 1.5 Implement `viewport_registry_init` / `viewport_registry_shutdown`
- [ ] 1.6 Implement `viewport_create(desc) → ViewportId` — allocates render targets via RHI, initialises render graph instance
- [ ] 1.7 Implement `viewport_destroy(id)` — releases render targets, closes WebSocket sender, frees slot
- [ ] 1.8 Implement `viewport_set_active(id, bool)` — transitions state machine; resumes or suspends render targets
- [ ] 1.9 Implement `viewport_resize(id, w, h)` — recreates render targets at new resolution
- [ ] 1.10 Implement `viewport_get_status(id, out_status)` — fills status struct (state, fps, vram_bytes)
- [ ] 1.11 Implement `viewport_registry_tick()` — called each engine frame; iterates viewports, skips INACTIVE/SUSPENDED

## 2. Engine — RenderScheduler Integration

- [ ] 2.1 Modify main render loop to call `viewport_registry_tick()` instead of single-viewport render
- [ ] 2.2 Per-active-viewport: bind render targets, execute render graph, JPEG encode, send WebSocket frame with 4-byte `viewport_id` prefix
- [ ] 2.3 VRAM budget check before activating: sum `vram_bytes` of all ACTIVE viewports; if > 512 MB, suspend oldest and emit `viewport.suspended` push event
- [ ] 2.4 Max active cap: reject activation if 4 viewports already ACTIVE; return error to IPC caller

## 3. Engine — Heartbeat Watchdog

- [ ] 3.1 Track timestamp of last IPC message received
- [ ] 3.2 In `viewport_registry_tick()`: if `now - last_ipc > 30s`, call `viewport_suspend_all()`
- [ ] 3.3 Log watchdog trigger at INFO level

## 4. Engine — IPC Handlers

- [ ] 4.1 Add `viewport.create` handler — parses JSON config, calls `viewport_create`, returns `{ viewport_id }`
- [ ] 4.2 Add `viewport.destroy` handler — calls `viewport_destroy(id)`
- [ ] 4.3 Add `viewport.setActive` handler — calls `viewport_set_active(id, active)`
- [ ] 4.4 Add `viewport.resize` handler — calls `viewport_resize(id, w, h)`
- [ ] 4.5 Add `viewport.getStatus` handler — returns JSON status for viewport id
- [ ] 4.6 Emit `viewport.suspended` push event when auto-suspend triggers (VRAM cap or watchdog)

## 5. Engine — Multi-viewport WebSocket Multiplexing

- [ ] 5.1 Modify JPEG sender to prepend 4-byte little-endian `viewport_id` before every JPEG frame
- [ ] 5.2 Verify frame delimiter protocol unchanged (length prefix still valid after viewport_id prefix)

## 6. TypeScript — ViewportManager

- [ ] 6.1 Create `vscode-extension/src/viewport-manager.ts` — singleton class `ViewportManager`
- [ ] 6.2 `ViewportManager.create(type, config) → Promise<ViewportPanel>` — sends `viewport.create` IPC
- [ ] 6.3 `ViewportManager.close(id)` — sends `viewport.destroy` IPC, disposes panel
- [ ] 6.4 `ViewportManager.setActive(id, active)` — sends `viewport.setActive` IPC
- [ ] 6.5 `ViewportManager.suspendAll()` — calls `setActive(id, false)` for all panels
- [ ] 6.6 `ViewportManager.resumeVisible()` — calls `setActive(id, true)` for panels with `webviewPanel.visible === true`
- [ ] 6.7 WebSocket `onmessage`: read 4-byte `viewport_id`, dispatch JPEG blob to correct `ViewportPanel`

## 7. TypeScript — ViewportPanel

- [ ] 7.1 Create `vscode-extension/src/viewport-panel.ts` — class `ViewportPanel`
- [ ] 7.2 Wraps `vscode.WebviewPanel`; constructor receives `viewportId` and `ViewportManager` ref
- [ ] 7.3 `onDidChangeViewState`: call `viewportManager.setActive(id, e.webviewPanel.active)`
- [ ] 7.4 `onDidDispose`: call `viewportManager.close(id)`
- [ ] 7.5 `receiveFrame(jpegBlob)`: post JPEG blob to webview for canvas rendering

## 8. TypeScript — Window Focus Integration

- [ ] 8.1 In `extension.ts` activate: subscribe `vscode.window.onDidChangeWindowState`
- [ ] 8.2 `focused === false` → `viewportManager.suspendAll()`
- [ ] 8.3 `focused === true` → `viewportManager.resumeVisible()`

## 9. TypeScript — Migration (Legacy Panel)

- [ ] 9.1 Refactor existing `stream-panel.ts` to extend `ViewportPanel`
- [ ] 9.2 On extension activation, register legacy panel as viewport id=1, type=scene
- [ ] 9.3 Verify existing streaming behaviour unchanged after refactor

## 10. Tests

- [ ] 10.1 Unit test: `viewport_create` → state is INACTIVE, id != 0
- [ ] 10.2 Unit test: `viewport_set_active(id, true)` → state is ACTIVE
- [ ] 10.3 Unit test: `viewport_set_active(id, false)` → state is SUSPENDED
- [ ] 10.4 Unit test: `viewport_destroy(id)` → state is DESTROYED, slot freed
- [ ] 10.5 Unit test: VRAM cap — creating 5 viewports auto-suspends oldest
- [ ] 10.6 Unit test: max active cap — 5th setActive returns error
- [ ] 10.7 Unit test: watchdog — mock 31s elapsed, all viewports SUSPENDED
- [ ] 10.8 Integration test: IPC `viewport.create` → engine returns viewport_id
- [ ] 10.9 Integration test: IPC `viewport.setActive` → engine render loop skips suspended viewport
- [ ] 10.10 Integration test: WebSocket frame has correct 4-byte viewport_id prefix

## 11. Documentation

- [ ] 11.1 Verify `docs/specs/editor/viewport-manager.md` matches implementation
- [ ] 11.2 Add `viewport-manager.md` row to `docs/specs/editor/INDEX.md` (create if absent)
- [ ] 11.3 Update `docs/specs/editor/editor-panels.md` with cross-reference to viewport-manager
