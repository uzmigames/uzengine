## 1. Implementation

- [x] 1.1 Added `VfxToolbarWidget m_toolbar` + `VfxStatusBarWidget m_statusBar` + `bool m_toolbarInitialized` members to `VfxEditorOverlay`
- [x] 1.2 Extended `VfxEditorComposition` layout: new `toolbarBar` strip (28 px) below the menu bar, new `statusBar` strip (22 px) above the timeline bar; middle content height subtracts both strips
- [x] 1.3 Wired toolbar buttons lazily on the first frame — `Save` (id 1) / `Restart` (id 2) / separator / `Play` (id 3, Toggle kind) / `Thumb` (id 4)
- [x] 1.4 Toolbar `HitTest` + click dispatch in `BuildNiagaraUI` routes to existing overlay callbacks (`onMenuCommand(File_Save)`, `onRestart()`, Play toggled via `SetToggled`, `onRecaptureThumbnail`)
- [x] 1.5 Status bar per-frame feed: `SetFps(60.0f)` placeholder, `SetParticleCount(cpuAliveCount, maxParticles)` from the bound emitter, `SetDrawCalls(0)` / `SetVramUsed(0, 0)` placeholder, `SetDirty(composition.UndoStack().CanUndo())` driven from the existing undo stack
- [x] 1.6 Status bar `Tick(dt)` is callable from any editor-update path via the public accessor; current render pass produces persistent chips without transient notifications, so Tick drives only the notification auto-dismiss when the host pushes one
- [x] 1.7 Toolbar + status bar rendered inside `BuildNiagaraUI` after the panel-background pass so they land above the panels; dispatch runs before render so clicks inside the same frame take effect
- [x] 1.8 Composition layout math updated: `middleHeight = screenH - menuH - toolbarH - statusH - timelineH`; `middleY = menuH + toolbarH`; verified by `MiddlePanelShrinksForNewStrips` + `StackPanelStartsBelowToolbar` tests

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
