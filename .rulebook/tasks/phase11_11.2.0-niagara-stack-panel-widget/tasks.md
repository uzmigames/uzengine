# phase11_11.2.0-niagara-stack-panel-widget

Visual stack editor with collapsible stages + draggable module cards.

Reference: UE5 `SNiagaraStack`.

## 1. Widget

- [ ] 1.1 `VfxStackPanelWidget` in `runtime/engine/vfx/editor/vfx_stack_panel_widget.{h,cpp}`.
      `Bind(VfxStack*)` + `Render(TextRenderer&, const Rect&)`.
- [ ] 1.2 Per-stage header: arrow toggle (>/v) + stage name + module count badge.
      Click header → toggle `m_stageExpanded[stage]`.
- [ ] 1.3 Per-module card: checkbox (enabled) + module name + strength summary
      (e.g. "Gravity  Z:-981") + "..." right-click trigger.

## 2. Mouse interaction

- [ ] 2.1 Click empty "+ Add Module" row at stage bottom → fire
      `onAddModuleRequested(VfxStage)` callback (wired to 11.3).
- [ ] 2.2 Drag-hold on a card's drag handle → reorder within the stage.
      Insertion gap rendered as a 2px accent line.
- [ ] 2.3 Right-click a card → context menu (Remove / Duplicate / Move Up /
      Move Down / Disable). Menu routed through 11.9 UndoCommand pattern.
- [ ] 2.4 Hover state feedback (highlight card background) + active state
      (dimmed when `enabled == false`).

## 3. Overlay integration

- [ ] 3.1 `VfxEditorOverlay` detects `emitter->stack.IsEmpty()` and falls
      through to legacy flat module list; otherwise hands rendering to
      the `VfxStackPanelWidget` inside the left panel area.
- [ ] 3.2 Callback `onModuleSelected(const VfxModuleInstance*)` routed to
      the existing parameter-slider panel on the right so the user can
      edit the selected module's config inline.

## 4. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
      Augment `docs/engine/25-vfx/44-niagara-stack-model.md` with an
      ASCII UI mock + interaction table (click/drag/right-click).
- [ ] 4.2 Write tests covering the new behavior
      Widget state tests (stage-expanded map, hit-test regions,
      drag-reorder index math). Visual behaviour verified via
      `vfx_viewport_app` screenshots.
- [ ] 4.3 Run tests and confirm they pass
      `zig build` clean; manual QA: click/drag/remove on each stage.
