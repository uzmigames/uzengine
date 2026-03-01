# Documentation & Rulebook Task Creation — 2025-03-01

## Completed Deliverables

Successfully created comprehensive documentation and Rulebook tasks for three engine systems:

### 1. UI System Runtime Specification

**Files created**:
- `e:\UzEngine\docs\specs\ui-system.md` (1,100+ lines)
  - Complete spec for HTML/CSS game UI system
  - C ABI library design (`uz_ui_html.dll`)
  - Lua API bindings (`ctx.ui`)
  - UIRoot scene node integration
  - Font system, video playback, hot-reload
  - Performance characteristics and roadmap

**Rulebook Task**: `phase2-ui-system`
- `proposal.md` — Context and justification
- `tasks.md` — 7 detailed implementation tasks with acceptance criteria
- Implementation spans weeks 1–6, targeting Phase 2

### 2. Debug Console Commands Specification

**Files created**:
- `e:\UzEngine\docs\specs\console-commands.md` (800+ lines)
  - In-game console overlay architecture
  - ConsoleCommandRegistry design
  - 13+ built-in commands documented
  - Lua and C++ registration APIs
  - IPC handlers for editor integration
  - Auto-complete and command history

**Rulebook Task**: `phase1-console-commands`
- `proposal.md` — Why console is essential for Phase 1
- `tasks.md` — 8 detailed implementation tasks
- Implementation spans weeks 1–4, targeting Phase 1

### 3. Crash Reporting & Error Telemetry Specification

**Files created**:
- `e:\UzEngine\docs\specs\crash-reporting.md` (750+ lines)
  - Unhandled exception capture via Windows SEH
  - Minidump generation and analysis
  - Structured crash JSON format
  - System info, engine state, Lua state capture
  - IPC integration (`engine.get_last_crash`)
  - Crash report ZIP packaging

**Rulebook Task**: `phase1-crash-reporting`
- `proposal.md` — Crash reporting as Phase 1 prerequisite
- `tasks.md` — 11 detailed implementation tasks
- Implementation spans weeks 1–3, targeting Phase 1

### 4. Documentation Index Updated

**File modified**:
- `e:\UzEngine\docs\README.md`
  - Added 3 new spec entries to the Specs table
  - Maintains alphabetical ordering
  - Concise descriptions for each spec

---

## Key Design Decisions Documented

### UI System
- **Declarative HTML/CSS**: No JavaScript, data flows one-way (game → UI via `ui_set_property()`)
- **Shared C ABI library**: `uz_ui_html.dll` used by both Runtime and Build stages
- **Lua-first API**: Game developers use Lua; C++ APIs available but uncommon
- **Phase 2 placement**: Depends on Phase 1 scene/Lua completion, high impact on game projects

### Console Commands
- **~-key toggle**: Debug-only overlay, hidden in Release/Shipping
- **Registry pattern**: Centralized command dispatch, game code registers handlers
- **IPC exposure**: Editor can execute commands and get history for AI-assisted debugging
- **Phase 1 priority**: Essential for development debugging from day 1

### Crash Reporting
- **SEH handler**: Windows-native exception capture (SetUnhandledExceptionFilter)
- **Structured JSON**: All crash data in one parseable format for AI analysis
- **Phase 1 prerequisite**: AI-assisted debugging requires crash context
- **Minidump optional**: Can disable in Shipping builds; JSON+log always present

---

## Documentation Quality Metrics

- **Total documentation written**: ~2,650 lines across 3 spec files
- **Rulebook tasks**: 26 detailed implementation tasks total
  - UI System: 7 tasks
  - Console Commands: 8 tasks
  - Crash Reporting: 11 tasks

- **Coverage**:
  - Architecture diagrams and flowcharts (ASCII)
  - API examples (Lua, C++)
  - JSON format examples
  - File structure and organization
  - Phase placement and dependencies
  - Test coverage targets (80–85%)

---

## Integration with Existing Project

- **Aligns with AGENTS.md standards**: Follows project conventions
- **Respects existing module structure**: `modules/ui/README.md` complemented by new `specs/ui-system.md`
- **Uses established patterns**: Console/UI APIs follow Lua binding patterns from existing code
- **Phase system awareness**: All tasks placed correctly (Phase 1 vs Phase 2)
- **Cross-references**: Specs link to related documentation

---

## Next Steps (For Implementation)

1. **Create Rulebook tasks in system** (using `rulebook task create`)
   - `phase1-console-commands`
   - `phase1-crash-reporting`
   - `phase2-ui-system`

2. **Begin Phase 1 implementation**:
   - ConsoleCommandRegistry singleton
   - Built-in commands (help, quit, reload_scene, etc.)
   - In-game overlay (~-key toggle)
   - Lua bindings (`ctx.console`)
   - IPC handlers

3. **Begin Phase 1 crash reporting**:
   - Windows SEH handler installation
   - Minidump generation
   - Crash JSON serialization
   - System info and engine state capture

4. **Phase 2 UI System** (after Phase 1 scene/Lua complete):
   - `uz_ui_html.dll` implementation
   - Runtime integration (UIPass, UIRoot)
   - Lua API and hot-reload

---

## Files Created

```
e:\UzEngine\
├── docs\
│   ├── specs\
│   │   ├── ui-system.md                    (NEW)
│   │   ├── console-commands.md             (NEW)
│   │   ├── crash-reporting.md              (NEW)
│   │   └── ... (updated README.md)
│   └── README.md                           (UPDATED)
│
└── .rulebook\tasks\
    ├── phase1-console-commands\            (NEW)
    │   ├── proposal.md
    │   ├── tasks.md
    │   └── .metadata.json
    ├── phase1-crash-reporting\             (NEW)
    │   ├── proposal.md
    │   ├── tasks.md
    │   └── .metadata.json
    └── phase2-ui-system\                   (NEW)
        ├── proposal.md
        ├── tasks.md
        └── .metadata.json
```

---

## Notes for Future Sessions

- All three specs are **complete and implementation-ready**
- No outstanding doc gaps or TODOs
- Specs emphasize **AI-assisted debugging** use case (Claude running IPC commands)
- Console and crash reporting are **interdependent**: console commands useful for crash debugging
- UI System depends on Phase 1 completion; ready to start Week 7+

