# String Table System — CSV Format, Key Lookup, Context Markers

> **Depends on**: Localization system (§23)
> **Used by**: Game code, Lua scripts, UI system (§21)
> **File formats**: `.uzloc` (CSV), `.uzlocalized` (JSON variants)

## Purpose

String tables store human-readable localized text in CSV format—machine parseable, diff-friendly, LLM-friendly. No binary formats, no XML, no unclear escaping. A single CSV file defines strings across all locales (columns = locales), with dot-notation keys for hierarchy and optional context markers for translator hints.

## The Problem

Traditional string tables use XML (`strings.xml`) or proprietary formats (`.xls`, `.po`). This creates friction:

- **XML is verbose** — overhead obscures actual translations
- **Separating per-locale files** — impossible to diff changes across languages
- **No context for translators** — "menu_button_1" gives no context. What should this button say?
- **Escaping hell** — XML entities, quotes, newlines require careful handling
- **Not AI-friendly** — LLMs struggle with XML structure, easier with flat CSV

## CSV Format — `.uzloc`

A single CSV file per translation domain (UI, gameplay, NPCs, etc.). Each column is a locale; each row is a key.

### Basic Format

```csv
# content/localization/ui.uzloc
# Format: key | en | pt_BR | es | ja | ar
# Comments start with #
# Empty lines are allowed

key,en,pt_BR,es,ja,ar
ui.menu.play,Play,Jogar,Jugar,プレイ,تشغيل
ui.menu.settings,Settings,Configurações,Opciones,設定,الإعدادات
ui.menu.quit,Quit Game,Sair do Jogo,Salir del Juego,ゲームを終了,خروج من اللعبة
ui.dialog.confirm,Are you sure?,Tem certeza?,¿Estás seguro?,確認してもよろしいですか？,هل أنت متأكد؟
game.npc.greeting,"Hello, traveler!","Olá, viajante!","¡Hola, viajero!","こんにちは、旅人！","مرحبا يا مسافر!"
```

**Escaping rules**:
- Quote fields containing commas: `"Hello, friend!"` not `Hello, friend!`
- Double-quote for literal quotes: `"Say ""hello"""` → `Say "hello"`
- Newlines in fields: literal `\n` becomes actual newline at runtime
- UTF-8 encoded, with optional BOM (stripped at parse time)

### Key Hierarchy

Keys use dot notation to organize strings into logical groups:

```
ui.menu.*        — Main menu strings
ui.dialog.*      — Dialog boxes
ui.hud.*         — In-game HUD
game.npc.*       — NPC dialogue
game.items.*     — Item names
game.effects.*   — Effect/ability names
```

Enables searching by prefix: `FindKeys("ui.menu.*")` returns all menu strings.

### Context Markers (Comments)

Translator context is embedded in the CSV as comment columns.

```csv
# content/localization/ui.uzloc
key,en,context_en,pt_BR,context_pt,ja
ui.button.ok,OK,Button label,OK,Rótulo de botão,わかりました
ui.button.cancel,Cancel,Button label,Cancelar,Rótulo de botão,キャンセル
game.item.sword,Iron Sword,Weapon name,Espada de Ferro,Nome da arma,鉄の剣
game.npc.farewell,Goodbye.,NPC dialogue,Adeus.,Diálogo do NPC,さようなら。
```

The `context_*` column is metadata only — skipped by the string loader. Tools can use it to:
- Show context in translation UI
- Generate developer documentation
- Flag untranslated strings

Format: any column matching `context_*` is treated as metadata.

## Loading and Parsing

CSV is hand-parsed (no external library dependency) to handle edge cases and produce clear error messages.

### StringTable Class

```cpp
namespace uz::Localization {
    /// In-memory string table. Loaded from .uzloc CSV file.
    class StringTable {
    public:
        /// Load CSV from path. Returns error if file not found or parse fails.
        /// Locale names come from header row (columns 2+).
        /// Returns ordered list of locale columns (e.g., {"en", "pt_BR", "es"}).
        static std::vector<std::string> Load(const uz::Path& csv_path);

        /// Get string for locale. Falls back: current → English → key itself.
        StringView Get(StringView key, StringView locale = "") const;

        /// Find all keys matching pattern (supports * wildcard).
        /// Example: FindKeys("ui.menu.*") → {"ui.menu.play", "ui.menu.settings", ...}
        std::vector<std::string> FindKeys(StringView pattern) const;

        /// Reload from disk. Called by FileWatcher on save (dev builds).
        void Reload(const uz::Path& csv_path);

        /// Get all locales in this table.
        const std::vector<std::string>& GetLocales() const;

        /// Stats for profiling/debugging.
        struct Stats {
            size_t total_strings = 0;
            size_t locales = 0;
            size_t total_bytes = 0;  // heap memory
        };
        Stats GetStats() const;

    private:
        // key → (locale → string)
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data_;
        std::vector<std::string> locales_;
    };
}
```

### Parse Implementation

Key points:

1. **UTF-8 BOM handling**: Strip `\xEF\xBB\xBF` if present
2. **Quote handling**: Track in-quote state; handle `""` as literal quote
3. **Comment lines**: Skip lines starting with `#`
4. **Empty lines**: Skip (allowed for readability)
5. **Error reporting**: Line number + column + message on parse failure

