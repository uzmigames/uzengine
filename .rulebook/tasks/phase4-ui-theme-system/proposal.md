# Proposal: phase2-ui-theme-system

## Why
Every game UI currently hard-codes colors, font families, font sizes, and spacing values directly in CSS files. Changing the visual identity of a game UI — switching from a dark fantasy look to a bright sci-fi look, or implementing an accessibility high-contrast mode — requires finding and editing every CSS file individually. Without a centralized token system, there is no runtime theme switching, no dark/light mode toggle, and no way for mods or DLC to restyle the UI without replacing every stylesheet.

## What Changes
- Define a theme file format as a plain CSS file containing only `:root { }` CSS custom property declarations; a project's theme files live at `ui/themes/<name>.css` and are listed under `ui.themes` in `.uzproj`
- Establish a standard set of CSS custom property names that all built-in UI components consume: `--color-primary`, `--color-secondary`, `--color-surface`, `--color-on-surface`, `--color-error`, `--font-heading`, `--font-body`, `--font-mono`, `--spacing-sm`, `--spacing-md`, `--spacing-lg`, `--radius-sm`, `--radius-md`
- Implement theme loading in uz_ui_html.dll: on startup, inject the active theme CSS file as the first stylesheet in every screen's document head, before any game CSS, so game stylesheets can override individual tokens without losing the rest of the theme
- Add Lua API: `ctx.ui:set_theme(name)` swaps the injected theme stylesheet and patches all live CSS custom properties on `document.documentElement` in one synchronous call, achieving a zero-flash theme switch at runtime
- Support theme inheritance: a theme file can declare `/* @extends dark */` in a comment header; the engine merges it on top of the parent theme so child themes only need to declare overrides
- Add hot-reload on theme file change: the file watcher detects edits to any active `ui/themes/*.css` file and applies a CSS-only patch (no iframe reload) using the same mechanism as phase2-ui-hot-reload-stable
- Add a VSCode theme editor panel that shows all standard token names with their current values as color pickers and font selectors, writing changes back to the active theme file on edit

## Impact
- Affected specs: `docs/specs/ui-system.md`, `docs/specs/localization.md` (font fallback interacts with font tokens)
- Affected code: `src/ui/uz_ui_html.dll`, `src/ui/ThemeManager.cpp`, `src/engine/ProjectConfig.cpp`, `vscode-ext/src/panels/UIThemeEditorPanel.ts`
- Breaking change: NO (existing hardcoded CSS continues to work; theme tokens are additive)
- User benefit: The entire visual identity of a game's UI can be changed by editing a single theme file or calling one Lua function, making dark/light modes, accessibility themes, and DLC reskins trivial to implement and maintain.
