# phase10_10.2.0-ui-modal-dialog-framework

Modal dialog stack for the VFX editor overlay.

Reference: Dear ImGui `BeginPopupModal`.

## 1. Modal stack

- [ ] 1.1 `PushModal(name, renderFn)` / `PopModal()` API on the overlay.
- [ ] 1.2 Dim overlay (alpha 0.4) behind the topmost modal.
- [ ] 1.3 Exclusive input capture — clicks / keystrokes bypass the underlying panels.
- [ ] 1.4 Dismissal: Escape / Enter (commit) / click outside the modal frame.

## 2. Standard frame

- [ ] 2.1 Title bar, body region, button row layout helper.
- [ ] 2.2 `OK` / `Cancel` button generator shared across consumers.
- [ ] 2.3 Size auto-fit based on body content height.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 3.1 Update or create documentation covering the implementation
- [ ] 3.2 Write tests covering the new behavior
- [ ] 3.3 Run tests and confirm they pass
