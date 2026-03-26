---
name: VFX Graph Asset Format Documentation Suite Created
description: Complete .uzvfx, .uzvfx_composite, Lua API, and hot-reload documentation (409-418 lines each)
type: observation
---

**Date**: 2026-03-24
**Task**: VFX system documentation task 3.62.0 §9
**Status**: COMPLETED

## Files Created (1,497 total lines)

### 1. 07-uzvfx-format.md (409 lines)
- **Purpose**: JSON format specification for single-emitter `.uzvfx` graphs
- **Content**:
  - Root fields (format, version, name, emitter, modules, parameters, events)
  - Emitter configuration (max_particles, lifespan_default)
  - Module types: spawn, update, turbulence, constraint, sprite_render, decay
  - Parameters system (float, vec3, vec4 with defaults and ranges)
  - Events system (emission_count, velocity_scale)
  - Validation rules (8 categories)
  - Complete fire campfire example
  - File location & naming conventions
- **Key Tables**: Module type fields (rate, spread_angle, gravity, drag, etc.), validation rules, example JSON
- **Cross-references**: Links to overview, modules, module API, Lua API, composite format

### 2. 08-uzvfx-composite-format.md (260 lines)
- **Purpose**: JSON format specification for multi-emitter `.uzvfx_composite` graphs
- **Content**:
  - Root fields (format, version, name, emitters)
  - Emitters array with name/graph/offset/delay fields
  - Delayed spawn mechanism with timing examples
  - Parameter and event forwarding to child emitters
  - Complete explosion composite example (5 emitters)
  - File location & naming
  - Validation rules (5 categories with referential integrity)
  - Interoperability with single-emitter effects
  - Memory and performance considerations
  - Best practices (delay for impact, reuse single-purpose effects)
  - Future extensions (hierarchical composites, parameter linking, physics coupling)
- **Key Tables**: Emitter reference fields, validation rules

### 3. 09-lua-vfx-api.md (418 lines)
- **Purpose**: Runtime Lua API reference for controlling VFX effects
- **Content**:
  - 6 main API functions:
    - `ctx.vfx:spawn(graph_path, position)` → entity
    - `ctx.vfx:set_parameter(entity, name, value)` → void
    - `ctx.vfx:emit_event(entity, name)` → void
    - `ctx.vfx:start(entity)` / `stop(entity)` / `pause(entity, paused)` → void
    - `ctx.vfx:get_alive_count(entity)` → integer
  - Each function with signature, parameters, returns, errors, examples
  - 7 practical examples:
    1. Simple fire effect
    2. Explosion with dynamic parameters
    3. Parameter oscillation (pulsing glow)
    4. Dynamic wind effect
    5. Event-driven bursts (weapon fire)
    6. Magic spell with color tinting
    7. Particle count monitoring
  - Error handling patterns (safe spawn, null-safe control)
  - Performance considerations
  - Threading & Lua sandbox notes
- **Key Examples**: All complete and runnable (not stubs)

### 4. 10-hot-reload.md (410 lines)
- **Purpose**: Dev workflow for iterative VFX editing
- **Content**:
  - Architecture overview (file watching, polling, reload, apply)
  - How hot-reload works (4 steps: monitoring, polling, reload/compile, apply)
  - Availability & gating (dev-only, conditional compilation, debug vs release)
  - Workflow: Edit → Save → See Changes (5-step user-facing guide)
  - VFXHotReload API with initialization example
  - Limitations (new files not auto-detected, shader recompilation overhead, no undo, parameter binding issues)
  - Troubleshooting (3 common problems with solutions)
  - Best practices (organize VFX, use external editor with auto-save, keep effects running, monitor frame rate, version control)
  - Future extensions (auto new file detection, parameter persistence, diff visualization, undo/redo)
- **Key Code**: C++ API example, engine integration pattern, conditional compilation
- **Key Tables**: Build type vs hot-reload availability

## Documentation Standards Met

✅ All files follow UzEngine documentation patterns:
- Frontmatter with status, format/API, references
- "Purpose" section first
- Architecture diagrams (in 10-hot-reload.md)
- Clear heading hierarchy (no level skips)
- Code examples with syntax highlighting (JSON, C++, Lua)
- Tables for structured data (field references, validation rules)
- Cross-references with relative links
- "See Also" sections linking between docs
- No duplicated content (uses cross-references instead)
- All files under 1500 lines (409-418 lines each)
- Single responsibility per file

## README.md Updated

Line 18-21: Added 4 new entries to VFX System documentation table:
- `07-uzvfx-format.md` — .uzvfx JSON format specification
- `08-uzvfx-composite-format.md` — .uzvfx_composite format
- `09-lua-vfx-api.md` — Lua VFX API reference
- `10-hot-reload.md` — VFX hot-reload workflow

## File Sizes

- 07-uzvfx-format.md: 15 KB, 409 lines
- 08-uzvfx-composite-format.md: 9.8 KB, 260 lines
- 09-lua-vfx-api.md: 12 KB, 418 lines
- 10-hot-reload.md: 12 KB, 410 lines
- **Total**: 48.8 KB, 1,497 lines

All well under limits.
