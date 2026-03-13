# ICU Message Format — Plurals, Gender, Number/Date/Currency Formatting

> **Depends on**: String tables (§23.2), ICU library
> **Used by**: Game strings with context-sensitive formatting, Lua API
> **Standards**: Unicode CLDR, ICU Message Format Syntax

## Purpose

ICU (International Components for Unicode) provides sophisticated localization formatting. Different languages have different plural rules, gender agreement requirements, and number formats. English says "1 apple, 5 apples" (two forms); Polish says "1 jabłko, 2–4 jabłka, 5+ jabłek" (three forms); Japanese ignores plurals entirely. ICU encodes these rules per-locale, allowing game code to write once and localize to any language.

## Plural Forms

### The Problem

Pluralization varies wildly across languages:

- **English**: one (1) vs other (all others)
- **Polish**: one (1), few (2–4), many (5–21, 25–31, ...), other
- **Russian**: one (1), few (2–4), many (0, 5–20, 25–30, ...), other
- **French**: one (0–1), other
- **Japanese, Chinese**: no plural distinction
- **Arabic**: zero, one, two, few (3–10), many (11–99), other

### ICU Plural Rules

ICU CLDR (Common Locale Data Repository) defines plural categories for all locales:

```cpp
// CLDR plural categories
enum class PluralCategory {
    Zero,    // e.g., Arabic "zero items"
    One,     // singular
    Two,     // dual (rare)
    Few,     // 2–4 items
    Many,    // 5+ items
    Other    // catch-all (always required)
};
```

### Message Format with Plurals

Strings use ICU message format with embedded plural choices:

```csv
# ui.uzloc
key,en,pt_BR,ja
item.count,{count, plural, one {# item} other {# items}},{count, plural, one {# item} other {# itens}},{count, plural, other {#個のアイテム}}
```

Parsing:

- `{count, plural, ...}` — a plural choice block
- `one {...}` — format if count is in "one" category
- `other {...}` — format otherwise
- `#` — placeholder for the count value

### Usage in C++

```cpp
namespace uz::Localization {
    /// Format string with plural handling.
    /// Detects locale's plural category for count, substitutes message.
    std::string FormatPlural(
        StringView key,
        int count,
        StringView locale = ""  // current locale if empty
    );
}

// Usage:
int apple_count = 5;
std::string text = uz::Localization::FormatPlural("item.count", apple_count);
// Locale en: "5 items"
// Locale pt_BR: "5 itens"
// Locale ja: "5個のアイテム"

// For count=1:
std::string text = uz::Localization::FormatPlural("item.count", 1);
// Locale en: "1 item"
// Locale pt_BR: "1 item"
// Locale ja: "1個のアイテム"
```

### Advanced Plurals

Combining plural with other parameters:

```csv
# game.uzloc
key,en,es
damage.dealt,"{damage, plural, one {You dealt # damage!} other {You dealt # total damage!}}","{damage, plural, one {¡Hiciste # de daño!} other {¡Hiciste # de daño total!}}"
inventory.slots,"{used}/{total} slots: {used, plural, one {# item equipped} other {# items equipped}}","{used}/{total} espacios: {used, plural, one {# artículo} other {# artículos}}"
```

Usage:

```cpp
std::map<std::string, int> args = {
    {"damage", 15},
    {"used", 3},
    {"total", 20}
};
std::string dmg_text = uz::Localization::FormatMessage("damage.dealt", args);
// "You dealt 15 total damage!"

std::string slot_text = uz::Localization::FormatMessage("inventory.slots", args);
// "3/20 slots: 3 items equipped"
```

## Gender Agreement

Some languages (Spanish, German, French, Italian) require gender agreement in phrases.

### Message Format with Gender

```csv
# ui.uzloc
key,en,es,de
greeting,"{gender, select, male {Hello, sir!} female {Hello, madam!} other {Hello!}}","{gender, select, male {¡Hola, señor!} female {¡Hola, señora!} other {¡Hola!}}","{gender, select, male {Hallo, Herr!} female {Hallo, Frau!} other {Hallo!}}"
article,"{gender, select, male {the hero} female {the heroine} other {the hero}}","{gender, select, male {el héroe} female {la heroína} other {el héroe}}","{gender, select, male {der Held} female {die Heldin} other {der Held}}"
```

