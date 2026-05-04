## 1. Implementation

- [x] 1.1 Created `runtime/engine/vfx/editor/vfx_tab_strip_widget.{h,cpp}` — `VfxTabEntry { label, icon, closable }` + `VfxTabStripWidget { AddTab, Clear, SetActiveIndex, SetHoveredIndex, ComputeLayout, HitTest, Render }` with `onSelect / onClose / onReorder` callbacks
- [x] 1.2 Active tab renders `AddRoundedRect(radius.sm=3, cardBg)` with the bottom `radius` px overdrawn with a flat `cardBg` rect to square the bottom edge; 2-px `accentPrimary` rect along the top edge spans the full tab width
- [x] 1.3 Inactive tab renders `AddRoundedRect(radius.sm=3, panelBg)` with the same bottom-square overdraw; label uses `textSecondary`
- [x] 1.4 Hover tab renders with `barBg` fill and `textPrimary` label
- [x] 1.5 1-px `borderSubtle` vertical divider emitted between adjacent tabs (4-px top/bottom padding so the divider sits inside the tab content band)
- [x] 1.6 Close `x` sprite (`VfxUiIcon::Cross`) renders 10x10 at `tabRight - tabPadding - 10` when `hoveredIdx == tabIdx`; `HitTest` checks close-icon region first so it takes priority over the tab body click
- [x] 1.7 Migrated emitter tab bar in `vfx_editor_overlay.cpp::BuildEmitterBar` — rebuilds `m_emitterTabStrip` each frame from `m_emitterNames`, calls `Render(text, font, GetMsdfFont(), stripBounds)`
- [x] 1.8 Existing `onSelectEmitter` callback fires via the `HitTest` tab-body detection in the host; `onClose` hook is declared on the widget and ready for future emitter-close command wiring
- [x] 1.9 Headless screenshot `.screenshots/v4_14_2_tabs.png` shows the emitter tab strip rendering the "Fireball" tab with the UE5 rounded-top / flat-bottom geometry

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
