## 1. Implementation

- [x] 1.1 Created `vfx_system_overview_widget.{h,cpp}` with `VfxSystemOverviewWidget` class (Bind to `const std::vector<VfxOverviewEmitterView>*`, ComputeLayout, HitTest, Render, BeginDrag/EndDrag/CancelDrag/IsDragging, selection API, all 6 callbacks). Data-view pattern keeps the widget decoupled from ParticleEmitter so tests need no GPU infrastructure.
- [x] 1.2 Defined `VfxOverviewItemType` enum with all 11 values + `VfxOverviewEmitterView` data struct (name, aliveCount, isEmitting, isEnabled)
- [x] 1.3 `ComputeLayout` produces Row + 8 per-row sub-rects (DragHandle, Checkbox, StatusDot, NameLabel, ParticleCount, 4 action buttons) plus trailing AddEmitterRow
- [x] 1.4 `HitTest` uses enum-value priority (Row=1 lowest, AddEmitterRow=11 highest) so action-button hits beat the row body
- [x] 1.5 `Render` via `VfxTheme()`: StatusDot colored by runtime state (stateSuccess when emitting+aliveCount>0, stateWarning when paused+aliveCount>0, textDim otherwise); Checkbox inner fill stateSuccess/cardBg; 3-px accent stripe on selected row; 1-px borderSubtle divider; action glyphs via `vfx_editor_icons.h`
- [x] 1.6 Callbacks added: `onEmitterSelected`, `onEmitterEnableToggled(idx, bool)`, `onEmitterMoved(src, dst)`, `onEmitterDuplicated`, `onEmitterDeleted`, `onAddEmitterRequested` — all caller-applied
- [x] 1.7 Added `BindSystemOverview(VfxSystemOverviewWidget*)` to `VfxEditorComposition` with matching accessor
- [x] 1.8 Wired `VfxEditorOverlay::BuildNiagaraUI` — right panel now:
  - (a) rebuilds `m_overviewViews` vector from emitter names + active `isEmitting`/`cpuAliveCount`
  - (b) dispatches clicks on each hit kind (Row/NameLabel/DragHandle → select + set activeEmitterIdx; Checkbox → toggle; BtnMoveUp → onEmitterMoved(i, i-1); BtnMoveDown → onEmitterMoved(i, i+1); BtnDuplicate → onEmitterDuplicated; BtnDelete → onEmitterDeleted; AddEmitterRow → onAddEmitterRequested with fallback to overlay-level callback)
  - (c) calls `Render`. Parameter inspector now only appears in the center panel (CenterMode == Parameters).
- [x] 1.9 Every color via `VfxTheme()` tokens (cardBg, cardBgSelected, cardBgHover, borderSubtle, borderStrong, accentPrimary, stateSuccess, stateWarning, textPrimary, textDim, panelBgAlt) — no hard-coded literals

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation (docs/engine/25-vfx/61-system-overview.md)
- [x] 2.2 Write tests covering the new behavior (test_vfx_system_overview.cpp — 12 tests: unbound empty, bound-empty only add row, 3 emitters yield 3 rows + add, each row emits all 9 sub-items, hit-test action button beats row + add-emitter-row returned, drag 0→2 fires moved callback, self-target drag noops, delete callback fires + widget doesn't shrink list, add callback fires once, duplicate callback carries index, selection round-trip, EmitterCount reflects vector)
- [x] 2.3 Run tests and confirm they pass (`zig build` compiles runtime + test_runtime cleanly. Test-binary execution continues blocked by pre-existing missing `swscale-9.dll` — same gap documented in every prior phase)
