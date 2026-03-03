# Proposal: phase3-game-settings-framework

## Why
No framework exists for player-facing settings (graphics quality, keybinds, audio volume, accessibility options). Every game built on UzEngine currently reimplements settings persistence manually via ad-hoc JSON files, which leads to inconsistent schemas, missing migration logic, and settings data being lost or corrupted between sessions. A shared typed key-value settings framework eliminates this boilerplate and provides a consistent player experience across all UzEngine games.

## What Changes
- Add `GameSettings` typed key-value store with built-in setting types: `bool`, `int`, `float`, `enum` (string set), `keybind` (virtual key code)
- Built-in quality preset groups: `Low`, `Medium`, `High`, `Ultra` — each preset sets a batch of individual settings atomically
- JSON persistence at `%APPDATA%/<GameName>/settings.json`; schema versioned with `"settings_version"` field for forward-compatible migration
- `SettingsChangeEvent<T>` fired through EventBus when any setting changes — subsystems (renderer, audio, input) subscribe and apply changes immediately without restart
- Lua API: `ctx.settings:get(key)`, `ctx.settings:set(key, value)`, `ctx.settings:reset(key)`, `ctx.settings:apply_preset(name)`, `ctx.settings:on_change(key, handler)`
- `.uzproj` `[settings]` section declares the settings schema (keys, types, defaults, display names) that the engine validates against at startup
- Editor Settings panel in VSCode extension shows all declared settings with appropriate input widgets (toggle, slider, dropdown, keybind recorder)
- `ctx.settings:get_keybind(action)` returns the current key code for an action, supporting remapping

## Impact
- Affected specs: `docs/specs/input-system.md`, `docs/specs/audio-system.md`, `docs/specs/renderer.md`, `docs/specs/lua-scripting.md`, `docs/specs/event-bus.md`, `docs/specs/localization.md`
- Affected code: `src/core/game_settings.h`, `src/core/game_settings.cpp`, `src/lua/lua_settings_api.cpp`, `src/editor/settings_panel.cpp`, `src/input/input_system.cpp`
- Breaking change: NO
- User benefit: Players get a consistent settings experience that persists reliably across sessions; game developers replace hundreds of lines of boilerplate with a schema declaration and a few Lua calls
