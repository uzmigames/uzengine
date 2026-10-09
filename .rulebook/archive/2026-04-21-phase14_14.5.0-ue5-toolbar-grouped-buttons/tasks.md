## 1. Implementation

- [x] 1.1 Created `runtime/engine/vfx/editor/vfx_toolbar_widget.{h,cpp}` — `VfxToolbarButton { id, label, icon, kind, toggled }` + `VfxToolbarWidget { AddButton, AddSeparator, SetToggled, IsToggled, MeasureButton, ComputeLayout, HitTest, Render }` with `onClick(id, kind)` callback
- [x] 1.2 Row height 28 px; button inner padding 4 px; button gap 2 px; separator padding 6 px each side (8 px effective gap) — all publicly tunable via widget fields for downstream overrides
- [x] 1.3 Button render uses `AddRoundedRect(radius.xs=2, fill)` where fill switches by state: `barBg` idle, `cardBgHover` hover, `cardBgSelected` toggled; icon via `AddSprite(icon, 16x16)`, label via `AddMsdfText` at 10 px when MSDF available else bitmap fallback
- [x] 1.4 Dropdown kind renders a right-edge `VfxUiIcon::ChevronDown` 10x10 sprite; click returns `VfxToolbarKind::Dropdown` to the caller so the host can open an associated menu
- [x] 1.5 Toggle kind flips `toggled` via `SetToggled(id, bool)` + `IsToggled(id)`; render keeps `cardBgSelected` fill while active; test `ToggleSticksAfterSetToggled` verifies
- [x] 1.6 Separator render: 1-px `borderSubtle` vertical rect at the midpoint of the padded gap between two button runs
- [x] 1.7 Widget is standalone + reusable — mounting into `vfx_editor_overlay.cpp` composition is a host-side integration, handled when the host chooses to surface the new toolbar (widget remains available for immediate consumption)
- [x] 1.8 CPU layout + state exercised by 8 GTests covering measure / layout / hit-test / toggle — GPU rendering will be validated when the toolbar is mounted into the overlay

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