```cpp
// Pseudocode (simplified)
void ParseCSV(const std::string& content, StringTable& out) {
    std::istringstream stream(content);
    std::string line;
    int line_number = 0;

    // Read header (locales)
    std::getline(stream, line);
    line_number++;
    auto locales = SplitCSVLine(line);  // Skip first column (key)
    if (locales.size() < 2) {
        throw ParseError(line_number, "Need at least one locale column");
    }

    // Read rows
    while (std::getline(stream, line)) {
        line_number++;
        if (line.empty() || line[0] == '#') continue;

        auto fields = SplitCSVLine(line);
        if (fields.size() != locales.size() + 1) {
            throw ParseError(line_number, "Column count mismatch");
        }

        std::string key = fields[0];
        for (size_t i = 0; i < locales.size(); i++) {
            out.data_[key][locales[i]] = fields[i + 1];
        }
    }
}

// Quote-aware CSV split
std::vector<std::string> SplitCSVLine(const std::string& line) {
    std::vector<std::string> result;
    std::string field;
    bool in_quotes = false;

    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (c == '"') {
            if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                field += '"';  // Escaped quote
                i++;  // Skip next quote
            } else {
                in_quotes = !in_quotes;  // Toggle quote state
            }
        } else if (c == ',' && !in_quotes) {
            result.push_back(field);
            field.clear();
        } else {
            field += c;
        }
    }
    result.push_back(field);  // Last field
    return result;
}
```

## Multiple String Tables

A project can have multiple `.uzloc` files, loaded into a unified namespace:

```cpp
namespace uz::Localization {
    /// Manager for multiple string tables.
    class LocaleManager {
    public:
        /// Load all .uzloc files matching pattern.
        /// Pattern supports glob: "content/localization/*.uzloc"
        void LoadStringTables(const uz::Path& pattern);

        /// Get string from any loaded table. First match wins.
        StringView GetString(StringView key);

        /// Set active locale (affects all subsequent GetString calls).
        void SetLocale(StringView locale);

        /// Get current active locale.
        StringView GetCurrentLocale() const;
    };
}
```

## Usage in C++

```cpp
// Initialize at game start
uz::Localization::LocaleManager manager;
manager.LoadStringTables("content/localization/*.uzloc");
manager.SetLocale("pt_BR");

// Later: fetch strings
std::string play_text = uz::Localization::GetString("ui.menu.play");
// Returns "Jogar" (Portuguese), falls back to "Play" (English) if pt_BR missing

// Find all NPC dialogue keys
auto npc_keys = manager.FindKeys("game.npc.*");
// Returns: {"game.npc.greeting", "game.npc.farewell", ...}
```

## Usage in Lua

```lua
-- scripts/main_menu.lua
function on_start(ctx)
    -- ctx.locale is bound to LocaleManager
    local play_button_text = ctx.locale:get("ui.menu.play")
    play_button:set_text(play_button_text)

    local settings_text = ctx.locale:get("ui.menu.settings")
    settings_button:set_text(settings_text)
end

-- Runtime locale switching
function on_settings_changed(ctx, new_locale)
    ctx.locale:set_locale(new_locale)
    -- UI automatically updates via observer pattern
end
```

## Fallback Chain

When a string is not found in the active locale, the system cascades:

```
1. Active locale (e.g., "pt_BR")
2. Language root (e.g., "pt")
3. English ("en")
4. Key itself (never crashes)
```

Example:

```
User selects: pt_BR
Strings loaded: ui.menu.play exists in pt_BR and en

GetString("ui.menu.play") → "Jogar" (found in pt_BR)

User selects: pt (without BR)
Strings loaded: ui.menu.play exists only in en

GetString("ui.menu.play") → "Play" (falls back to en)

Missing key entirely:

GetString("game.unknown.key") → "game.unknown.key" (returns key itself)
```

## Hot-Reload

Development builds watch for CSV file changes and reload automatically.

```cpp
#if UZ_DEV_BUILD
uz::Localization::StartWatching("content/localization/*.uzloc");
// On save in editor: tables reloaded, UI updates instantly
#endif
```

## File Format Comparison

| Feature | CSV `.uzloc` | XML `strings.xml` | JSON `.json` | PO `.po` |
|---------|-----------|---------|---------|--------|
| Human readable | **Yes** | Somewhat | Somewhat | Yes |
| LLM friendly | **Yes** | No (verbose) | Yes | Yes |
| All locales in one file | **Yes** | No (per-locale) | Optional | No (per-locale) |
| Context for translators | **Yes** (comment cols) | Via comments | Limited | Yes |
| Escape sequences | Simple (`\n`, `""`) | XML entities | JSON escapes | Backslash escapes |
| Easy to diff | **Yes** | Hard (XML overhead) | Yes | Yes |
| Easy to parse | **Yes** (no library) | No (XML parser needed) | Yes | Limited |
| Null/empty handling | Clear | Via `<string/>` | Via `null` | Via empty string |

## Best Practices

1. **One file per domain**: `ui.uzloc`, `npc_dialogue.uzloc`, `gameplay.uzloc` — easier to manage, parallelize translation.
2. **Use context columns**: Always add `context_en` at minimum. Translators love clarity.
3. **Meaningful keys**: `ui.menu.play` not `button_1`. Keys must describe purpose.
4. **Keep strings short**: Ideally <100 chars. Long strings are harder to translate and render.
5. **Avoid placeholders in keys**: Put them in the value with `{placeholder}` syntax.
6. **Test fallback**: Always test with missing locales to ensure graceful fallback.
7. **Version-control the `.uzloc` files**: Changes are small diffs, easy to review. Use branch protection to prevent accidental translations.

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | CSV parser, StringTable class, basic Get/Find API, hot-reload |
| **Phase 3** | LocaleManager (multiple tables), fallback chain, context columns, Lua binding |
| **Phase 4** | Stats/profiling, validation tools, import/export helpers |
