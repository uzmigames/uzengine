# Spec: Localization / I18n System

> **Status**: Planned (Phase 3)
> **Format**: Plain-text CSV for maximum AI compatibility
> **Default locale**: Configured per project (e.g., `en`)
> **Available locales**: Configured in `.uzproj`
> **Runtime switching**: Full locale change while game is running
> **Font fallback**: Per-locale font overrides (e.g., CJK fonts for Japanese)

---

## Overview

Localization is a **first-class feature** of UzEngine designed for AI-assisted game development. All game text is externalized to CSV files, enabling:

- **Instant AI translation** — LLMs can generate strings in multiple languages simultaneously
- **Live text editing** — change strings without rebuilding the game
- **Transparent fallback** — missing keys show the English version (never crashes)
- **VSCode integration** — missing key detection, export workflow for translators
- **Runtime language switch** — players change language without restarting

The system prioritizes **plain-text, human-readable formats** over binary serialization.

---

## Architecture

```
.uzproj (project config)
│  ├── [locale]
│  │   ├── default = "en"
│  │   ├── available = ["en", "pt_br", "es", "fr", "de", "ja"]
│  │   ├── strings_path = "res://locale/strings.csv"
│  │   └── [locale.fonts]
│  │       └── ja = "res://fonts/NotoSansCJK-Regular.ttf"
│
res://locale/
│  └── strings.csv
│     (keyed translation table)
│
render.exe
│  └── LocaleManager (singleton)
│       ├── Load CSV on startup
│       ├── resolve(key, locale) → translated string
│       ├── set_locale(locale)  → change language at runtime
│       └── get_missing_keys()  → list untranslated keys (debug)
│
game.dll (C++)
│  └── Call LocaleManager::get().translate(key)
│
ctx (Lua)
│  └── ctx.locale:set("pt_br")  — switch language
│  └── ctx.locale:t("key")      — translate key
│  └── ctx.locale:tf("key", ...) — format with printf-style args
```

---

## CSV Format

**File**: `res://locale/strings.csv`

```csv
key,en,pt_br,es,fr,de,ja
ui.menu.play,Play,Jogar,Jugar,Jouer,Spielen,プレイ
ui.menu.quit,Quit,Sair,Salir,Quitter,Beenden,終了
ui.menu.settings,Settings,Configurações,Configuración,Paramètres,Einstellungen,設定
ui.hud.health,Health: %d,Saúde: %d,Salud: %d,Santé: %d,Gesundheit: %d,体力: %d
ui.hud.score,Score: %d,Pontuação: %d,Puntuación: %d,Score: %d,Punktzahl: %d,スコア: %d
ui.dialog.npc_greeting,Hello %s!,Olá %s!,¡Hola %s!,Bonjour %s!,Hallo %s!,こんにちは%s!
game.objective,Find the key,Encontre a chave,Encuentra la llave,Trouvez la clé,Finden Sie den Schlüssel,鍵を探す
game.victory,You win!,Você venceu!,¡Ganaste!,Vous gagnez!,Du gewinnst!,勝ちました！
```

**Format rules**:
- First row is header: `key,en,pt_br,es,...` (language codes)
- Each subsequent row is one string entry
- Language codes are ISO 639-1 or custom (e.g., `pt_br` for Brazilian Portuguese)
- Empty cells are permitted (fallback to English key)
- Keys use dot notation: `ui.menu.play`, `game.objective`, etc.
- No quotes around values (unless value contains commas — use standard CSV escaping)

**Escaping**:
- Commas in values: wrap value in double-quotes: `key,"Hello, world!",Olá, mundo!`
- Double-quotes in values: escape as `""`: `key,"He said ""hi""",Ele disse ""oi"""`
- Newlines in values: not recommended (keep strings single-line)

---

## File Organization

### Single CSV File (Recommended)

All strings in one file:

```
res://locale/
└── strings.csv      (5000+ lines, all keys and languages)
```

**Pros**: Simple, easy for AI to regenerate all translations at once
**Cons**: Large file, harder to organize by category

### Multiple CSV Files (Alternative)

Split by category:

```
res://locale/
├── ui.csv          (all ui.* keys)
├── game.csv        (all game.* keys)
├── dialog.csv      (all dialog.* keys)
└── menus.csv       (all menus.* keys)
```

In `.uzproj`:

