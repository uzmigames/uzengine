# Proposal: phase12_12.2.0-niagara-stack-panel-redesign

## Why

The current `VfxStackPanelWidget` (phase 11.2.0) renders a functional
but primitive stack: flat rectangles for stage headers, single-line
module cards with a tiny left ribbon as the "drag handle", no module
icons, no overflow menu button, no separator lines between stages, and
an "+ Add Module" row that repeats for every stage instead of being
an inline action on the stage header.

Niagara's actual stack looks fundamentally different:

- **Emitter header card** at the top holds the emitter name, enable
  toggle, lock icon, and an "Add Emitter" button aligned to the right.
- **Stage headers** are 28-30 px tall with an expand chevron, a stage
  icon, the stage name, a count badge, AND an inline `+` button that
  opens the module picker directly on the stage — no separate "Add
  Module" row at the bottom.
- **Module cards** are 36 px tall, show a drag grip, an enable
  checkbox, a module-category icon (velocity arrow, color swatch, etc.),
  the module name, an inheritance badge right of the name, and a
  three-dot overflow menu button at the right edge. Selected cards
  have a 3-px accent stripe on the left.
- **Visual hierarchy** uses subtle dividers between stages (not a
  blank gap) and a very faint border around the whole panel.

This task redesigns the stack panel visuals + layout + hit-test to
match Niagara pixel-for-pixel, while keeping every public widget API
and callback (the data contract) unchanged.

## What Changes

1. **Emitter header card** — new layout item kind
   `VfxStackItemType::EmitterHeader` at the top of `ComputeLayout`.
   Renders emitter name (bold, `type.headerPx`), enable checkbox, and
   lock icon. `HandleMouse` toggles `emitter->emitting` on checkbox
   click.

2. **Stage header redesign** — every stage header adds:
   - A 16 px chevron glyph on the left (`VFX_ICON_CHEVRON_DOWN` when
     expanded, `VFX_ICON_CHEVRON_RIGHT` when collapsed).
   - A stage-category icon (`VFX_ICON_LOCATION` for spawn stages,
     `VFX_ICON_VELOCITY` for update, etc.) 18 px wide.
   - The stage name in `textSecondary` color, normal weight.
   - A count badge on the right (`N` modules / `M` enabled, styled as
     a small pill).
   - An inline `+` button at the right edge (fires
     `onAddModuleRequested` — the standalone "+ Add Module" row is
     removed).
   - A 1-px `borderSubtle` divider below the last module of the stage.

3. **Module card redesign** — 36 px cards with horizontal layout:
   `[grip-dots 16][checkbox 18][module-icon 20][name + summary, flex][override-badge 16][overflow-menu ⋯ 20]`.
   Hover, selected, and drag states each have distinct background
   shades from the theme (`cardBgHover`, `cardBgSelected`,
   `cardBgDrag`). Selected cards also get a 3-px accent left stripe.

4. **Overflow menu button** — new layout item
   `VfxStackItemType::CardOverflowMenu` hit-tested as the three-dot
   area. Clicking it fires a new callback
   `onModuleContextMenuRequested(instanceId, x, y)` that the host
   (12.8) wires to the context menu system.

5. **Module icon resolution** — stack panel resolves a category from
   `VfxModuleMetadata` (phase 11.3) and renders the corresponding
   glyph from `vfx_editor_icons.h`. Unknown modules get the generic
   `VFX_ICON_MODULE` fallback.

6. **Drag handle** — replace the 2-px left ribbon with a proper
   6-dot grip glyph rendered in `textDim`, hit-testable as
   `VfxStackItemType::CardDragHandle` (only that sub-rect initiates
   drag, not the whole card).

7. **Selection stripe** — when `instanceId == m_selectedInstanceId`,
   render a 3-px `accentPrimary` rectangle on the card's left edge.

8. **No visual regression in data / callback contract** — the
   existing callbacks (`onModuleSelected`, `onModuleToggled`,
   `onModuleMoved`, `onAddModuleRequested`, `onModuleRemoveRequested`,
   `onModuleDuplicateRequested`, `onRevertOverrideRequested`) fire on
   the same logical events. One new callback
   `onModuleContextMenuRequested` is additive.

## Impact

- Affected specs: `specs/vfx-stack-panel-redesign/spec.md` (new).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_stack_panel_widget.h` —
    `VfxStackItemType` enum grows (`EmitterHeader`, `CardDragHandle`,
    `CardOverflowMenu`); new callback.
  - `runtime/engine/vfx/editor/vfx_stack_panel_widget.cpp` —
    `ComputeLayout`, `HitTest`, `Render` rewritten.
  - `runtime/engine/vfx/editor/vfx_editor_overlay.cpp` — `BuildNiagaraUI`
    wires the new callback and reacts to the new hit types.
  - `runtime/tests/engine/vfx/test_vfx_stack_panel_redesign.cpp` — new.
  - `docs/engine/25-vfx/54-stack-panel-visuals.md` — new.
- Breaking change: MINOR — the `VfxStackItemType` enum gains new
  values; downstream switches that relied on exhaustive matching
  gain warnings. All existing values keep their numeric identity.
- User benefit: stack panel now looks like Niagara — visually
  scannable, touch-friendly hit targets, clear hierarchy, inline
  add-module action.
