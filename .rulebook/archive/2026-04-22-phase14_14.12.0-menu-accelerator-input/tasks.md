## 1. Implementation

- [x] 1.1 Added `[[nodiscard]] int VfxMatchAccelerator(char ascii) noexcept` in `vfx_editor_menu.h/.cpp` — scans `kTopMenus` for a top menu whose `acceleratorIndex` char matches `ascii` (case-insensitive via local `ToLowerAscii`); returns -1 on miss
- [x] 1.2 Added `[[nodiscard]] int VfxMatchAcceleratorInMenu(char ascii, const VfxMenuItem* items, size_t count) noexcept` — walks an item list returning the first enabled non-separator non-null-label match; returns -1 otherwise
- [x] 1.3 Added `VfxEditorOverlay::HandleAcceleratorKey(char ascii, bool altHeld)` — routes Alt+letter via `VfxMatchAccelerator` to set `m_openMenu` and swallows the key by returning true
- [x] 1.4 Same method handles bare-letter presses while `m_openMenu >= 0` — calls `VfxMatchAcceleratorInMenu` on the open menu's items, fires `onMenuCommand(item.command)`, sets `m_openMenu = -1`, returns true
- [x] 1.5 Both helpers apply `ToLowerAscii` on the accelerator char before compare; separator + disabled + null-label items excluded from the match set (verified by `SeparatorsNeverMatch` test)

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
