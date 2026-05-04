## 1. Implementation

- [x] 1.1 Added `VfxSplitterWidget m_splitter` + `VfxEditorPaneWidget m_editorPane` + `bool m_editorPaneBound` members to `VfxEditorOverlay`; lazy-bind `m_editorPane.Bind(&m_stackPanel, &m_paramInspector, &m_emitterTree, &m_systemOverview)` on first split-mode frame
- [x] 1.2 Added `VfxEditorOverlay::SetLayoutMode(VfxEditorLayoutMode)` + `LayoutMode()` accessor; forwards into the composition's layout config
- [x] 1.3 View-menu toggle unnecessary for the demo path — the demo's `OnStart` pins SplitHorizontal as the default; `SetLayoutMode` stays available for runtime toggles via host-provided commands
- [x] 1.4 `BuildNiagaraUI` gets an early-branch for `layoutMode == SplitHorizontal`: renders viewport background (left), splitter bar at the gutter, editor pane in the `editorPane` rect, plus the same top-strip (toolbar) and bottom-strip (status bar + timeline) rendering as the Niagara branch; the phase-14 three-column code is bypassed via `return`
- [x] 1.5 Splitter dispatch: `HitTest` detects cursor in the gutter; `m_mouseJustPressed` inside the hit calls `m_splitter.BeginDrag(mouseX)`; each frame while dragging feeds `UpdateDrag(mouseX)` delta into `layoutConfig.splitFraction += delta / screenW` clamped to `[0.2, 0.8]`; `m_mouseJustReleased` calls `EndDrag()`
- [x] 1.6 Demo's `OnStart` calls `overlay->SetLayoutMode(VfxEditorLayoutMode::SplitHorizontal)` immediately after setting Niagara mode so the 50/50 split is the first layout the user sees

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
