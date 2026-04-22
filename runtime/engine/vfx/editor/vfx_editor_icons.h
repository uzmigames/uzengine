#pragma once

/// VFX Editor Icon Glyph Table — phase 13.2.0.
///
/// The `BitmapFont` bakes the default multi-range set via
/// `GetDefaultGlyphRanges()` (ASCII + Arrows + Box Drawing + Block
/// Elements + Geometric Shapes + Misc Symbols + Dingbats), so real
/// Unicode glyphs (▶ ❚ ■ ✓ ✕ ↺ ⋮ …) now render inline. Phase-12
/// ASCII compositions are preserved as fallbacks for the glyphs
/// the shipped font does not cover.
///
/// Each constant is a stable UTF-8 C string. Widgets consume them
/// as `text.AddText(font, VFX_ICON_PLAY, x, y, color)`; the
/// renderer decodes UTF-8 and looks up each codepoint in the
/// atlas. If a codepoint is not baked, the renderer falls back to
/// the replacement glyph `?`.
///
/// # Categories
/// - Transport: play, pause, stop, step-*, skip-*, restart.
/// - Navigation: chevrons, arrows.
/// - State: check, cross, plus, minus, dot, star, warning, info,
///   error.
/// - Module overrides: add, replace, revert, remove.
/// - Drag handles: grip, vertical dots.
/// - Module categories: one glyph per category for stack cards.
///
/// @ref docs/engine/25-vfx/53-editor-theme.md
/// @ref docs/engine/25-vfx/64-unicode-bitmap-font-ranges.md

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Transport
// ─────────────────────────────────────────────────────────────────────────────

/// Play button — U+25B6 (▶ BLACK RIGHT-POINTING TRIANGLE).
inline constexpr const char* VFX_ICON_PLAY       = "\xE2\x96\xB6";
/// Pause button — `||` fallback (U+275A HEAVY VERTICAL BAR rarely in mono fonts).
inline constexpr const char* VFX_ICON_PAUSE      = "||";
/// Stop button — U+25A0 (■ BLACK SQUARE).
inline constexpr const char* VFX_ICON_STOP       = "\xE2\x96\xA0";
/// Step one frame forward — U+23ED absent in Dingbats, use `>>` fallback.
inline constexpr const char* VFX_ICON_STEP_FWD   = ">>";
/// Step one frame backward — `<<` fallback (U+23EE absent).
inline constexpr const char* VFX_ICON_STEP_BACK  = "<<";
/// Skip to start — `|<` fallback.
inline constexpr const char* VFX_ICON_SKIP_START = "|<";
/// Skip to end — `>|` fallback.
inline constexpr const char* VFX_ICON_SKIP_END   = ">|";
/// Restart / cycle — U+21BA (↺ ANTICLOCKWISE OPEN CIRCLE ARROW).
inline constexpr const char* VFX_ICON_RESTART    = "\xE2\x86\xBA";
/// Loop toggle — U+21BB (↻ CLOCKWISE OPEN CIRCLE ARROW).
inline constexpr const char* VFX_ICON_LOOP       = "\xE2\x86\xBB";

// ─────────────────────────────────────────────────────────────────────────────
// Navigation
// ─────────────────────────────────────────────────────────────────────────────

/// Right chevron — U+25B8 (▸ BLACK RIGHT-POINTING SMALL TRIANGLE).
inline constexpr const char* VFX_ICON_CHEVRON_RIGHT = "\xE2\x96\xB8";
/// Down chevron — U+25BE (▾).
inline constexpr const char* VFX_ICON_CHEVRON_DOWN  = "\xE2\x96\xBE";
/// Left chevron — U+25C2 (◂).
inline constexpr const char* VFX_ICON_CHEVRON_LEFT  = "\xE2\x97\x82";
/// Up chevron — U+25B4 (▴).
inline constexpr const char* VFX_ICON_CHEVRON_UP    = "\xE2\x96\xB4";
/// Left arrow — U+2190 (←).
inline constexpr const char* VFX_ICON_ARROW_LEFT    = "\xE2\x86\x90";
/// Right arrow — U+2192 (→).
inline constexpr const char* VFX_ICON_ARROW_RIGHT   = "\xE2\x86\x92";
/// Up arrow — U+2191 (↑).
inline constexpr const char* VFX_ICON_ARROW_UP      = "\xE2\x86\x91";
/// Down arrow — U+2193 (↓).
inline constexpr const char* VFX_ICON_ARROW_DOWN    = "\xE2\x86\x93";

// ─────────────────────────────────────────────────────────────────────────────
// State
// ─────────────────────────────────────────────────────────────────────────────