```toml
[locale]
strings_path = "res://locale/*.csv"  # Glob pattern, all files loaded
```

**Pros**: Organized, easier to hand to translators by category
**Cons**: Must merge files when generating AI translations

---

## C++ API

### LocaleManager Class

Located in `src/engine/localization/locale_manager.h`:

```cpp
class LocaleManager {
public:
    // Static singleton
    static LocaleManager& get();

    // Initialize with CSV file(s) and project config
    // Called by engine on startup
    Result<void> init(const ProjectConfig& config);

    // Translate a key using current locale
    // Returns English fallback if translation missing
    const char* translate(StringId key) const;
    const char* translate(const char* key_cstr) const;

    // Formatted translation (printf-style arguments)
    std::string translatef(StringId key, ...);
    std::string translatef(const char* key_cstr, ...);

    // Get current locale
    const char* current_locale() const;

    // Change locale at runtime
    // Triggers locale_changed event (subscribable in game.dll)
    Result<void> set_locale(const char* locale_code);

    // Get all available locales from config
    std::vector<std::string> available_locales() const;

    // Get default locale from config
    const char* default_locale() const;

    // DEBUG: List all keys present in a locale (editor only)
    std::vector<std::string> get_all_keys() const;

    // DEBUG: List keys present in default locale but missing from a target locale
    std::vector<std::string> get_missing_keys(const char* target_locale) const;

    // Get font override for a locale
    // Returns font file path (res:// URL), or empty if using default font
    const char* get_font_for_locale(const char* locale_code) const;

    // Check if a locale is available
    bool is_locale_available(const char* locale_code) const;

    // Get locale display name (for UI dropdowns)
    // e.g., "pt_br" → "Português (Brasil)"
    const char* get_display_name(const char* locale_code) const;
};
```

### Usage in C++

```cpp
// At startup (game.dll init)
LocaleManager::get().init(ctx->config);
LocaleManager::get().set_locale("en");  // Load English strings

// In game code
const char* text = LocaleManager::get().translate("ui.menu.play");
ctx->log(text);  // "Play" (or Portuguese/Spanish/etc. depending on locale)

// Formatted strings
std::string msg = LocaleManager::get().translatef("ui.hud.health", 75);
// CSV has: "ui.hud.health,Health: %d,Saúde: %d,..."
// Result: "Health: 75" (or "Saúde: 75" in Portuguese)

// Runtime language change
LocaleManager::get().set_locale("pt_br");
const char* text2 = LocaleManager::get().translate("ui.menu.play");  // "Jogar"
```

---

## Lua API

Game scripts access localization via `ctx.locale`:

```lua
-- Set current language
ctx.locale:set("pt_br")

-- Translate a key (returns English if missing)
local text = ctx.locale:t("ui.menu.play")
ctx:log(text)  -- "Jogar"

-- Formatted translation
local msg = ctx.locale:tf("ui.hud.health", 100)
ctx:log(msg)  -- "Saúde: 100"

-- Get current locale
local current = ctx.locale:current()
ctx:log(current)  -- "pt_br"

-- Get available locales
for _, locale in ipairs(ctx.locale:available_locales()) do
    ctx:log("Available: " .. locale)
end

-- Detect missing translations (editor mode only)
local missing = ctx.locale:get_missing_keys("es")
if #missing > 0 then
    ctx:log("Missing " .. #missing .. " Spanish translations")
    for _, key in ipairs(missing) do
        ctx:log("  - " .. key)
    end
end
```

---

## CSV Parsing

### Implementation

Use a simple hand-written CSV parser (no external dependency):

```cpp
// src/engine/localization/csv_parser.h/cpp
class CSVParser {
public:
    // Load CSV file, return map of key → (lang → value)
    static Result<std::unordered_map<std::string, std::unordered_map<std::string, std::string>>>
    parse_file(const char* path);

    // Parse CSV blob from memory
    static Result<...> parse_string(const char* csv_content);
};
```

**Parser must handle**:
- UTF-8 encoding (for CJK and accented characters)
- CSV escaping (double-quotes, commas in values)
- Empty cells (treated as "missing translation")
- Comments (lines starting with `#` are ignored)
- BOM (UTF-8 BOM is stripped on load)

---

## Project Configuration (`.uzproj`)

