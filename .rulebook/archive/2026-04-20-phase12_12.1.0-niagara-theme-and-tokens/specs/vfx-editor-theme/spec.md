# vfx-editor-theme

## ADDED Requirements

### Requirement: Single theme source of truth

The VFX editor SHALL source every color, spacing value, font size, and
corner radius from `const VfxEditorTheme& VfxTheme()`. Hard-coded
palette values in widget .cpp files are forbidden. Tests MUST assert
that no `Vec4{...}` literal appears in the widget rendering paths for
color values — colors come from theme tokens only.

#### Scenario: Theme accessor is stable across calls

Given a running editor
When the caller invokes `VfxTheme()` twice
Then both calls return a reference to the same underlying instance.

### Requirement: Niagara-dark palette defaults

The default `VfxTheme()` SHALL return a dark palette with the following
minimum contrast guarantees:

- `textPrimary` against `panelBg` SHALL reach at least WCAG AA (4.5:1).
- `accentPrimary` against `panelBg` SHALL reach at least 3:1.
- `textDim` against `panelBg` SHALL reach at least 3:1.

#### Scenario: Text contrast against panel background

Given the default theme
When the renderer composes `textPrimary` on `panelBg`
Then the computed relative-luminance contrast ratio is >= 4.5.

### Requirement: Icon glyph table is complete

`vfx_editor_icons.h` SHALL declare at least 30 glyph constants covering
transport (play, pause, stop, step-forward, step-backward, restart),
navigation (chevron-right, chevron-down), state (check, cross, plus,
minus, warning), module overrides (add, replace, revert, remove), drag
handles (vertical-dots, grip), and module categories (location,
velocity, color, force, lifetime, rotation, size, spawn).

#### Scenario: All declared glyphs are non-empty C strings

Given the icon table
When any declared glyph constant is evaluated at compile time
Then its value is a non-null C string with length >= 1.

### Requirement: Glyph availability verification

On `VfxEditorOverlay::Init`, the overlay SHALL invoke
`BitmapFont::HasGlyph(codepoint)` for every icon in the theme's glyph
table and log a warning for any missing glyph. Missing glyphs do not
fail init — they fall back to the `?` placeholder glyph.

#### Scenario: Init logs warnings for missing glyphs

Given a bitmap font lacking one declared icon codepoint
When `VfxEditorOverlay::Init` runs
Then exactly one warning line mentioning the missing codepoint is
logged, and `Init` still returns `true`.