### Usage

```cpp
std::string text = uz::Localization::FormatMessage("greeting", {{"gender", "female"}});
// Locale en: "Hello, madam!"
// Locale es: "¡Hola, señora!"
// Locale de: "Hallo, Frau!"
```

**Note**: Gender is a user/character attribute, not auto-detected. Store it on Character entities:

```cpp
class CharacterComponent {
    enum class Gender { Male, Female, Other };
    Gender gender_ = Gender::Other;
};

// When formatting:
std::map<std::string, std::string> args;
args["gender"] = character->GetGender() == Gender::Male ? "male" : "female";
```

## Number Formatting

Different locales format numbers differently:

- US English: `1,234.56` (comma thousands separator, dot decimal)
- Germany: `1.234,56` (dot thousands separator, comma decimal)
- India: `12,34,567.89` (unique grouping: 2–3–3 digits)
- French: `1 234,56` (space thousands separator, comma decimal)
- Arabic: `١٬٢٣٤٫٥٦` (Arabic numerals)

### Formatting API

```cpp
namespace uz::Localization {
    /// Format number according to locale conventions.
    std::string FormatNumber(
        double value,
        StringView locale = ""  // current locale if empty
    );

    /// Format number with fixed decimal places.
    std::string FormatNumber(
        double value,
        int decimal_places,
        StringView locale = ""
    );

    /// Format as percentage.
    std::string FormatPercent(
        double value,      // 0.75 → "75%"
        StringView locale = ""
    );

    /// Format as integer (no decimals).
    std::string FormatInteger(
        int64_t value,
        StringView locale = ""
    );
}

// Usage:
uz::Localization::FormatNumber(1234.56, "en_US");   // "1,234.56"
uz::Localization::FormatNumber(1234.56, "de_DE");   // "1.234,56"
uz::Localization::FormatNumber(1234.56, "fr_FR");   // "1 234,56"
uz::Localization::FormatPercent(0.75, "en_US");     // "75%"
```

## Currency Formatting

Format money according to locale (symbol placement, decimal places, spacing).

```cpp
namespace uz::Localization {
    /// Format currency value with locale-specific symbol and conventions.
    std::string FormatCurrency(
        double value,
        StringView currency_code,  // "USD", "EUR", "GBP", "JPY", etc.
        StringView locale = ""
    );
}

// Usage:
uz::Localization::FormatCurrency(99.99, "USD", "en_US");   // "$99.99"
uz::Localization::FormatCurrency(99.99, "USD", "fr_FR");   // "99,99 $"
uz::Localization::FormatCurrency(99.99, "EUR", "de_DE");   // "99,99 €"
uz::Localization::FormatCurrency(1000, "JPY", "ja_JP");    // "¥1,000" (no decimals)
uz::Localization::FormatCurrency(99.99, "GBP", "en_GB");   // "£99.99"
```

## Date and Time Formatting

Format dates/times according to locale (order, separators, AM/PM vs 24h, month names).

### Format Patterns

ICU date patterns use a standard syntax:

```
y = year (yyyy = 4-digit)
M = month (MM = 2-digit, MMM = short name, MMMM = full name)
d = day (dd = 2-digit)
H = hour 24h (HH = 2-digit)
h = hour 12h (hh = 2-digit)
m = minute (mm = 2-digit)
s = second (ss = 2-digit)
z = timezone
a = AM/PM
```

Examples:

```
"MMM d, yyyy"      →  "Mar 12, 2026" (US English)
"d/M/yyyy"         →  "12/3/2026" (UK English)
"dd.MM.yyyy"       →  "12.03.2026" (German)
"yyyy年M月d日"     →  "2026年3月12日" (Japanese)
"HH:mm:ss"         →  "14:30:45" (24-hour)
"h:mm:ss a"        →  "2:30:45 PM" (12-hour US)
```

### Formatting API

