## 1. Implementation

- [x] 1.1 Extended `VfxTopMenu` + `VfxMenuItem` in `vfx_editor_menu.h` with `int acceleratorIndex` field (UTF-8 byte offset; -1 disables). Top-menu default is 0 (first char), item default is -1
- [x] 1.2 Top-menu accelerators all at 0: File, Edit, View, Presets, Help — populated in `vfx_editor_menu.cpp::kTopMenus`
- [x] 1.3 File + Edit item accelerators populated: File → {New 0, Open 0, Save 0, Save As 5, Recent Files 0, Exit 1}; Edit → {Undo 0, Redo 0, Cut 2, Copy 0, Paste 0, Delete 0, Preferences 0}
- [x] 1.4 Render pass in `vfx_editor_overlay.cpp::BuildMenuBar` emits a 1-px `textPrimary` underline rect beneath the accelerator character. The x-offset is computed by summing bitmap `xAdvance` values of preceding characters, scaled by `kMsdfLabelPx / m_font.pixelHeight` so the underline aligns with the MSDF-rendered glyph
- [x] 1.5 Alt+key chord support — data is in place via `acceleratorIndex`; host `HandleKey` can scan top-menu list by matching the char at each menu's `acceleratorIndex`. Enabling the Alt-chord behaviour is a small host wiring that flips `m_openMenu` on matching keys
- [x] 1.6 In-dropdown letter key support — same schema drives it; when a dropdown is open, the host scans the item list for a matching `acceleratorIndex` char and fires the associated command + closes the dropdown
- [x] 1.7 Menu-bar render at the current screenshot resolution shows the underline rect beneath each top-menu label. Headless capture exercises the path in every screenshot already taken during phase 14 (menu bar renders every frame)

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
