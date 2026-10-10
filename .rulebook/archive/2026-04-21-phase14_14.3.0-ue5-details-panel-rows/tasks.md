## 1. Implementation

- [x] 1.1 Added `kLabelColFrac = 0.40f` in `vfx_parameter_inspector_widget.cpp::ParameterRow` render; label column = 40 % of row width, value column = 60 %
- [x] 1.2 Label column uses `textSecondary` per UE5 muted-label convention; existing short labels fit without ellipsis (long-label truncation already handled by `TextRenderer::AddText` downstream)
- [x] 1.3 Value editor column delegated to existing `splitRow` layout (editR rect) which the `EditorFloat/Vec3/Color/...` render consumes — phase-13 value rendering preserved
- [x] 1.4 1-px `borderSubtle` vertical divider emitted at `row.x + labelColW` spanning full row height
- [x] 1.5 Zebra stripe via new `mutable uint32_t m_rowIndex` member; `panelBg` on even rows, `panelBgAlt` on odd rows; counter resets at the top of `Render`; section headers excluded (they use `barBg` fill directly)
- [x] 1.6 Section header migrated to UE5 flat bar: `barBg` fill, left chevron sprite (`VfxUiIcon::ChevronDown / ChevronRight`) at `rect.x + 6 px`, uppercase-transformed label at `rect.x + 22 px`
- [x] 1.7 Section-header hover handled by existing `HitTest` path in `HandleMouse`; selected-section visual swap is already present in phase-13 code and unchanged
- [x] 1.8 Nested-property indent kept at existing `splitRow` tier — phase-14.3 focuses on the row decoration; deeper indent levels honour the existing `leftX + scope indent` offset
- [x] 1.9 1-px `borderSubtle` bottom divider emitted on every `ParameterRow` + every `Section` row
- [x] 1.10 Headless screenshot `.screenshots/v4_14_3_details.png` confirms label/value split + zebra stripe + divider hairlines

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
