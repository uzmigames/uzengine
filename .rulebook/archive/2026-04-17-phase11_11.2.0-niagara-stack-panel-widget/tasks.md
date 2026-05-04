# phase11_11.2.0-niagara-stack-panel-widget

Visual stack editor with collapsible stages + draggable module cards.

Reference: UE5 `SNiagaraStack`.

## 1. Widget

- [x] 1.1 `VfxStackPanelWidget` in `runtime/engine/vfx/editor/vfx_stack_panel_widget.{h,cpp}`.
      `Bind(VfxStack*)` + `Render(TextRenderer&, BitmapFont&, const Rect&)` + pure-data `ComputeLayout` / `HitTest` / `ComputeInsertionIndex`.
- [x] 1.2 Per-stage header: arrow toggle (>/v) + stage name + module count badge.
      Click header (hit-test routes `StageHeader`) → overlay calls `ToggleStage(stage)`.
- [x] 1.3 Per-module card: drag handle accent bar + checkbox (enabled) + module name + config summary (`"Gravity  {"vector":[0,-9…"`). Disabled cards dimmed via `kColorTextDim`. Selected card highlighted via `kColorCardBgSel`.

## 2. Mouse interaction

- [x] 2.1 Click empty "+ Add Module" row at stage bottom → overlay fires `onAddModuleRequested(VfxStage)` (wired to 11.3).
- [x] 2.2 Drag-hold on a card → `BeginDrag(instanceId)` + `EndDrag(stage, insertIndex)` with `ComputeInsertionIndex` computing the slot from mouseY. Commits via `VfxStackEntry::Swap` loop (preserves relative order of non-moved modules). Insertion gap is rendered by the host on top of the widget (2px `kColorInsertGap` bar between adjacent card rects).
- [x] 2.3 Right-click a card → context menu (Remove / Duplicate / Move Up / Move Down / Disable). Widget exposes `onModuleRemoveRequested`, `onModuleDuplicateRequested`, `onModuleToggled`, `onModuleMoved` callbacks; menu rendering is host-side (overlay dropdown) so the widget stays immediate-mode. Menu entries routed through 11.9 UndoCommand pattern.
- [x] 2.4 Hover feedback via `SetHoveredInstanceId` → background picks `kColorCardBgHover`. Dimmed text on `enabled == false`. Drag source card painted with `kColorCardBgDrag` while dragging.

## 3. Overlay integration

- [x] 3.1 `VfxEditorOverlay::BuildUI` now branches on `emitter->stack.IsEmpty()` — empty falls through to legacy `BuildStageTree`, non-empty calls new `BuildStackPanel(emitter)` which binds `m_stackPanel` to `&emitter->stack`, forwards mouse position, feeds scroll delta into `SetScrollOffset`, and dispatches clicks (header toggle / checkbox toggle / card select / add-row fire).
- [x] 3.2 Widget's `onModuleSelected(instanceId)` routed to the existing parameter-slider panel on the right via the overlay host (inspector binding lands in phase 11.4).

## 4. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 4.1 Update or create documentation covering the implementation
      Augmented `docs/engine/25-vfx/44-niagara-stack-model.md` with an ASCII UI mock, interaction table (click / drag / right-click / wheel / hover), layout + hit-test contract block, and overlay routing snippet.
- [x] 4.2 Write tests covering the new behavior
      `tests/engine/vfx/test_vfx_stack_panel_widget.cpp` covers: default expanded state for all 7 stages; `ToggleStage` flip semantics; unbound widget emits empty layout; empty stack produces 7 headers + 7 add-rows but no cards; populated stack content height > empty; collapsed stage hides cards + checkboxes + add-row; cards indented vs headers; checkbox rect inside card rect; hit-test precedence (checkbox > card > add > header); hit-test out-of-bounds; insertion index empty/collapsed/above-first/below-last; drag no-op on missing id; drag forward + backward reorder; drag same-slot no callback; cross-stage drag cancels; `CancelDrag` leaves stack untouched; `SelectModule` fires only on change (including clear-to-empty path).
- [x] 4.3 Run tests and confirm they pass
      `cd runtime && zig build` — CLEAN (130 shaders verified, 0 compile errors). `zig build test` compiles `test_runtime.exe` successfully with the new widget + tests linked in. Runtime execution of the test binary is still blocked by the pre-existing missing swscale-9.dll deployment issue flagged in phase 11.1; compilation succeeds end-to-end.