```cpp
namespace uz::Localization {
    /// Format Unix timestamp according to pattern and locale.
    std::string FormatDateTime(
        uint64_t unix_timestamp,
        StringView pattern,      // "MMM d, yyyy" or "d/M/yyyy HH:mm"
        StringView locale = ""
    );

    /// Convenience: format as date only.
    std::string FormatDate(
        uint64_t unix_timestamp,
        StringView locale = ""   // uses locale's default date pattern
    );

    /// Convenience: format as time only.
    std::string FormatTime(
        uint64_t unix_timestamp,
        StringView locale = ""
    );
}

// Usage:
uint64_t now = std::chrono::system_clock::now().time_since_epoch().count() / 1000;

uz::Localization::FormatDate(now, "en_US");       // "Mar 12, 2026"
uz::Localization::FormatDate(now, "de_DE");       // "12.03.2026"
uz::Localization::FormatDate(now, "ja_JP");       // "2026年3月12日"

uz::Localization::FormatTime(now, "en_US");       // "2:30 PM"
uz::Localization::FormatTime(now, "de_DE");       // "14:30"

uz::Localization::FormatDateTime(now, "MMM d, yyyy h:mm a", "en_US");  // "Mar 12, 2026 2:30 PM"
```

## Composite Message Format

Combine plurals, gender, and numbers in a single string:

```csv
# game.uzloc — battle log
key,en,es
combat.log,"{actor} dealt {damage, plural, one {# damage} other {# total damage}} to {target, gender, select, male {him} female {her} other {them}}. {target}'s health: {health, number}","{actor} infligió {damage, plural, one {# punto de daño} other {# puntos de daño}} a {target, gender, select, male {él} female {ella} other {ellos}}. Salud de {target}: {health, number}"
```

Usage:

```cpp
std::map<std::string, std::string> args = {
    {"actor", "Warrior"},
    {"damage", "25"},
    {"target", "Goblin"},
    {"target_gender", "male"},
    {"health", "75"}
};
std::string log = uz::Localization::FormatMessage("combat.log", args);
// "Warrior dealt 25 total damage to him. Goblin's health: 75"
```

## Choice Format

Conditional formatting based on numeric ranges:

```csv
# game.uzloc
experience.level,"{level, choice, 0#Novice | 10#Apprentice | 50#Expert | 100#Master}"
```

Evaluation:

- `0#Novice` — if level >= 0 and < next threshold
- `10#Apprentice` — if level >= 10 and < 50
- `50#Expert` — if level >= 50 and < 100
- `100#Master` — if level >= 100

## Implementation

ICU is integrated via the system library (`icu-dev` on Linux, included in Windows SDK):

```cpp
// Simplified pseudocode
namespace uz::Localization {
    class MessageFormatter {
    public:
        std::string FormatMessage(
            const std::string& pattern,
            const std::map<std::string, std::string>& args,
            const icu::Locale& locale
        ) {
            icu::MessageFormat fmt(pattern, locale);
            icu::Formattable formattables[args.size()];
            // ... populate formattables from args
            UnicodeString result;
            icu::FieldPosition fp(0);
            fmt.format(formattables, args.size(), result, fp);
            return std::string(result.getBuffer(), result.length());
        }
    };
}
```

## Best Practices

1. **Use plurals for numeric values**: Any string with a number should consider plural forms.
2. **Separate gender from game logic**: Store character gender separately; pass as parameter when formatting.
3. **Test with all locales**: Plural/gender rules vary; test that messages format correctly in each locale.
4. **Avoid hardcoded numbers**: Use `FormatNumber()` instead of `std::to_string()` to respect locale.
5. **Document date patterns**: If using custom patterns, document them; let ICU defaults handle most cases.
6. **Cache formatters**: Message formatting is CPU-intensive; cache compiled MessageFormat objects per locale.

## Comparison

| Feature | Unreal 5 | Godot 4 | YAML i18n | **UzEngine** |
|---------|----------|---------|-----------|--------------|
| Plurals | `FTextFormat` with rules | None | Via ruby plugin | **ICU message format** |
| Gender | Manual strings | None | None | **ICU select** |
| Numbers | FText::AsNumber | Locale-aware | Via formatters | **ICU formatter** |
| Dates | FText::AsDate via locale | Built-in | Custom | **ICU pattern-based** |
| Currency | Limited | None | Custom | **ICU formatter** |
| Complexity | Low | High | Medium | **Medium** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | String tables (CSV), basic fallback |
| **Phase 3** | **ICU integration** (plurals, gender, number/date formatting), message format evaluation |
| **Phase 4** | Advanced choice format, collation, calendar systems |