/// Check mark — U+2713 (✓).
inline constexpr const char* VFX_ICON_CHECK    = "\xE2\x9C\x93";
/// Cross / close — U+2715 (✕).
inline constexpr const char* VFX_ICON_CROSS    = "\xE2\x9C\x95";
/// Plus — ASCII `+` (HEAVY variants rare in mono fonts).
inline constexpr const char* VFX_ICON_PLUS     = "+";
/// Minus — U+2212 (− MINUS SIGN, in Arrows+Math but not baked; fallback).
inline constexpr const char* VFX_ICON_MINUS    = "-";
/// Filled dot — U+25CF (●).
inline constexpr const char* VFX_ICON_DOT      = "\xE2\x97\x8F";
/// Star — U+2605 (★).
inline constexpr const char* VFX_ICON_STAR     = "\xE2\x98\x85";
/// Warning — U+26A0 (⚠).
inline constexpr const char* VFX_ICON_WARNING  = "\xE2\x9A\xA0";
/// Info — fallback lowercase `i` (no suitable glyph in baked ranges).
inline constexpr const char* VFX_ICON_INFO     = "i";
/// Error — U+2715 (✕ MULTIPLICATION X, light weight, broadly supported).
inline constexpr const char* VFX_ICON_ERROR    = "\xE2\x9C\x95";
/// Question — ASCII fallback.
inline constexpr const char* VFX_ICON_QUESTION = "?";

// ─────────────────────────────────────────────────────────────────────────────
// Module overrides (phase 11.11.0)
// ─────────────────────────────────────────────────────────────────────────────

/// Override: parent module is added in this emitter.
inline constexpr const char* VFX_ICON_OVERRIDE_ADD     = "+";
/// Override: parent value is replaced in this emitter.
inline constexpr const char* VFX_ICON_OVERRIDE_REPLACE = "~";
/// Override: can be reverted to parent.
inline constexpr const char* VFX_ICON_OVERRIDE_REVERT  = "<";
/// Override: module is explicitly removed.
inline constexpr const char* VFX_ICON_OVERRIDE_REMOVE  = "x";

// ─────────────────────────────────────────────────────────────────────────────
// Drag handles
// ─────────────────────────────────────────────────────────────────────────────

/// Vertical dots for drag handle — U+22EE (⋮ VERTICAL ELLIPSIS).
/// Math Operators range not baked; fallback to `:`.
inline constexpr const char* VFX_ICON_DRAG_DOTS = ":";
/// Six-dot grip — ASCII `::` fallback (U+2630 ☰ not always available).
inline constexpr const char* VFX_ICON_DRAG_GRIP = "::";

// ─────────────────────────────────────────────────────────────────────────────
// Menu / action affordances
// ─────────────────────────────────────────────────────────────────────────────

/// Three-dot overflow menu — ASCII fallback (U+2026 … is not in the
/// baked Box Drawing range; Misc Symbols covers nothing suitable).
inline constexpr const char* VFX_ICON_OVERFLOW   = "...";
/// Reset-to-default — U+21BA (↺ same as RESTART).
inline constexpr const char* VFX_ICON_RESET      = "\xE2\x86\xBA";
/// Bind to curve / expression — ASCII `~` fallback (chain-link glyphs
/// are all in Misc Symbols / Dingbats tail that Consolas lacks).
inline constexpr const char* VFX_ICON_BIND       = "~";
/// Delete row — U+2715 (✕).
inline constexpr const char* VFX_ICON_DELETE     = "\xE2\x9C\x95";
/// Duplicate row — `++` fallback.
inline constexpr const char* VFX_ICON_DUPLICATE  = "++";
/// Search input leading icon — ASCII `?` fallback (U+26B2 ⚲ rarely in fonts).
inline constexpr const char* VFX_ICON_SEARCH     = "?";
/// Lock — ASCII `#` fallback.
inline constexpr const char* VFX_ICON_LOCK       = "#";
/// Unlock — ASCII `~` fallback.
inline constexpr const char* VFX_ICON_UNLOCK     = "~";

// ─────────────────────────────────────────────────────────────────────────────
// Module categories — one glyph per category used in stack card icons
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr const char* VFX_ICON_CAT_LOCATION = "@";
inline constexpr const char* VFX_ICON_CAT_VELOCITY = ">";
inline constexpr const char* VFX_ICON_CAT_COLOR    = "#";
inline constexpr const char* VFX_ICON_CAT_FORCE    = "F";
inline constexpr const char* VFX_ICON_CAT_LIFETIME = "T";
inline constexpr const char* VFX_ICON_CAT_ROTATION = "R";
inline constexpr const char* VFX_ICON_CAT_SIZE     = "[]";
inline constexpr const char* VFX_ICON_CAT_SPAWN    = "*";
inline constexpr const char* VFX_ICON_CAT_RENDER   = "^";
inline constexpr const char* VFX_ICON_CAT_EVENT    = "!";
inline constexpr const char* VFX_ICON_CAT_GENERIC  = ".";

