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

### 0. Product Requirements Document (PRD.md)
- **File**: `e:\UzEngine\docs\PRD.md` (544 lines, comprehensive)
- **Purpose**: Executive summary, vision, problem statement, target users, product goals across all phases
- **Structure**: 12 sections including Executive Summary, Problem Statement (4 key problems + solutions), Target Users (3 personas), Product Goals (4 phases), Functional & Non-Functional Requirements (tabulated), Out of Scope, Technical Constraints, Success Metrics, and 38-task Appendix
- **Key Content**:
  - Vision: "First game engine designed from ground up for AI-assisted development"
  - Problem-Solution pairs addressing binary formats, hot-reload instability, editor crashes, opaque data
  - Differentiation table vs Unity/Unreal/Godot
  - Persona analysis: AI-augmented indie dev (primary), engine researcher (secondary), technical artist (tertiary)
  - 44 Functional Requirements (FR-001 through FR-090) with IDs, text, and rationale
  - 15 Non-Functional Requirements covering performance, stability, AI usability, maintainability
  - Complete success metrics for all phases
  - Appendix maps all 38 Rulebook tasks to phases, complexity, and specs
- **Updated**: README.md navigation section to link PRD as "Start here for requirements and goals"

### 1. Created two comprehensive Phase 3 systems with full documentation:

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

## Guides Expansion (2026-03-01 Session 2)

### Updated Existing Guides (4 files)
1. **getting-started.md** — Enhanced with:
   - LuaJIT 2.1 note in Prerequisites (auto via vcpkg)
   - Phase 0 status section (what works today vs planned)
   - "Your First Project" section with .uzscene INI + Lua script examples

2. **architecture.md** — Expanded with:
   - Module Placements table (Editor in VSCode, Runtime split uz_engine.lib + game.dll)
   - LuaJIT inside game.dll clarification, Jolt Physics in game.dll, XAudio2 in uz_engine.lib
   - Game Loop Data Flow diagram (ECS → Physics step → Audio → Render → Frame-limited)
   - Phase Progression table (Phase 0-4 module status)

3. **build.md** — Updated with:
   - vcpkg manifest mode explanation (reproducible builds, version lock)
   - Complete dependencies: LuaJIT 2.1, Jolt Physics 5.1, fastgltf, meshoptimizer, ispc_texcomp, msgpack-c, zstd
   - CMake presets section (--preset release, --preset test, --preset coverage)
   - UzBuild.exe section (offline asset compilation)
   - game.dll hot-reload section (rebuild while editor running)

4. **ipc-mcp.md** — Massively expanded with:
   - Lua namespace (Phase 1+): lua.eval, lua.reload_script, lua.get_errors, lua.get_globals
   - Audio namespace (Phase 2+): audio.play, audio.stop_all, audio.set_bus_volume
   - Physics namespace (Phase 3+): physics.debug_draw, physics.set_gravity, physics.ray_cast
   - Console namespace (Phase 2+): console.execute, console.get_commands, console.get_history
   - Saves namespace (Phase 3+): saves.list_slots, saves.delete_slot, saves.inspect_slot
   - Locale namespace (Phase 2+): locale.get_missing_keys, locale.set

### Created New Guides (3 files)

1. **lua-scripting-guide.md** (1300+ lines)
   - Lifecycle hooks: on_start, on_update, on_stop with self table persistence
   - Complete ctx API: scene, input, audio, events, saves, locale, console, vfx, nav, ui, logging
   - Math types: Vec3, Quat, Vec2 with full operations
   - Error handling: pcall for safety, Lua crash behavior (node disabled, engine continues)
   - Hot-reload mechanics with state preservation via ctx.saves
   - Timer system: schedule_after, schedule_every, cancel_timer
   - Event bus: subscribe/emit with built-in events (collisions, triggers, animations)
   - 5 common patterns: patrol AI, door trigger, health system, pickup, node interaction
   - Debugging via ctx.log, lua.get_errors IPC, lua.get_globals inspection
   - Performance tips: avoid allocations, cache node refs, batch operations

2. **physics-guide.md** (1000+ lines)
   - Jolt Physics 5.1 feature overview
   - Body modes: Static (fixed), Dynamic (gravity), Kinematic (script-controlled)
   - Collider shapes: Box, Sphere, Capsule, Cylinder, Convex mesh, Heightfield
   - Collision layers and matrix (control what collides)
   - Collision callbacks in Lua with contact data (position, normal, impact velocity)
   - Trigger volumes (non-physical detection zones)
   - Character controller API: move, jump, is_grounded
   - Raycasting and overlap queries (ray_cast, overlap_sphere, overlap_box)
   - Physics debug visualization + gravity override (editor IPC commands)
   - Performance tips: static for non-moving, primitive shapes, limit layers
   - 4 patterns: bullet raycast, explosion damage, door trigger, knockback effect

