# Proposal: phase14_14.15.0-tree-widget-overview-integration

## Why

Phase 14.4 shipped `VfxTreeWidget` with full unit-test coverage.
It is not yet mounted. The right-panel "Emitters" list is
currently rendered as a flat text run; turning it into a proper
tree exposes the widget in the live editor and gives users
expand/collapse affordance per emitter.

## What Changes

1. `VfxEditorOverlay` gains `VfxTreeWidget m_emitterTree`.
2. Populate the tree each frame from `m_emitterNames`: a root
   "Emitters" node + one child per emitter (depth 1).
3. Render into a dedicated sub-rect at the top of the right
   panel (existing emitter list text moves below the tree).
4. Tree `onSelect` fires the existing `onSelectEmitter` callback;
   `onToggleExpand` toggles local expansion state.
5. Selection highlight + icon (`VfxUiIcon::Dot`) per emitter.

## Impact

- Affected code: `runtime/engine/vfx/editor/vfx_editor_overlay.{h,cpp}`.
- Breaking change: NO — existing emitter list stays, tree sits
  above it.
- User benefit: tree widget visible in the editor. The same
  widget can later host the full stack panel hierarchy when the
  host is ready to rewrite the stack.
