## 1. Implementation

- [x] 1.1 Rewrote `VfxColorPalette` background tokens to UE5 five-step ladder: `backgroundBase` = `#0B0B0B` (0.043), `panelBg` = `#1C1C1C` (0.110), `barBg` = `#242424` (0.141), `cardBg` = `popupBg` = `#2C2C2C` (0.173), `cardBgHover` = `#383838` (0.220)
- [x] 1.2 Replaced `accentPrimary` with UE5 blue `#0070E0` (0.000/0.439/0.878); added `accentOrange` `#FF7B00` (1.000/0.482/0.000); `accentHover`/`accentPressed`/`accentMuted` derived from the new primary
- [x] 1.3 Updated text ladder: `textPrimary` `#E5E5E5` (0.898), `textSecondary` `#A0A0A0` (0.627), `textDim` `#707070` (0.439), `textDisabled` `#4A4A4A` (0.290); monotonic-descent invariant verified by tests
- [x] 1.4 Updated borders: `borderSubtle` `#1A1A1A` (0.102), `borderStrong` `#404040` (0.251); accent-hover border derivation stays via `borderAccent = accentPrimary`
- [x] 1.5 Added `VfxTypographyScale::menuPx=9`, `bodyPx=10`, `captionPx=11`, `headerPx=12`, `titlePx=14`; accessor `VfxThemeTypography()` returns the singleton
- [x] 1.6 Shrunk `VfxRadiusScale` to UE5 values: `none=0`, `xs=2`, `sm=3`, `md=4`, `lg=6`; documented semantic roles (panels use `none`, buttons `xs`, tabs/cards `sm`, dropdowns `md`, dialogs `lg`)
- [x] 1.7 Updated `VfxShadowScale` to UE5 lighter intensities: hover `(1, 2, 0.15)`, popup `(2, 6, 0.35)`, dialog `(4, 12, 0.50)`; alpha caps verified by test
- [x] 1.8 Updated `cardBgSelected` to UE5 blue `#1C3A5E` (0.110/0.227/0.369); blue-bias invariant (`R < B` AND `G < B`) verified by test
- [x] 1.9 Headless screenshot `.screenshots/v4_ue5_palette.png` confirms panels render at UE5 `#2C2C2C`, stage chevrons at `textSecondary`, selected module "turbulence" with `#1C3A5E` blue tint, menu-bar labels in `#E5E5E5` MSDF

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