3. **asset-pipeline-guide.md** (1200+ lines)
   - res:// path system, .import/ metadata, .uzpak distribution
   - Supported formats: Mesh (glTF, FBX, OBJ), Texture (PNG, JPG, TGA, HDR), Audio (WAV, MP3, OGG)
   - Auto-import workflow: FileWatcher → import dialog → .import.json generated
   - Texture compression: BC7 (color), BC5 (normals), BC4 (grayscale), BC6H (HDR)
   - Mesh LODs: meshoptimizer generates 4 levels with screen coverage thresholds
   - Using assets in .uzscene: mesh, material, skeletal mesh references
   - Using assets in Lua: ctx.assets:load_mesh, load_material, load_audio
   - Asset cooking with UzBuild.exe (offline compilation)
   - Import settings reference: JSON configs for mesh, texture, audio
   - 3 patterns: importing character, terrain, IBL environment
   - Hot-reload in editor: file save → auto re-import → instant viewport update
   - Distribution: single .uzpak file shipping

### Total Documentation Delivered
- **Updated**: 4 guides with 4,500+ lines of modifications
- **Created**: 3 guides with 3,500+ lines of new content
- **Total**: 10 guides (7 original + 3 new) covering Phase 0-4 workflows

## Important Context for Future Sessions
- msgpack-c++ and zstd need vcpkg integration for save system
- CSV parser must handle UTF-8 with CJK characters
- Phase 4 defers: plural forms, RTL layout, binary locale archives, cloud saves
- Both systems designed for AI-driven development (plain text, hot-reload, graceful fallback)
- Lua APIs use ctx binding pattern established in lua-scripting.md
- All guides updated to reference LuaJIT, Jolt Physics, XAudio2 placement
- IPC command reference now covers all Phase 1-3 subsystems
- Asset pipeline guide provides complete hot-reload and packaging workflow
- Guides designed for reading ahead: developers can understand Phase 2-4 features before implementation

## Gap Analysis Consolidation (2026-03-02)

### Master Document Created
- **File**: `e:\UzEngine\docs\reviews\MASTER-GAP-ANALYSIS.md` (5,800+ lines)
- **Source**: Consolidated 7 specialist agent reviews (137 total gaps identified)
- **Key Sections**:
  1. Executive Summary: 137 gaps across 7 domains (28 P0 blockers, 72 P1 critical, 37 P2 important)
  2. Cross-Cutting Concerns: 7 features spanning multiple domains
  3. Consolidated Gap Table: All 137 gaps with priority, complexity, phase, rationale
  4. Architectural Decisions Before Phase 1: 8 critical design tasks
  5. Risk Assessment: Cost of delaying critical features
  6. Recommended Rulebook Tasks: 30+ new tasks for Phases 0-4
  7. Inconsistencies in Current Specs: Issues found in 9 spec files

### Gap Distribution
- ECS: 14 gaps (4 P0, 7 P1, 3 P2)
- Physics: 16 gaps (3 P0, 8 P1, 5 P2)
- Audio: 18 gaps (2 P0, 9 P1, 7 P2)
- Lua: 17 gaps (4 P0, 8 P1, 5 P2)
- Rendering: 28 gaps (7 P0, 14 P1, 7 P2)
- Assets: 24 gaps (5 P0, 13 P1, 6 P2)
- UI: 20 gaps (3 P0, 13 P1, 4 P2)

### P0 Architectural Blockers (Must Design Before Phase 1)
1. Render Graph (post-processes depend on it, retrofit cost 2-3 weeks)
2. Data-Driven Config Framework (audio events, physics materials, UI flows, entity templates)
3. Async Streaming Pipeline (coroutines, streaming priorities, task cancellation)
4. Unified Debug Infrastructure (DAP, profiler, hot-reload coordination)
5. Shared State System (audio snapshots, UI binding, save/load)
6. Spatial Partitioning for ECS (query perf O(n) → O(log n))
7. GPU Memory Budget System (VRAM tracking, virtual memory foundation)
8. Entity Hot-Reload Protocol (C++ component versioning, migration)

### README.md Update
- Added "Gap Analysis & Reviews" section after Guides with 8 review links
- Master doc positioned as "start here" for understanding missing features vs AAA standards
