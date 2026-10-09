# Proposal: phase14_14.16.0-content-browser-modal

## Why

Phase 14.7 shipped `VfxContentBrowserWidget` with full unit-test
coverage. It is not yet mounted. Wiring it as a modal opened from
a menu command surfaces the widget in the live editor and
demonstrates its drag / selection / hover contract end-to-end.

## What Changes

1. `VfxEditorOverlay` gains `VfxContentBrowserWidget m_contentBrowser`
   + `bool m_contentBrowserOpen` + `VfxStackRect m_contentBrowserBounds`.
2. `OpenContentBrowser()` method opens the modal centred on the
   screen; `CloseContentBrowser()` hides it.
3. The modal populates with 6 demo tiles when opened (Sparks,
   Smoke, Fire, Dust, Magic, Explosion) — type-coloured tint
   bars pick discrete accent colours so the grid reads as an
   assortment.
4. When open: renders over all other panels with a dimming
   backdrop + rounded dialog body + close `x` in the corner.
5. Clicking a tile fires `onContentBrowserSelect(id, label)`;
   double-click fires `onContentBrowserConfirm(id, label)` and
   closes the modal; Esc closes the modal without selection.

## Impact

- Affected code: `runtime/engine/vfx/editor/vfx_editor_overlay.{h,cpp}`.
- Breaking change: NO — modal is additive + opt-in via explicit
  `OpenContentBrowser` call.
- User benefit: content browser widget visible in the live
  editor; click-dispatch + drag-threshold gestures proven against
  real user input.
