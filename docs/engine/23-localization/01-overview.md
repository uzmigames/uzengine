# Localization — I18N with ICU, HarfBuzz, and Per-Script Fonts

> **Status**: Planned (Phase 2–4)
> **Source**: `engine/localization/`
> **Dependencies**: ICU (CLDR, collation, text shaping), HarfBuzz (text shaping), SDF font atlas generation
> **References**: UE5 FText + StringTable, Godot TranslationServer, Unicode CLDR standards

## Purpose

Full Unicode localization system supporting 190+ locales. Covers string translation, plurals, gender, number/date formatting, bidirectional text (Arabic, Hebrew), complex scripts (CJK, Devanagari), and per-locale font variants. All systems designed for LLM-driven game development — localization data is human-readable (CSV) and hot-reloadable.

## Architecture

```
┌──────────────────────────────────────┐
│ Game Code / Scripts                   │
│   ctx.locale:get("ui.menu.play")     │
│   ctx.locale:format_plural(n, key)   │
│   ctx.locale:format_number(3.14)     │
└──────────────┬───────────────────────┘
               │
┌──────────────▼──────────────────────────────┐
│ Localization System (LocaleManager)         │
│  ├── StringTable (hot-reloadable CSV)       │
│  ├── CLDR formatter (plurals, numbers)      │
│  ├── Font resolver (per-locale, per-script) │
│  ├── Fallback chain (pt-BR → pt → en)      │
│  └── Runtime locale switching               │
└──────────────┬───────────────────────────────┘
               │
┌──────────────▼──────────────────────────────┐
│ Rendering (Text Shaping & Layout)           │
│  ├── HarfBuzz (glyph shaping)               │
│  ├── ICU BiDi (right-to-left handling)      │
│  ├── SDF atlas lookup (glyph rendering)     │
│  └── Text mesh generation per locale/font  │
└──────────────────────────────────────────────┘
```

## String Table System

Strings are organized in CSV files (human-editable, diff-friendly, AI-friendly). Keys use dot notation for hierarchy.

### `.uzloc` CSV Format

```csv
# content/localization/ui.uzloc
# Format: key, en, pt_BR, es, ja
# Comma-escaped strings use quotes

key,en,pt_BR,es,ja
ui.menu.play,Play,Jogar,Jugar,プレイ
ui.menu.settings,Settings,Configurações,Opciones,設定
ui.menu.quit,Quit Game,Sair do Jogo,Salir del Juego,ゲームを終了
ui.dialog.confirm,Are you sure?,Tem certeza?,¿Estás seguro?,確認してもよろしいですか？
game.npc.greeting,"Hello, traveler!","Olá, viajante!","¡Hola, viajero!","こんにちは、旅人！"
game.items.sword,Iron Sword,Espada de Ferro,Espada de Hierro,鉄の剣
```

Keys are case-insensitive. Quotes escape commas within fields.

### String Table API

```cpp
namespace uz::Localization {
    /// Get localized string for current locale, with fallback.
    /// Falls back: current → English → key itself (never crashes).
    StringView GetString(StringView key);

    /// Format string with placeholder substitution.
    /// Placeholders are {name} or {0}, {1}, etc.
    std::string FormatString(StringView key, const std::map<StringView, StringView>& args);

    /// Manually register a string (for programmatic strings, not UI).
    void RegisterString(StringView key, StringView locale, StringView text);

    /// Get all keys matching a pattern (for autocomplete, debugging).
    std::vector<std::string> FindKeys(StringView pattern);  // "ui.menu.*"
}

// Usage in C++:
std::string play_text = uz::Localization::GetString("ui.menu.play");

std::map<StringView, StringView> args;
args["player_name"] = "Alice";
std::string greeting = uz::Localization::FormatString(
    "game.npc.greeting.personalized",  // "Hello, {player_name}!"
    args
);  // Result: "Hello, Alice!"
```

### Lua API

```lua
-- scripts/menu.lua
function on_start(ctx)
    local play_text = ctx.locale:get("ui.menu.play")
    button:set_text(play_text)
end

function on_update(ctx, dt)
    if ctx.input:is_key_pressed("Enter") then
        local confirm = ctx.locale:get("ui.dialog.confirm")
        dialog:show_yes_no(confirm)
    end
end
```

## Pluralization — ICU Message Format

Support for plural forms and gender agreement across languages.

### Plural Rules

Different languages have different plural rules:
- English: "1 apple" vs "2 apples"
- Polish: 1 → `one`, 2–4 → `few`, 5+ → `many`
- Russian: Similar to Polish
- Japanese: No plural distinction (1りんご, 2りんご)

