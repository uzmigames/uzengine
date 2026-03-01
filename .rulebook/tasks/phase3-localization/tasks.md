## 1. CSV Parser Implementation

- [ ] 1.1 Create `src/engine/localization/csv_parser.h` — CSV parsing utilities
- [ ] 1.2 Create `src/engine/localization/csv_parser.cpp` — hand-written parser
- [ ] 1.3 Implement `CSVParser::parse_file()` — load CSV from disk, return map<key, map<locale, value>>
- [ ] 1.4 Implement `CSVParser::parse_string()` — parse CSV from memory buffer
- [ ] 1.5 Handle UTF-8 encoding (support CJK, accents, special characters)
- [ ] 1.6 Handle CSV escaping (double-quotes, commas in values, escaped quotes)
- [ ] 1.7 Skip comments (lines starting with `#`)
- [ ] 1.8 Strip BOM (UTF-8 BOM removed on load)
- [ ] 1.9 Handle empty cells (treated as missing translation)
- [ ] 1.10 Error reporting: line number + message for malformed CSV

## 2. LocaleManager Core

- [ ] 2.1 Create `src/engine/localization/locale_manager.h` — `LocaleManager` singleton interface
- [ ] 2.2 Create `src/engine/localization/locale_manager.cpp` — implementation
- [ ] 2.3 Implement `LocaleManager::init()` — load CSV, initialize from ProjectConfig
- [ ] 2.4 Implement `LocaleManager::translate()` overloads (StringId, const char*)
- [ ] 2.5 Implement `LocaleManager::translatef()` — printf-style formatting (%d, %s, %f, etc.)
- [ ] 2.6 Implement `LocaleManager::set_locale()` — change current locale at runtime
- [ ] 2.7 Implement `LocaleManager::current_locale()` — return current locale code
- [ ] 2.8 Implement `LocaleManager::default_locale()` — return default locale from config
- [ ] 2.9 Implement `LocaleManager::available_locales()` — return vector of available locale codes
- [ ] 2.10 Implement `LocaleManager::is_locale_available()` — check if locale exists in config

## 3. LocaleManager Debug Features

- [ ] 3.1 Implement `LocaleManager::get_all_keys()` — return all keys in CSV
- [ ] 3.2 Implement `LocaleManager::get_missing_keys()` — list keys missing in target locale
- [ ] 3.3 Implement `LocaleManager::get_display_name()` — locale display name (for UI dropdowns)
- [ ] 3.4 Implement `LocaleManager::get_font_for_locale()` — return font path from config

## 4. Font Fallback Integration

- [ ] 4.1 Update ProjectConfig to parse `[locale.fonts]` section from `.uzproj`
- [ ] 4.2 Update ProjectConfig to parse `[locale.display_names]` section from `.uzproj`
- [ ] 4.3 Implement `LocaleManager::get_font_for_locale()` to return configured font path
- [ ] 4.4 Document in `.uzproj` template: how to specify fonts per locale
- [ ] 4.5 Add support for CJK font recommendation (ja, ko, zh_cn auto-detect)

## 5. C++ API Integration

- [ ] 5.1 Create `src/engine/localization/locale_event.h` — `LocaleChangedEvent` struct
- [ ] 5.2 Register `LocaleChangedEvent` with EventBus on `set_locale()`
- [ ] 5.3 Expose `LocaleManager` via `GameContext` (e.g., `ctx->locale_manager`)
- [ ] 5.4 Add convenience method to `GameContext`: `const char* translate(const char* key)`
- [ ] 5.5 Test C++ API: translate, translatef, set_locale, event broadcast

## 6. Lua API Integration

- [ ] 6.1 Create `src/engine/scripting/lua_locale_api.h/cpp` — bind `ctx.locale` global
- [ ] 6.2 Implement `ctx.locale:set(locale_code)` → set current locale
- [ ] 6.3 Implement `ctx.locale:t(key)` → translate key
- [ ] 6.4 Implement `ctx.locale:tf(key, ...)` → translate with printf-style args
- [ ] 6.5 Implement `ctx.locale:current()` → return current locale code
- [ ] 6.6 Implement `ctx.locale:available_locales()` → return array of locale codes
- [ ] 6.7 Implement `ctx.locale:get_missing_keys(locale)` → editor-only debug feature
- [ ] 6.8 Test Lua API from game script (via IPC lua.eval in editor mode)

## 7. CSV Data & Project Integration

