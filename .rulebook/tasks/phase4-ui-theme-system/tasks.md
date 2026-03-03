## 1. Theme File Format and Project Configuration
- [ ] 1.1 Define the theme file format: a plain CSS file at `ui/themes/<name>.css` containing only `:root { }` CSS custom property declarations
- [ ] 1.2 Add `ui.themes` key to the `.uzproj` parser in `src/engine/ProjectConfig.cpp` accepting an array of theme name strings (e.g. `["dark", "light", "high-contrast"]`)
- [ ] 1.3 Add `ui.active_theme` key to the `.uzproj` parser (string, default `""` meaning no theme applied)
- [ ] 1.4 Expose parsed values as `ProjectConfig::ui_themes` (`std::vector<std::string>`) and `ProjectConfig::ui_active_theme` (`std::string`) in `src/engine/ProjectConfig.h`

## 2. Standard Token Set
- [ ] 2.1 Document the standard CSS custom property token names in `src/ui/ThemeManager.h` as a constant array: `--color-primary`, `--color-secondary`, `--color-surface`, `--color-on-surface`, `--color-error`, `--font-heading`, `--font-body`, `--font-mono`, `--spacing-sm`, `--spacing-md`, `--spacing-lg`, `--radius-sm`, `--radius-md`
- [ ] 2.2 Create a default `dark` theme at `ui/themes/dark.css` in the engine template project defining all 13 standard tokens
- [ ] 2.3 Create a default `light` theme at `ui/themes/light.css` in the engine template project defining all 13 standard tokens with appropriate light-mode values

## 3. ThemeManager Implementation
- [ ] 3.1 Create `src/ui/ThemeManager.h` declaring the `ThemeManager` class with methods `LoadTheme(const char* name)`, `SetActiveTheme(const char* name)`, `GetActiveTheme()`, and `ResolveThemePath(const char* name)`
- [ ] 3.2 Create `src/ui/ThemeManager.cpp` implementing theme loading: read the `.css` file at `ui/themes/<name>.css`, parse all `:root { }` CSS custom property declarations into a `std::unordered_map<std::string, std::string>`
- [ ] 3.3 Implement theme inheritance in `ThemeManager::LoadTheme()`: scan the CSS file for a `/* @extends <parent> */` comment header; if found, load the parent theme first and merge the child's declarations on top
- [ ] 3.4 Implement `ThemeManager::SetActiveTheme(const char* name)`: inject the resolved theme CSS file as the first `<style>` element in every screen's document head via the injected runtime script, before any game CSS
- [ ] 3.5 Implement zero-flash runtime switching in `ThemeManager::SetActiveTheme()`: call `document.documentElement.style.setProperty('--token', value)` for every token in the new theme via the injected script, replacing only changed properties

## 4. Lua API
- [ ] 4.1 Implement `ctx.ui:set_theme(name)` Lua binding in `src/lua/lua_ui_api.cpp` that calls `ThemeManager::SetActiveTheme(name)` and returns `true` on success or `false` with an error string if the theme file does not exist

## 5. Hot-Reload on Theme File Change
- [ ] 5.1 Register theme file paths with the `FileWatcher` in `ThemeManager::LoadTheme()` so that edits to active `ui/themes/*.css` files are detected
- [ ] 5.2 On theme file change, call `ThemeManager::SetActiveTheme()` to re-apply the CSS-only patch using the same mechanism as `ui.patch_css` (no iframe reload)

## 6. VSCode Theme Editor Panel
- [ ] 6.1 Create `vscode-uzengine/src/panels/UIThemeEditorPanel.ts` with a `UIThemeEditorPanel` class extending `vscode.WebviewPanel`
- [ ] 6.2 Query the active theme's token values via IPC command `ui.get_theme_tokens` returning `{token: string, value: string}[]`
- [ ] 6.3 Implement IPC handler `ui.get_theme_tokens` in `src/ipc/IpcServer.cpp` returning the current `ThemeManager` token map as JSON
- [ ] 6.4 Render each token in the panel with an appropriate input widget: color picker for `--color-*` tokens, font selector for `--font-*` tokens, numeric slider for `--spacing-*` and `--radius-*` tokens
- [ ] 6.5 On widget change, write the updated value back to the active theme CSS file on disk and call `ui.patch_css` via IPC to apply the change live
- [ ] 6.6 Register the panel as VSCode command `uzengine.openUIThemeEditor` in `vscode-uzengine/package.json` `contributes.commands`

## 7. Testing
- [ ] 7.1 Create `tests/ui/test_theme_manager.cpp` with Google Test suite `ThemeManagerTest`
- [ ] 7.2 Write test `LoadThemeParsesAllTokens`: load a test CSS file with 5 custom properties; assert all 5 are present in the resolved token map
- [ ] 7.3 Write test `ThemeInheritanceMergesTokens`: create a parent theme with tokens A and B; create a child theme with `/* @extends parent */` that overrides only token B; assert the resolved map contains A from parent and overridden B from child
- [ ] 7.4 Write test `MissingParentThemeLogs`: load a child theme referencing a non-existent parent; assert `LoadTheme` logs an error and falls back to loading the child tokens only
- [ ] 7.5 Write test `SetActiveThemeReturnsErrorForMissingTheme`: call `SetActiveTheme("nonexistent")`; assert it returns `false`
- [ ] 7.6 Write test `ProjectConfigParsesUiThemesArray`: parse a `.uzproj` with `ui.themes = ["dark", "light"]`; assert `ProjectConfig::ui_themes` contains both names
- [ ] 7.7 Create `vscode-uzengine/src/__tests__/UIThemeEditorPanel.test.ts` with Jest test suite `UIThemeEditorPanelTests`
- [ ] 7.8 Write test `TokensQueriedFromIPC`: assert the panel calls `ui.get_theme_tokens` on activation and populates the token list from the response
- [ ] 7.9 Write test `ColorTokenRendersColorPicker`: assert that a token beginning with `--color-` has a `<input type="color">` widget rendered
- [ ] 7.10 Write test `PanelCommandRegistered`: parse `package.json` and assert `uzengine.openUIThemeEditor` is in `contributes.commands`
- [ ] 7.11 Verify C++ line coverage >= 95% and branch coverage >= 95% for `ThemeManager.cpp`
- [ ] 7.12 Verify Jest line coverage >= 95% for `UIThemeEditorPanel.ts`

## 8. Documentation
- [ ] 8.1 Update `docs/specs/ui-system.md` with the theme file format, standard token names, theme inheritance syntax, hot-reload behavior, and `ctx.ui:set_theme` Lua API
