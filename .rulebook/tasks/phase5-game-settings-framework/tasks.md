## 1. GameSettings Core Implementation
- [ ] 1.1 Create `src/core/game_settings.h` defining `SettingType` enum: `BOOL`, `INT`, `FLOAT`, `ENUM`, `KEYBIND`
- [ ] 1.2 Define `SettingDesc` struct in `src/core/game_settings.h` with fields: `const char* key`, `SettingType type`, `const char* display_name`, union default value, `const char** enum_values` (for ENUM type), `int enum_count`
- [ ] 1.3 Create `src/core/game_settings.cpp` implementing `GameSettings` class with `Register(const SettingDesc& desc)`, `Get(const char* key)`, `Set(const char* key, SettingValue value)`, `Reset(const char* key)`, and `GetAll()` methods
- [ ] 1.4 Define `SettingValue` union/variant in `src/core/game_settings.h` holding `bool b`, `int i`, `float f`, `const char* s` (enum string), `uint32_t key_code`
- [ ] 1.5 Implement type validation in `GameSettings::Set()`: if the value type does not match the registered `SettingType`, log an error and return `false` without applying the change
- [ ] 1.6 Implement `GameSettings::Reset(const char* key)` restoring the setting to its registered default value

## 2. Quality Preset Groups
- [ ] 2.1 Define `QualityPreset` struct in `src/core/game_settings.h` with fields `const char* name` and `std::vector<std::pair<std::string, SettingValue>> overrides`
- [ ] 2.2 Implement built-in presets `Low`, `Medium`, `High`, `Ultra` in `src/core/game_settings.cpp` that set common graphics quality keys (e.g. `graphics.shadow_quality`, `graphics.aa_mode`, `graphics.texture_quality`)
- [ ] 2.3 Implement `GameSettings::ApplyPreset(const char* name)` applying all overrides in the named preset atomically in a single EventBus batch

## 3. JSON Persistence
- [ ] 3.1 Implement `GameSettings::Save(const char* game_name)` serializing all non-default settings to `%APPDATA%/<game_name>/settings.json` with a top-level `"settings_version"` integer field
- [ ] 3.2 Implement `GameSettings::Load(const char* game_name)` reading `settings.json`, validating the `settings_version` field, and applying each stored key-value pair via `Set()`
- [ ] 3.3 Implement forward-compatible migration: if `settings_version` in the file is older than the current schema version, apply default values for any keys missing from the file; do not fail on unknown keys, simply skip them
- [ ] 3.4 Handle corrupt or missing JSON gracefully: if `Load()` fails to parse the file, log a warning and use all defaults; do not crash

## 4. EventBus Integration
- [ ] 4.1 Define `SettingsChangeEvent` template struct in `src/core/game_settings.h` with fields `const char* key` and `SettingValue new_value`
- [ ] 4.2 Fire `SettingsChangeEvent` through `EventBus` in `GameSettings::Set()` after successfully applying a new value
- [ ] 4.3 Fire a single batched `SettingsBatchChangeEvent` (list of changed keys) in `GameSettings::ApplyPreset()` to avoid N individual events for a preset

## 5. .uzproj Schema Declaration
- [ ] 5.1 Add `[settings]` section parsing in `src/engine/ProjectConfig.cpp` reading `[[settings.keys]]` TOML array into `std::vector<SettingDesc>`
- [ ] 5.2 Validate the schema at startup: if `GameSettings::Register()` is called with a key already registered to a different type, log an error and skip the re-registration

## 6. Lua API
- [ ] 6.1 Implement `ctx.settings:get(key)` Lua binding in `src/lua/lua_settings_api.cpp` returning the current value as an appropriate Lua type (bool, number, or string)
- [ ] 6.2 Implement `ctx.settings:set(key, value)` Lua binding calling `GameSettings::Set()`
- [ ] 6.3 Implement `ctx.settings:reset(key)` Lua binding calling `GameSettings::Reset()`
- [ ] 6.4 Implement `ctx.settings:apply_preset(name)` Lua binding calling `GameSettings::ApplyPreset()`
- [ ] 6.5 Implement `ctx.settings:on_change(key, handler)` Lua binding subscribing an EventBus listener that calls the Lua callback when the named key changes
- [ ] 6.6 Implement `ctx.settings:get_keybind(action)` Lua binding returning the current `uint32_t` key code for the named action setting

## 7. Testing
- [ ] 7.1 Create `tests/core/test_game_settings.cpp` with Google Test suite `GameSettingsTest`
- [ ] 7.2 Write test `RegisterAndGetBool`: register a bool setting with default `true`; call `Get("my.setting")`; assert the returned value is `true`
- [ ] 7.3 Write test `SetValidatesType`: register an int setting; call `Set("my.setting", bool_value)`; assert it returns `false` and the stored value is unchanged
- [ ] 7.4 Write test `ResetRestoresDefault`: set a setting to a non-default value; call `Reset("my.setting")`; assert `Get()` returns the default
- [ ] 7.5 Write test `ApplyPresetSetsAllKeys`: call `ApplyPreset("Low")`; assert all keys in the Low preset have their preset values
- [ ] 7.6 Write test `SaveAndLoadRoundtrip`: set 3 settings; call `Save("TestGame")`; create a new `GameSettings` instance; call `Load("TestGame")`; assert all 3 settings match the saved values
- [ ] 7.7 Write test `LoadHandlesCorruptFile`: write invalid JSON to the settings file; call `Load()`; assert it returns with all defaults and does not throw
- [ ] 7.8 Write test `LoadHandlesMissingFile`: call `Load()` with no file present; assert it completes with all defaults
- [ ] 7.9 Write test `SettingsChangeEventFiredOnSet`: subscribe to `SettingsChangeEvent` on EventBus; call `Set("vol", 0.5f)`; assert event received with `key == "vol"` and `new_value.f == 0.5f`
- [ ] 7.10 Write test `UnknownKeyInJsonSkipped`: add an unknown key to the settings JSON; call `Load()`; assert `Load()` completes without error and known keys are loaded correctly
- [ ] 7.11 Write test `EnumSettingRejectsInvalidValue`: register an enum setting with values `["low","high"]`; call `Set("quality", "ultra")`; assert it returns `false`
- [ ] 7.12 Verify C++ line coverage >= 95% and branch coverage >= 95% for `game_settings.cpp`

## 8. Documentation
- [ ] 8.1 Update `docs/specs/lua-scripting.md` with all `ctx.settings.*` API entries
- [ ] 8.2 Update `docs/specs/input-system.md` with `ctx.settings:get_keybind()` and keybind remapping flow
- [ ] 8.3 Update `docs/specs/event-bus.md` with `SettingsChangeEvent` and `SettingsBatchChangeEvent` definitions
