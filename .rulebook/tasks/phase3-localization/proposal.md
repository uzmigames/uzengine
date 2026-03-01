# Phase 3 — Localization / I18n System

## Context

The engine currently has no internationalization support. All game text is hard-coded in C++ and Lua. There is no way to build multilingual games or change language at runtime. This task implements a complete localization system: CSV-based string storage, LocaleManager singleton, runtime locale switching, and font fallback.

## Goal

A running `UzEngine.exe --play` that can:
- Load translation strings from CSV file (keyed by locale)
- Change language at runtime without restarting the game
- Support at least 6 languages: English, Portuguese (Brazil), Spanish, French, German, Japanese
- Fall back gracefully when translations are missing (show English or key itself)
- Load locale-specific fonts (e.g., CJK font for Japanese)
- Provide debug inspection of missing translations via IPC
- Support hot-reload of CSV in editor mode

## Spec Reference

[Localization Spec](../../../docs/specs/localization.md)

## Approach

1. Design CSV format (keyed, human-readable, AI-friendly)
2. Implement lightweight CSV parser (hand-written, no external dependency)
3. Implement `LocaleManager` singleton with in-memory translation table
4. Implement C++ API: `translate()`, `translatef()`, `set_locale()`, `get_missing_keys()`
5. Implement Lua API: `ctx.locale:t()`, `ctx.locale:tf()`, `ctx.locale:set()`
6. Integrate font fallback: per-locale font path from `.uzproj`
7. Add IPC handlers for editor inspection and export
8. Implement hot-reload of CSV in editor mode (FileWatcher integration)

## Out of Scope

- Plural forms (Phase 4)
- Right-to-left layout (Phase 4)
- Binary locale archives (Phase 4)
- Platform-specific locale detection (Windows default locale) — Phase 4

## Dependencies

- No external dependencies required (hand-written CSV parser)
- Uses existing FileWatcher infrastructure for hot-reload
- Font system must support loading per-locale fonts (existing in engine)
