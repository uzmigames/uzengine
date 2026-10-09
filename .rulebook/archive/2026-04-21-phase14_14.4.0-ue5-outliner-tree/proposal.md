# Proposal: phase14_14.4.0-ue5-outliner-tree

## Why

UE5's World Outliner is the reference tree widget — hierarchical
nodes with expand/collapse arrows, per-type icons, indentation
per depth level, and selection highlight that spans the full row
width regardless of label length. The stack panel in phase 13
emits a flat list of stage headers + module cards. It reads as a
list, not a tree.

Rebuilding the stack panel as a proper UE5-style tree widget
gives:
- Consistent expand/collapse interaction across every hierarchy
  in the editor (stages, property sub-categories, asset folders).
- Full-width hover + selection highlights.
- Unified indent guides.
- Reusable `VfxTreeWidget` for future panels (outliner, content
  browser, asset hierarchy).

## What Changes

1. New `VfxTreeWidget` — generic hierarchical list with:
   - Expand arrow sprite (`ChevronRight` collapsed / `ChevronDown`
     expanded) at 12 px at row left edge + `depth * 16` px indent.
   - Per-type icon sprite (caller-provided via `VfxUiIcon`).
   - Full-row hover + selection fill.
   - 1-px vertical indent guide rect at each depth level
     (subtle dotted-looking line — actually a 1-px
     `borderSubtle` rect with 2-px spacing across rows).
   - Row height 20 px (UE5 density).
2. Migrate stack panel render to use `VfxTreeWidget` instead of
   custom stage header + module card composition.
3. Selection highlight spans `row.x` → `row.x + row.w`, fills
   with `cardBgSelected`, regardless of label length.
4. Hover highlight spans the same full-width region with
   `cardBgHover`.
5. Multi-select via Ctrl+click + Shift+click (future — spec only).

## Impact

- Affected specs: `specs/ue5-outliner-tree/spec.md` (new).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_tree_widget.{h,cpp}` (new).
  - `runtime/engine/vfx/editor/vfx_stack_panel_widget.cpp`
    (migration).
- Breaking change: NO — stack panel public API + callbacks
  preserved.
- User benefit: hierarchy browsing matches UE5 muscle-memory and
  the same tree infrastructure ships across every future panel.
