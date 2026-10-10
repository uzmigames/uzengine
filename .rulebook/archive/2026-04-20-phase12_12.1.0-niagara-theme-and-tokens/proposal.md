# Proposal: phase12_12.1.0-niagara-theme-and-tokens

## Why

Phase 11.14.0 mounted `VfxEditorComposition` inside the demo, but every
widget is still rendering with the bootstrap palette that was meant as
a placeholder (flat `kColorPanelBg`, `kColorBarBg`, etc., literal RGB
values scattered across `vfx_editor_overlay.cpp`, `vfx_stack_panel_widget.cpp`,
and seven other files). The result does not look anything like Niagara —
it is a colored block UI with no design system backing it.

Before any cosmetic work on the individual widgets (12.2 – 12.10) can
be done coherently, **the engine needs a single source of truth for
color, spacing, typography, and iconography.** Without this, every
widget re-invents its own shades of grey and font sizes, and a later
theme swap (light mode, high contrast, user-tinted) is impossible.

This task establishes `VfxEditorTheme` + the `VfxIconGlyph` table as
the authoritative design system. Every subsequent phase-12 task
consumes these tokens instead of hard-coded Vec4/float literals.

## What Changes

1. **New header `engine/vfx/editor/vfx_editor_theme.h`** — declares
   `struct VfxEditorTheme` with:
   - Color palette (40+ semantic tokens: `backgroundBase`,
     `panelBg`, `panelBgAlt`, `cardBg`, `cardBgHover`, `cardBgSelected`,
     `cardBgDrag`, `borderSubtle`, `borderStrong`, `accentPrimary`,
     `accentMuted`, `textPrimary`, `textSecondary`, `textDim`,
     `textDisabled`, `stateSuccess`, `stateWarning`, `stateError`,
     `stateInfo`, etc.) — values calibrated against Niagara screenshots.
   - Spacing scale (`space.xs = 2`, `space.sm = 4`, `space.md = 8`,
     `space.lg = 12`, `space.xl = 16`, `space.xxl = 24`).
   - Typography scale (`type.headerPx`, `type.bodyPx`, `type.captionPx`,
     `type.codePx`).
   - Corner-radius scale (`radius.sm = 2`, `radius.md = 4`,
     `radius.lg = 6`).
   - Single global accessor `const VfxEditorTheme& VfxTheme()`.

2. **New header `engine/vfx/editor/vfx_editor_icons.h`** — declares a
   complete Niagara-parity glyph table:
   - Transport icons: play, pause, stop, step-forward, step-backward, restart.
   - Navigation: chevron-right, chevron-down, back, forward.
   - State: check, cross, plus, minus, dot, star, warning, info, error.
   - Module overrides: add, replace, revert, remove.
   - Drag handles: vertical-dots, grip.
   - Module category glyphs (location, velocity, color, force,
     lifetime, rotation, size, spawn).
   - Each glyph has a stable C-string constant; font availability is
     verified at Init() time via `BitmapFont::HasGlyph(codepoint)`.

3. **New source `engine/vfx/editor/vfx_editor_theme.cpp`** — initial
   Niagara-dark palette values + `VfxTheme()` returning a static
   instance. Populating the theme from a `.uztheme` JSON file is a
   follow-up task (out of scope here).

4. **Refactor consumers to read from `VfxTheme()`** — every place in
   `vfx_editor_overlay.cpp`, `vfx_stack_panel_widget.cpp`,
   `vfx_parameter_inspector_widget.cpp`, `vfx_curves_editor_widget.cpp`,
   `vfx_timeline_scrubber_widget.cpp`, `vfx_module_picker_dialog.cpp`
   that currently uses a hard-coded Vec4 palette constant switches to
   reading `VfxTheme().colorName`.

## Impact

- Affected specs: `specs/vfx-editor-theme/spec.md` (new).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_editor_theme.{h,cpp}` — new.
  - `runtime/engine/vfx/editor/vfx_editor_icons.h` — new.
  - `runtime/engine/vfx/editor/vfx_editor_overlay.cpp` — palette refactor.
  - `runtime/engine/vfx/editor/vfx_stack_panel_widget.cpp` — palette refactor.
  - `runtime/engine/vfx/editor/vfx_parameter_inspector_widget.cpp` — palette refactor.
  - `runtime/engine/vfx/editor/vfx_curves_editor_widget.cpp` — palette refactor.
  - `runtime/engine/vfx/editor/vfx_timeline_scrubber_widget.cpp` — palette refactor.
  - `runtime/engine/vfx/editor/vfx_module_picker_dialog.cpp` — palette refactor.
  - `runtime/tests/engine/vfx/test_vfx_editor_theme.cpp` — new.
  - `docs/engine/25-vfx/53-editor-theme.md` — new.
- Breaking change: NO — tokens are an internal representation swap;
  public APIs of every widget are unchanged.
- User benefit: all subsequent visual tasks (12.2–12.10) share one
  palette; a Niagara-dark theme is the shipped default; later theming
  (light mode, HC) becomes a single-file change.