// ─────────────────────────────────────────────────────────────────────────────
// Compile-time glyph table (for availability testing)
// ─────────────────────────────────────────────────────────────────────────────

/// Aggregate list of every declared glyph — used by
/// `VfxEditorOverlay::Init` to verify all characters are present in
/// the bound font, and by `test_vfx_editor_theme.cpp` to assert
/// completeness.
struct VfxIconEntry {
    const char* name;
    const char* glyph;
};

/// Count of entries in `VfxAllIcons()`. Kept in sync with the
/// inline table below — any new icon above MUST add an entry here.
inline constexpr int kVfxIconCount = 48;

[[nodiscard]] inline const VfxIconEntry* VfxAllIcons() noexcept {
    static constexpr VfxIconEntry kTable[kVfxIconCount] = {
        {"PLAY",              VFX_ICON_PLAY},
        {"PAUSE",             VFX_ICON_PAUSE},
        {"STOP",              VFX_ICON_STOP},
        {"STEP_FWD",          VFX_ICON_STEP_FWD},
        {"STEP_BACK",         VFX_ICON_STEP_BACK},
        {"SKIP_START",        VFX_ICON_SKIP_START},
        {"SKIP_END",          VFX_ICON_SKIP_END},
        {"RESTART",           VFX_ICON_RESTART},
        {"LOOP",              VFX_ICON_LOOP},

        {"CHEVRON_RIGHT",     VFX_ICON_CHEVRON_RIGHT},
        {"CHEVRON_DOWN",      VFX_ICON_CHEVRON_DOWN},
        {"CHEVRON_LEFT",      VFX_ICON_CHEVRON_LEFT},
        {"CHEVRON_UP",        VFX_ICON_CHEVRON_UP},

        {"CHECK",             VFX_ICON_CHECK},
        {"CROSS",             VFX_ICON_CROSS},
        {"PLUS",              VFX_ICON_PLUS},
        {"MINUS",             VFX_ICON_MINUS},
        {"DOT",               VFX_ICON_DOT},
        {"STAR",              VFX_ICON_STAR},
        {"WARNING",           VFX_ICON_WARNING},
        {"INFO",              VFX_ICON_INFO},
        {"ERROR",             VFX_ICON_ERROR},
        {"QUESTION",          VFX_ICON_QUESTION},

        {"OVERRIDE_ADD",      VFX_ICON_OVERRIDE_ADD},
        {"OVERRIDE_REPLACE",  VFX_ICON_OVERRIDE_REPLACE},
        {"OVERRIDE_REVERT",   VFX_ICON_OVERRIDE_REVERT},
        {"OVERRIDE_REMOVE",   VFX_ICON_OVERRIDE_REMOVE},

        {"DRAG_DOTS",         VFX_ICON_DRAG_DOTS},
        {"DRAG_GRIP",         VFX_ICON_DRAG_GRIP},

        {"OVERFLOW",          VFX_ICON_OVERFLOW},
        {"RESET",             VFX_ICON_RESET},
        {"BIND",              VFX_ICON_BIND},
        {"DELETE",            VFX_ICON_DELETE},
        {"DUPLICATE",         VFX_ICON_DUPLICATE},
        {"SEARCH",            VFX_ICON_SEARCH},
        {"LOCK",              VFX_ICON_LOCK},
        {"UNLOCK",            VFX_ICON_UNLOCK},

        {"CAT_LOCATION",      VFX_ICON_CAT_LOCATION},
        {"CAT_VELOCITY",      VFX_ICON_CAT_VELOCITY},
        {"CAT_COLOR",         VFX_ICON_CAT_COLOR},
        {"CAT_FORCE",         VFX_ICON_CAT_FORCE},
        {"CAT_LIFETIME",      VFX_ICON_CAT_LIFETIME},
        {"CAT_ROTATION",      VFX_ICON_CAT_ROTATION},
        {"CAT_SIZE",          VFX_ICON_CAT_SIZE},
        {"CAT_SPAWN",         VFX_ICON_CAT_SPAWN},
        {"CAT_RENDER",        VFX_ICON_CAT_RENDER},
        {"CAT_EVENT",         VFX_ICON_CAT_EVENT},
        {"CAT_GENERIC",       VFX_ICON_CAT_GENERIC},
    };
    return kTable;
}

} // namespace uz