### Message Format with Plurals

```
# ui.uzloc — extended to support ICU message format
key, en, pt_BR
item.count, {count, plural, one {# item} other {# items}}, {count, plural, one {# item} other {# itens}}
```

In code:
```cpp
std::string text = uz::Localization::FormatPlural(
    "item.count",
    5  // count = 5
);  // Result: "5 items" (English) or "5 itens" (Portuguese)
```

For singular (count=1): Result: "1 item" / "1 item"

### Gender Agreement

Some languages (Spanish, German, French) require gender agreement in phrases.

```
# ui.uzloc — gender parameter
greeting, {gender, select, male {Hello, sir!} female {Hello, madam!} other {Hello!}}, ...
```

## Locale and Font Fallback Chain

When a locale is not available, the system cascades through fallback locales.

### Fallback Strategy

```cpp
// When user selects "pt-BR" (Brazilian Portuguese):
pt-BR → pt → en → (key itself)

// When user selects "zh-Hans" (Simplified Chinese):
zh-Hans → zh → en → (key itself)

// When user selects an unsupported locale like "fa" (Farsi):
fa → en → (key itself)  // Fallback to English, keep BiDi rules
```

```cpp
namespace uz::Localization {
    /// Set active locale. Initializes fonts, reloads strings.
    void SetLocale(StringView locale_code);  // "pt-BR", "ja", "en", etc.

    StringView GetCurrentLocale() const;

    /// Get all available locales.
    std::vector<std::string> GetAvailableLocales() const;

    /// Get localization metadata (native name, writing direction, etc).
    struct LocaleInfo {
        std::string code;          // "pt-BR"
        std::string native_name;   // "Português (Brasil)"
        std::string english_name;  // "Portuguese (Brazil)"
        bool is_rtl;               // right-to-left (Arabic, Hebrew)
        std::string script;        // "Latin", "Arab", "Hans", "Deva", etc.
    };
    LocaleInfo GetLocaleInfo(StringView code);
}
```

## Font System — Per-Script, Per-Locale

Different scripts require different fonts (Latin, CJK, Arabic, Devanagari). Each locale specifies its preferred fonts.

### Font Configuration

In project config (`.uzproj` TOML):

```toml
[localization]
locales = ["en", "pt-BR", "es", "ja", "zh-Hans", "ar", "hi"]

[localization.fonts]
# Default font for Latin script
latin = { path = "content/fonts/Roboto-Regular.ttf", size = 48 }

# CJK font (covers Chinese, Japanese, Korean)
cjk = { path = "content/fonts/NotoSansCJK-Regular.otf", size = 48 }

# Arabic script (contextual shaping)
arabic = { path = "content/fonts/NotoSansArabic-Regular.ttf", size = 48 }

# Devanagari (Hindi, Marathi, etc.)
devanagari = { path = "content/fonts/NotoSansDevanagari-Regular.ttf", size = 48 }

[localization.locales.ja]
# Override default font for Japanese locale
fonts = { cjk = "content/fonts/NotoSerifJP-Regular.otf" }

[localization.locales.ar]
fonts = { arabic = "content/fonts/GEDinarTwoArabic-Regular.ttf" }
is_rtl = true
```

### Font Resolution

```cpp
namespace uz::Localization {
    /// Get font asset for a given script and locale.
    /// Returns Latin font if script not available (graceful degradation).
    ResourceHandle<FontAsset> ResolveFont(StringView script, StringView locale);

    enum class Script { Latin, CJK, Arabic, Devanagari, Thai, Khmer };
    Script DetectScriptFromString(StringView text);
}

// Usage: render text in current locale
std::string text = uz::Localization::GetString("ui.menu.play");
auto script = uz::Localization::DetectScriptFromString(text);
auto font = uz::Localization::ResolveFont(script, ctx.locale);
renderer->DrawText(text, font);
```

### SDF Font Atlas

Signed Distance Field (SDF) glyph atlases support arbitrary font sizes without loss of quality.

**Per-locale atlas generation**:
1. Run `uzcook` with locale list
2. For each font + script combo, generate SDF atlas
3. Store compressed atlases in `.uzpak`
4. At runtime, each text mesh samples the SDF atlas for its locale/font

## Text Shaping — HarfBuzz + ICU BiDi

Complex scripts (Arabic, Devanagari, Thai) require glyph shaping. HarfBuzz handles contextual forms; ICU BiDi handles mixed directionality.

### Text Shaping Pipeline

