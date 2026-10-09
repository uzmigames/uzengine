## 1. Implementation

- [x] 1.1 Added `VfxTreeWidget m_emitterTree` + `bool m_emitterRootExpanded = true` members to `VfxEditorOverlay`
- [x] 1.2 `BuildNiagaraUI` rebuilds the tree every frame from `m_emitterNames` — root "Emitters" (id 0, depth 0, `hasChildren=true`, `expanded=m_emitterRootExpanded`, `textSecondary` tint) + per-emitter children (id `i+1`, depth 1, `VfxUiIcon::Dot`, `stateSuccess` tint)
- [x] 1.3 Reserved a 140-px slice below the right-panel "Emitters" header for the tree; system-overview render shifted below (`rightContent.y += kTreeH`)
- [x] 1.4 Hover + click dispatch via `m_emitterTree.HitTest`: `ExpandArrow` on root id 0 flips `m_emitterRootExpanded`; `Row` hit on id >= 1 fires `onSelectEmitter(id - 1)` + updates `m_activeEmitterIdx`
- [x] 1.5 `SetSelectedId(m_activeEmitterIdx + 1)` each frame keeps the tree highlight synced with the active emitter

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