```toml
[locale]
# Default locale code
default = "en"

# All available locales
available = ["en", "pt_br", "es", "fr", "de", "ja"]

# Path to CSV file(s) — can be a single file or glob pattern
strings_path = "res://locale/strings.csv"
# Glob example: "res://locale/*.csv"

# Display names for languages (optional, used in UI dropdowns)
[locale.display_names]
en = "English"
pt_br = "Português (Brasil)"
es = "Español"
fr = "Français"
de = "Deutsch"
ja = "日本語"

# Font overrides per locale (optional)
[locale.fonts]
ja = "res://fonts/NotoSansCJK-Regular.ttf"
ko = "res://fonts/NotoSansCJK-Regular.ttf"
zh_cn = "res://fonts/NotoSansCJK-Regular.ttf"
```

---

## Hot-Reload (Editor Mode)

In `--editor` mode, CSV files are watched. When a file changes:

```
Developer saves strings.csv
  ↓
FileWatcher detects change
  ↓
LocaleManager::reload_csv() is called
  ↓
Game receives `locale.changed` event (broadcast)
  ↓
UI updates (labels, buttons refresh with new strings)
```

**In game.dll**:

```cpp
ctx->event_bus.subscribe<LocaleChangedEvent>([](const LocaleChangedEvent& e) {
    ctx->log("Locale changed to: " + std::string(e.locale_code));
    // Refresh UI labels, subtitle text, etc.
});
```

---

## String Key Naming Conventions

Use hierarchical dot notation:

```
ui.*                    → All UI strings
  ui.menu.*             → Main menu
    ui.menu.play        → "Play" button
    ui.menu.quit        → "Quit" button
  ui.hud.*              → In-game HUD
    ui.hud.health       → Health display
    ui.hud.score        → Score display
  ui.dialog.*           → Dialog boxes
    ui.dialog.confirm   → "Confirm" button
    ui.dialog.cancel    → "Cancel" button

game.*                  → Game-specific strings
  game.objective.*      → Objectives / quests
  game.victory          → Victory message
  game.defeat           → Defeat message

npc.*                   → NPC dialog / messages
  npc.greeting          → Generic NPC greeting
  npc.farewell          → Generic NPC farewell

dialog.*                → Cutscene / story dialog (may be very large)
  dialog.intro.*        → Intro sequence
  dialog.final_boss.*   → Final boss encounter
```

---

## Fallback & Missing Key Behavior

When a translation key is not found:

1. **Current locale**: Try to find key in current locale's column
2. **English fallback**: If not found, use English translation
3. **Key as fallback**: If English also missing, return the key itself (e.g., `"ui.menu.play"`)

**Example**:

```csv
key,en,pt_br,es
ui.test,"Test","Teste","Prueba"
ui.missing,"Missing",<empty>,"Faltan"
```

- `translate("ui.test")` in Portuguese → `"Teste"` (found)
- `translate("ui.missing")` in Portuguese → `"Missing"` (Portuguese empty, fallback to English)
- `translate("ui.missing")` in Spanish → `"Faltan"` (found in Spanish)
- `translate("ui.unknown")` in any locale → `"ui.unknown"` (key not in CSV)

**This ensures the game never crashes on missing strings** — a critical requirement for AI-driven development where translators may not keep up with rapidly changing features.

---

## IPC Handlers (Editor Mode)

Available in `--editor` mode for VSCode extension integration:

| Handler | Args | Returns | Purpose |
|---------|------|---------|---------|
| `locale.get_available` | — | `["en", "pt_br", "es", ...]` | List available locales |
| `locale.get_current` | — | `"en"` | Get current locale |
| `locale.set` | `{locale: "pt_br"}` | `{ok: bool}` | Change locale |
| `locale.get_missing_keys` | `{locale: "es"}` | `["key1", "key2", ...]` | List untranslated keys |
| `locale.export_csv` | `{locale: "es"}` | `{csv: string}` | Export locale as CSV |
| `locale.all_keys` | — | `["ui.menu.play", "ui.menu.quit", ...]` | All keys in CSV |

---

## Font Fallback

Different languages require different fonts:

- **Latin** (English, Spanish, French, German, Portuguese): Default font OK
- **CJK** (Chinese, Japanese, Korean): Require CJK font (e.g., Noto Sans CJK)
- **Arabic/Hebrew**: Right-to-left layout (Phase 4)

