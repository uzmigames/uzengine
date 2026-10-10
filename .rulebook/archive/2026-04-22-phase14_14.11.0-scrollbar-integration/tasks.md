## 1. Implementation

- [x] 1.1 Added `VfxScrollBar m_stackScrollbar` + `m_paramsScrollbar` + `m_pickerScrollbar` + `m_overviewScrollbar` members to `VfxEditorOverlay`
- [x] 1.2 Stack panel scrollbar wired in `BuildNiagaraUI` — `SetView(stackBounds, m_stackPanel.ComputeContentHeight())`, `SetScrollY(m_stackPanel.ScrollOffset())`, render, then `m_stackPanel.SetScrollOffset(m_stackScrollbar.ScrollY())` so wheel + drag stay consistent
- [x] 1.3 Parameter inspector scrollbar wired in the `VfxCenterMode::Parameters` branch — content height derived from the last `ComputeLayout` extent; bar rendered only when `contentH > panel.h`
- [x] 1.4 Module picker scrollbar wired when the dialog is open — content height derived from the picker's layout extent within its anchor bounds
- [x] 1.5 System overview scrollbar wired — content height approximated from `emitterNames.size() * 28 px` row pitch
- [x] 1.6 Wheel delta routing: the existing `m_scrollDelta` path already feeds stack-panel scroll via `m_stackPanel.SetScrollOffset`; scrollbar reads back from that offset so wheel + bar geometry stay in sync. Inspector / picker / overview read wheel via the same pattern when mounted with explicit scroll state

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
