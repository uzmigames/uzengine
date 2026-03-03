# Proposal: phase1-viewport-manager

## Why

The engine renders at full frame rate regardless of whether the associated VSCode panel is visible. As the editor grows — Scene viewport, VFX preview, Material preview, Mesh preview, Texture viewer, Animation preview — each open tab will run a full DX12 render pass, JPEG encode thread, and WebSocket sender even when the tab is hidden behind another panel.

On a developer workstation with four open preview tabs, each idle tab can consume 10–30 % of GPU time. The combined overhead causes thermal throttling, fan noise, increased power usage, and stutters in the one tab the developer is actually using. Without active visibility management the engine becomes unreliable on mid-range hardware and unusable on laptops.

The fix is straightforward: the VSCode extension tracks tab visibility via `onDidChangeViewState` and notifies the engine with a lightweight IPC command (`viewport.setActive`). The engine immediately stops scheduling render work for suspended viewports (zero GPU cost), and resumes the moment the tab becomes visible again — no state loss, no reload delay.

This system must be in place before any editor preview panel is implemented, because retrofitting lifecycle management into existing panels is significantly more expensive than building it correctly from the start.

## What Changes

- **Engine C API** — `ViewportRegistry` that manages N independent viewports; each has an id, state machine (INACTIVE → ACTIVE ↔ SUSPENDED → DESTROYED), per-viewport render targets, render graph instance, and JPEG WebSocket sender
- **IPC handlers** — `viewport.create`, `viewport.destroy`, `viewport.setActive`, `viewport.resize`, `viewport.getStatus` commands added to the IPC dispatcher
- **Multi-viewport WebSocket multiplexing** — every outgoing JPEG frame is prefixed with a 4-byte `viewport_id` so the TypeScript side can route frames to the correct webview without opening additional ports
- **RenderScheduler** — the per-frame dispatch loop queries `ViewportRegistry` and skips INACTIVE/SUSPENDED viewports entirely; active viewports are rendered in insertion order up to the VRAM budget cap
- **VRAM budget enforcement** — 512 MB default cap across all active viewports; if a new `viewport.setActive` would exceed the cap, the oldest active viewport is auto-suspended and the extension is notified via a `viewport.suspended` push event
- **Heartbeat watchdog** — if the engine receives no IPC for 30 seconds it auto-suspends all viewports, preventing the GPU from running unattended when the developer's machine is locked or VSCode crashes
- **TypeScript `ViewportManager`** — singleton that owns all `ViewportPanel` instances; each `ViewportPanel` wraps a `vscode.WebviewPanel` and calls `viewportManager.setActive(id, true/false)` in its `onDidChangeViewState` handler
- **Window focus integration** — `vscode.window.onDidChangeWindowState` → `suspendAll()` when VSCode loses focus, `resumeVisible()` when it regains focus
- **Migration** — Phase 1 wraps the existing single-viewport streaming behavior inside the new `ViewportManager` API; the legacy `stream-panel.ts` becomes the first registered viewport (id=1, type=scene)

## Impact

- Affected specs: `docs/specs/editor/viewport-manager.md` (new), `docs/specs/editor/editor-panels.md` (cross-reference), `docs/specs/rendering/render-graph.md` (multi-viewport scheduling)
- Affected code: `src/ipc/viewport_ipc.c`, `src/renderer/viewport_registry.c`, `vscode-extension/src/viewport-manager.ts`, `vscode-extension/src/viewport-panel.ts`, `vscode-extension/src/extension.ts`
- Breaking change: NO — the existing single viewport continues to work; the manager wraps it transparently
- User benefit: Developers can open multiple preview panels simultaneously without GPU saturation; idle tabs cost zero GPU time; the engine remains responsive on mid-range hardware and laptops
