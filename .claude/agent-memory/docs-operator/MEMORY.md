# UzEngine Documentation Operator Memory

## Project Structure
- Docs are in Markdown under `/docs/`
- Specs are in `/docs/specs/`
- Rulebook tasks in `.rulebook/tasks/phase*-*/` with structure: `proposal.md` + `tasks.md` + `.metadata.json`
- Main navigation: `/docs/README.md` (index of all docs)

## Documentation Standards
- All specs follow consistent format: Status section, Overview, Architecture, C++ API, Lua API, etc.
- Specs include cross-references to related docs using relative paths: `[link](../path/to/file.md)`
- Phase roadmap at end of each spec file
- All code examples use appropriate syntax highlighting (```c, ```cpp, ```lua, etc.)
- Use admonition format: `> **⚠️ Note**: text` for callouts
- Lua APIs always bind via `ctx.*` pattern (ctx.saves, ctx.locale, ctx.scene, etc.)

## Recent Work (2026-03-01)
Created two comprehensive Phase 3 systems with full documentation:

### 1. Save/Load System (save-system.md)
- **File**: `e:\UzEngine\docs\specs\save-system.md` (600+ lines)
- **Format**: Binary msgpack (runtime) + JSON (debug)
- **Slots**: 0-9 numbered + autosave (-1) with rotating buffer
- **Key APIs**: SaveManager, SaveWriter/SaveReader, Lua ctx.saves, IPC handlers
- **Features**: Thumbnail capture (320x180 JPEG), node transform serialization, Lua table persistence, autosave on scene transitions
- **Storage**: `%APPDATA%/UzEngine/<project>/saves/`
- **Fallback**: Graceful - missing keys use defaults, no crashes
- **Task**: `.rulebook/tasks/phase3-save-system/` with 12 sections, 60+ implementation items

### 2. Localization/I18n System (localization.md)
- **File**: `e:\UzEngine\docs\specs\localization.md` (550+ lines)
- **Format**: CSV (human-readable, AI-friendly) - can be single file or glob pattern
- **Locales**: Configurable in .uzproj (e.g., en, pt_br, es, fr, de, ja)
- **Key APIs**: LocaleManager, Lua ctx.locale, IPC handlers
- **Features**: Runtime language switching, font fallback per locale, CSV hot-reload in editor, 3-tier fallback
- **Parsing**: Hand-written UTF-8 CSV parser (handles escaping, comments, BOM)
- **Font Support**: Per-locale font override (crucial for CJK fonts)
- **Task**: `.rulebook/tasks/phase3-localization/` with 15 sections, 85+ implementation items

## Updates Made to README.md
1. Line 59: Added save-system.md to specs table
2. Line 60: Added localization.md to specs table
3. Line 120: Added phase3-save-system to Rulebook Tasks table
4. Line 121: Added phase3-localization to Rulebook Tasks table

## Key Design Patterns Used
- **Save System**: Lambda-based writer/reader pattern for flexibility
  - `write_slot(slot, [](SaveWriter& w) { w.write_value(...); })`
  - Supports node transforms, Lua tables, game-specific data

- **Localization**: CSV format with dot-notation keys
  - Keys: `ui.menu.play`, `game.objective`, `npc.dialog.greeting`
  - 3-tier fallback: current locale → English → key itself
  - Never crashes on missing translations

## Documentation Conventions Observed
- Spec status always at top: `> **Status**: Planned (Phase N)`
- Architecture diagrams as ASCII art with comments
- C++ and Lua APIs in parallel (both first-class)
- File format specs with exact struct definitions
- Configuration examples in TOML (project.uzproj format)
- IPC handlers table for editor-mode debugging
- Test coverage section (unit + integration)
- Performance notes and phase roadmap
- Related Docs cross-references (relative paths)

## Important Context for Future Sessions
- msgpack-c++ and zstd need vcpkg integration for save system
- CSV parser must handle UTF-8 with CJK characters
- Phase 4 defers: plural forms, RTL layout, binary locale archives, cloud saves
- Both systems designed for AI-driven development (plain text, hot-reload, graceful fallback)
- Lua APIs use ctx binding pattern established in lua-scripting.md