### Font Selection

When `set_locale(locale_code)` is called:

1. Check `.uzproj` `[locale.fonts]` for locale-specific font
2. If found, load that font and use it for all text rendering
3. If not found, use default font

```cpp
// In game.dll or UI renderer
const char* font_path = LocaleManager::get().get_font_for_locale("ja");
if (font_path) {
    load_font(font_path);  // Load Noto Sans CJK
} else {
    load_font("res://fonts/default.ttf");  // Fallback
}
```

---

## Workflow for AI-Assisted Translation

### 1. Developer writes game in English

```csv
key,en
ui.menu.play,Play
ui.menu.quit,Quit
game.objective,Find the key
```

### 2. Developer prompts Claude with full CSV

```
Translate this game to Portuguese, Spanish, French, German, and Japanese.
Maintain the CSV format exactly.

key,en,pt_br,es,fr,de,ja
ui.menu.play,Play,...
...
```

### 3. Claude regenerates CSV with all translations

```csv
key,en,pt_br,es,fr,de,ja
ui.menu.play,Play,Jogar,Jugar,Jouer,Spielen,プレイ
ui.menu.quit,Quit,Sair,Salir,Quitter,Beenden,終了
...
```

### 4. Developer saves CSV, game immediately shows translations

Hot-reload picks up the change, new text appears in game.

---

## Plural Forms & Context (Phase 4)

> **Deferred to Phase 4**

English uses simple plurals: "1 apple", "2 apples". Other languages have more complex rules.

Phase 4 will add:

```cpp
// Plural form selection
int count = 5;
const char* text = LocaleManager::get().translate_plural("item.apple", count);
// CSV: "item.apple,apple|apples,maçã|maçãs,manzana|manzanas,..."
// Result: "apples" (English), "maçãs" (Portuguese)
```

---

## Right-to-Left Layout (Phase 4)

> **Deferred to Phase 4**

Arabic and Hebrew read right-to-left. Phase 4 will add:

```cpp
// Check if locale requires RTL layout
bool is_rtl = LocaleManager::get().is_rtl(locale_code);
if (is_rtl) {
    ui_panel.set_text_align(TextAlign::Right);
}
```

---

## Testing

### Unit Tests

- [ ] CSV parser: round-trip (write → parse → compare)
- [ ] UTF-8 handling: CJK, accents, special characters
- [ ] CSV escaping: commas, quotes, newlines
- [ ] Fallback behavior: missing key → English → key itself
- [ ] Formatted strings: printf-style %d, %s, %f
- [ ] Hot-reload: edit CSV, verify changes appear

### Integration Tests

- [ ] Load strings.csv on startup
- [ ] Set locale at runtime, verify translate() returns correct language
- [ ] Lua API: ctx.locale:t(), ctx.locale:tf()
- [ ] Missing key detection via IPC
- [ ] Font fallback: Japanese font loads for ja locale
- [ ] Event broadcast on locale change

---

## Storage & Distribution

### In Editor

Strings stored in `res://locale/strings.csv` (workspace file).

### In Shipped Game

During Cook phase, CSV is:
1. Parsed from source CSV
2. Compressed to binary locale archive (or kept as CSV for small games)
3. Packed into `.uzpak` game archive

Option: Keep CSV in shipped game for post-release localization updates (no rebuild needed).

---

## Phase Roadmap

| Phase | Deliverable |
|-------|-------------|
| **Phase 3** | CSV format, LocaleManager, Lua API, runtime locale switching, hot-reload |
| **Phase 4** | Plural forms, RTL layout, binary locale archives, locale export workflow |

---

## Performance Considerations

- **CSV parse time**: ~10ms for 5000 strings (one-time at startup)
- **Lookup**: O(1) hash map lookup (negligible)
- **String format** (translatef): ~1ms for typical formatted string
- **Hot-reload**: ~50ms to reparse CSV (editor mode only)

No runtime performance concerns — LocaleManager is lightweight.

---

## Related Docs

- [Scene System](scene-system.md) — node properties can reference string keys
- [Engine Design Decisions](../engine/design-decisions.md) — Section 4.3: Why CSV over binary formats
- [VSCode Extension](../guides/vscode-extension.md) — String editing UI in Inspector
- [Build System](build-system.md) — Cook phase handling of locale data
