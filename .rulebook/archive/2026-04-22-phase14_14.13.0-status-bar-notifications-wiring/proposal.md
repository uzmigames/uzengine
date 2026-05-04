# Proposal: phase14_14.13.0-status-bar-notifications-wiring

## Why

Phase 14.6 shipped the transient-notification queue on
`VfxStatusBarWidget`. Phase 14.10 mounted the bar. The queue is
wired but never pushed — no command in the editor surfaces a
notification when it runs. The status bar centre stays empty.

Wiring notifications to frequent user actions (Save / Restart /
Thumb / Undo / Redo) makes the editor feel responsive and gives
the user persistent, low-friction feedback.

## What Changes

1. `VfxEditorOverlay::Update(dt)` advances the status bar
   notification queue via `m_statusBar.Tick(dt)`.
2. Toolbar button dispatch pushes notifications:
   - Save    → `"Saved"` (Info).
   - Restart → `"Restarted"` (Info).
   - Thumb   → `"Thumbnail captured"` (Info).
   - Play toggle → `"Playing"` / `"Paused"` (Info).
3. Undo / Redo via `Ctrl+Z` / `Ctrl+Y` push `"Undo"` / `"Redo"`
   (Info) when the matching callback fires.
4. `VfxEditorOverlay::PushStatus(msg, level)` convenience method
   for hosts / widgets that want to push their own notifications.

## Impact

- Affected specs: none (queue already tested in 14.6).
- Affected code: `runtime/engine/vfx/editor/vfx_editor_overlay.{h,cpp}`.
- Breaking change: NO.
- User benefit: toolbar clicks produce visible feedback in the
  status bar; the editor stops feeling silent.