```cpp
namespace uz::Localization {
    /// Shape text: convert Unicode string to shaped glyphs with positions.
    /// Handles contextual forms (Arabic), ligatures, diacritic placement.
    struct ShapedGlyph {
        uint32_t codepoint;   // Original Unicode codepoint
        uint32_t glyph_id;    // Glyph ID in font
        float advance_x;      // Horizontal advance (pixels)
        float advance_y;      // Vertical advance (for vertical scripts)
        int offset_x, offset_y;  // Baseline offset
    };

    std::vector<ShapedGlyph> ShapeText(
        StringView text,
        ResourceHandle<FontAsset> font,
        StringView locale
    );

    /// Apply BiDi reordering for right-to-left text.
    /// Input: logical order (storage order), Output: visual order (display order).
    /// For mixed LTR/RTL (e.g., "Hello עברית"), reorders logically coherent runs.
    struct BiDiRun {
        int start, end;     // byte offsets in original string
        bool is_rtl;        // true = right-to-left
    };
    std::vector<BiDiRun> AnalyzeBiDi(StringView text, StringView locale);
}
```

### Line Breaking

ICU line breaking respects script boundaries and grapheme clusters.

```cpp
namespace uz::Localization {
    /// Find optimal line break positions for text layout.
    /// Respects script rules (CJK can break anywhere, Arabic/Latin need word boundaries).
    struct LineBreak {
        int position;  // byte offset in string
        float width;   // pixels to this break point
    };
    std::vector<LineBreak> FindLineBreaks(
        StringView text,
        float max_width,
        ResourceHandle<FontAsset> font
    );
}
```

## Number and Date Formatting

Locale-specific formatting via ICU.

```cpp
namespace uz::Localization {
    /// Format number according to locale (thousands sep, decimal sep).
    /// US: "1,234.56", Germany: "1.234,56", India: "12,34,567.89"
    std::string FormatNumber(double value, StringView locale = "current");

    /// Format currency (symbol, thousands sep, decimal places).
    std::string FormatCurrency(double value, StringView currency_code, StringView locale = "current");
    // Example: FormatCurrency(99.99, "USD", "en-US") → "$99.99"
    // Example: FormatCurrency(99.99, "EUR", "de-DE") → "99,99 €"

    /// Format date/time according to locale.
    std::string FormatDateTime(
        uint64_t unix_timestamp,
        StringView pattern,  // "MMM d, yyyy" or "d/M/yy"
        StringView locale = "current"
    );
}
```

## Asset Localization

Texture and audio variants per locale (e.g., localized UI graphics, dubbed voice).

```json
// content/ui/menu_background.uzlocalized
{
    "default": "content/ui/menu_bg_en.png",
    "locales": {
        "es": "content/ui/menu_bg_es.png",
        "ja": "content/ui/menu_bg_ja.png",
        "ar": "content/ui/menu_bg_ar.png"
    }
}
```

In code:
```cpp
auto bg = ResourceSystem::Load<TextureAsset>("content/ui/menu_background.uzlocalized");
// Automatically loads the variant for current locale, falls back to "default"
```

## Hot-Reload

CSV files are automatically reloaded when changed on disk (development builds only).

```cpp
#if UZ_DEV_BUILD
    uz::Localization::StartWatching("content/localization/**/*.uzloc");
    // On save in editor: strings reloaded instantly, affected UIs update
#endif
```

## Comparison

| Feature | Unreal 5 | Godot 4 | Unity Localization | **UzEngine** |
|---------|----------|---------|---|--------------|
| String tables | `UStringTable` (structured) | `.translation` (PO-like) | `StringTable` asset | **CSV `.uzloc` (plain text)** |
| Plurals | Via `FTextFormat` | None built-in | Message format | **ICU message format** |
| Fonts | Per-locale font fallback | Font override per locale | FontAsset assignments | **Per-script SDF atlases** |
| Text shaping | HarfBuzz (integrated) | HarfBuzz (integrated) | None | **HarfBuzz + ICU BiDi** |
| RTL support | Via HarfBuzz + FTextBiDi | Via ICU | Via TextShaping | **ICU BiDi + visual reordering** |
| Number formatting | FText::AsNumber via locale | OS locale system | Locale-dependent | **ICU formatting** |
| Hot-reload | No (compile-in) | Yes (instant) | Via asset reload | **Yes (CSV hot-reload)** |
| CJK support | Yes (Han unification) | Yes | Yes | **Full CJK + Devanagari + Arabic** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | String table loader (CSV), basic fallback chain, SetLocale API |
| **Phase 3** | ICU integration (plurals, number formatting), HarfBuzz shaping, SDF font atlas |
| **Phase 4** | Per-locale font variants, BiDi support (RTL), asset localization |
| **Phase 5** | Message format advanced features (choice, ordinal), collation, calendar systems |
