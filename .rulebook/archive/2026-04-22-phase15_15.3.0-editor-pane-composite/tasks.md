## 1. Implementation

- [x] 1.1 Created `runtime/engine/vfx/editor/vfx_editor_pane_widget.{h,cpp}` — `VfxEditorPaneTab { Stack, Parameters, Emitters }` + `VfxEditorPaneWidget { Bind, SetActiveTab, ActiveTab, HandleClick, Render }` with `onTabChanged` callback
- [x] 1.2 Render hosts an internal `VfxTabStripWidget` auto-populated to three tabs (Stack / Parameters / Emitters) on first access; tab strip height tunable via `tabStripHeight = 28.0f`
- [x] 1.3 Body rect = `bounds` minus the tab strip; active sub-panel renders into the body. Emitters tab splits the body 50/50 between the tree widget and the system overview
- [x] 1.4 `SetActiveTab` fires `onTabChanged(newTab)` only when the tab actually changes; same-tab set is a no-op (verified by `SameTabSetDoesNotFireCallback` test)
- [x] 1.5 `Bind(stack, params, tree, overview)` stores raw pointers; null pointer renders a placeholder label ("Stack: no widget bound" etc.) in `textDim`

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