- [ ] 7.1 Create `res://locale/strings.csv` example file with sample strings
- [ ] 7.2 Populate example strings: UI (menu, HUD), game (objectives, victory), NPC dialog
- [ ] 7.3 Include at least 6 languages: en, pt_br, es, fr, de, ja
- [ ] 7.4 Update `.uzproj` template to include `[locale]` section
- [ ] 7.5 Configure `[locale]` with: default locale, available locales, strings_path, fonts, display_names
- [ ] 7.6 Add `.uzproj` config keys: `default`, `available`, `strings_path`, `[locale.fonts]`, `[locale.display_names]`

## 8. Hot-Reload in Editor Mode

- [ ] 8.1 Create FileWatcher subscription for `res://locale/strings.csv`
- [ ] 8.2 On CSV change, call `LocaleManager::reload_csv()`
- [ ] 8.3 Implement `LocaleManager::reload_csv()` — re-parse CSV from disk
- [ ] 8.4 Broadcast `LocaleChangedEvent` after reload (triggers UI refresh)
- [ ] 8.5 Test hot-reload: edit CSV, save, verify translations update in game

## 9. IPC Debug Handlers

- [ ] 9.1 Register `locale.get_available` IPC handler — return available locales
- [ ] 9.2 Register `locale.get_current` IPC handler — return current locale
- [ ] 9.3 Register `locale.set` IPC handler — change locale (editor-only)
- [ ] 9.4 Register `locale.get_missing_keys` IPC handler — list untranslated keys
- [ ] 9.5 Register `locale.export_csv` IPC handler — export single locale as CSV
- [ ] 9.6 Register `locale.all_keys` IPC handler — return all keys in CSV
- [ ] 9.7 Test handlers via MCP tool calls in editor mode

## 10. Fallback & Error Handling

- [ ] 10.1 Implement 3-tier fallback: (1) current locale, (2) English, (3) key itself
- [ ] 10.2 Ensure `translate()` never returns null or crashes (always safe)
- [ ] 10.3 Log warning if key not found (editor mode only)
- [ ] 10.4 Test missing key behavior in each locale

## 11. Formatting & Internationalization

- [ ] 11.1 Implement printf-style formatting in `translatef()` — %d, %s, %f, %x, etc.
- [ ] 11.2 Support variable argument count in Lua: `ctx.locale:tf("key", arg1, arg2, ...)`
- [ ] 11.3 Test formatting with integers, floats, strings
- [ ] 11.4 Test with number formatting (locale-aware decimal separators — Phase 4)

## 12. Testing & Validation

- [ ] 12.1 Unit tests: CSV parser (valid CSV, escaped commas, quotes, newlines, UTF-8)
- [ ] 12.2 Unit tests: Fallback behavior (current locale, English fallback, key fallback)
- [ ] 12.3 Unit tests: translatef() with various format specifiers
- [ ] 12.4 Unit tests: set_locale(), verify all subsequent translate() calls use new locale
- [ ] 12.5 Integration tests: Load CSV on startup, all locales populated
- [ ] 12.6 Integration tests: Lua API: ctx.locale:t(), ctx.locale:tf()
- [ ] 12.7 Integration tests: Runtime locale change (C++ and Lua)
- [ ] 12.8 Integration tests: Hot-reload CSV in editor mode
- [ ] 12.9 Integration tests: Font loading per locale
- [ ] 12.10 Error case tests: malformed CSV, missing file, corrupt encoding

## 13. Performance & Memory

- [ ] 13.1 Benchmark CSV parse time (target: <50ms for 10,000 strings)
- [ ] 13.2 Benchmark translate() lookup (target: <1µs via hash map)
- [ ] 13.3 Benchmark translatef() formatting (target: <1ms)
- [ ] 13.4 Verify memory usage (estimate: ~50KB for 5,000 strings + 6 locales)
- [ ] 13.5 Profile hot-reload performance (editor mode)

## 14. Documentation

- [ ] 14.1 Confirm `docs/specs/localization.md` matches implementation
- [ ] 14.2 Update `docs/ipc-mcp.md` with `locale.*` handlers and example calls
- [ ] 14.3 Create example game script showing locale usage (change language, get missing keys)
- [ ] 14.4 Document CSV format in developer guide (key naming, escaping, UTF-8)
- [ ] 14.5 Document `[locale]` section in `.uzproj` template
- [ ] 14.6 Add localization section to `docs/guides/getting-started.md`

## 15. Example Assets

- [ ] 15.1 Create sample `res://locale/strings.csv` with 50+ strings
- [ ] 15.2 Include UI strings (menu, buttons, HUD)
- [ ] 15.3 Include game strings (objectives, victory, NPC dialog)
- [ ] 15.4 Include all 6 languages with natural, professional translations
- [ ] 15.5 Create `.uzproj` template with complete `[locale]` section
