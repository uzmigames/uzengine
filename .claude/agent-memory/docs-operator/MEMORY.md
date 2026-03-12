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

## Recent Work (2026-03-12) - FXC Dual-Compiler Documentation

### Documentation Updates for Milestone 0.2.1
- **Runtime CHANGELOG.md**: Added comprehensive 0.2.1 section (44 lines) detailing FXC dual-compiler implementation, shader model negotiation, RHI changes, SM5.0 support
- **Engine shader pipeline docs**:
  - `docs/engine/05-shader-pipeline/03-compilation.md`: Rewrote Purpose section (6→8 lines), added Supported Targets table with FXC, added DXC/FXC sections with code examples, added FXC implementation details, HLSL dual-path pattern section, Rules updated with 7 critical rules
  - `docs/engine/03-rhi/02-dx12-backend.md`: Added Shader Model Negotiation section (33 lines) covering NegotiateShaderModel(), force via UZ_FORCE_SM5=1, root signature adaptation table
- **User guide shader-pipeline.md**:
  - Compilation Targets section: Added FXC table row + explanation of dual-compiler system, UZ_FORCE_SM5 testing
  - New SM5.0 Shader Authoring section (100+ lines) with #ifdef UZ_BINDLESS pattern, dual-path example, compilation explanation, C++ detection, Input Assembler vertex format
- **Key patterns documented**:
  - Automatic compiler routing via RequiresDXC() in ShaderPipeline
  - #ifdef UZ_BINDLESS pattern for SM6.6 (bindless) vs SM5.0 (Input Assembler) code paths
  - FXC types defined inline in .h to avoid header conflicts
  - Cache differentiation: SM5.0 and SM6.6 shaders cached separately via ShaderTarget enum
  - TextRenderer/StatsOverlay accept bool bindless param for runtime adaptation

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

## Gap-Based Spec Updates (2026-03-02 Session 3)

### Updated Three Specs with Gap Findings:

#### 1. audio-system.md (530 → 600+ lines)
Added 5 new sections addressing audio gaps:
- **Audio Event System (Data-Driven)**: `.uzevent` assets decouple sound playback from game events
  - Supports random_no_repeat, sequential, weighted modes
  - `ctx.audio:trigger()`, `trigger_at()`, `trigger_attached()` API
- **Audio Occlusion and Obstruction**: Physics-based audio filtering via raycasts
  - LPF effects based on geometry occlusion
  - Smoothed over 100-200ms to avoid popping
- **DSP Effect Chains Per Bus**: XAPO effects per bus (reverb, EQ, limiter)
  - Configured in `.uzproj` TOML
  - Runtime control via `ctx.audio:set_bus_effect()`
- **Virtual Voices**: Voices virtualized when count exceeds max_voices (64)
  - Tracks position in CPU memory, resumes when priority voice ends
- **Adaptive Music System**: Multi-stem music tracks with runtime layer control
  - Quantized transitions (beat/bar/phrase)
  - `ctx.audio:play_music()`, `set_stem_active()`, `crossfade_music()`

#### 2. lua-scripting.md (620 → 700+ lines)
Added 5 new sections addressing Lua gaps:
- **Coroutines and Async Primitives**: Multi-frame sequences without state machines
  - `ctx:start_coroutine()`, `ctx.coroutine.wait()`, `wait_frames()`
  - Note: Runs in interpreter mode (~10x slower), avoid in hot paths
- **Module System**: `require()` for library code from `res://scripts/lib/`
  - Cached per VM, search path configurable in `.uzproj`
  - Dot notation for nested paths: `require("tween.ease")`
- **Shared State**: `ctx.shared` table backed by C++ GameState
  - Fires `GameStateChangedEvent` on changes
  - Used by UI binding and audio snapshot systems
- **Bytecode Cache**: `.luac` cached in `{cache_dir}/lua/{script_hash}.luac`
  - Invalidated on file mtime or content hash change
- **Rich Error Reporting**: Full stack traces with local variable snapshots
  - Sent via `lua.get_errors` IPC to VSCode diagnostics
- **Configurable Sandbox**: Three levels (strict, editor, trusted)
  - Set in `.uzproj` `[scripting]` section
- **Lua Test Framework**: Inline unit tests with `ctx.test` API
  - Run via `lua.run_tests` IPC, results in VSCode

#### 3. ui-system.md (800 → 900+ lines)
Added 5 new sections addressing UI gaps:
- **Gamepad Focus and Navigation**: FocusManager for D-pad navigation
  - Declarative: `data-focusable="true"`, `data-focus-default="true"`, `data-focus-right="id"`
  - C ABI: `ui_set_focus()`, `ui_get_focused_element()`
- **Widget Library**: 11 custom HTML elements with built-in behavior
  - `<uz-button>`, `<uz-slider>`, `<uz-checkbox>`, `<uz-dropdown>`, `<uz-scroll-view>`, etc.
  - All expose `--uz-*` CSS variables for theming
- **UI Animation and Tweening**: Lua API for programmatic animations
  - `ctx.ui:tween()` for single properties
  - `ctx.ui:sequence()` for multi-step animations
  - Easing curves (ease-out, ease-in, etc.)
- **UI State Machine**: `navigation_stack()` for menu hierarchy
  - `push()`, `pop()`, `replace()`, `clear()`
  - Restores focus on `pop()`
- **Bidirectional Data Binding**: Elements write to `ctx.shared`
  - `data-bind="audio.master_volume"` pattern
  - Triggers EventBus for system updates
- **C ABI Versioning**: Safeguards for forward compatibility
  - `struct_size` field in `UiEvent` for extensibility
  - Function name fix: `ui_unload_document` (not `ui_free_document`)
- **Localization Integration**: Direct l10n key support in HTML
  - `data-l10n-key="ui.menu.play"` pattern
  - Hot-reload updates all documents automatically
  - `ctx.ui:set_locale()` for runtime language switching

### Pattern Consistency Verified
- All 3 specs maintain:
  - Status badge at top: `> **Status**: Planned (Phase N)`
  - C++ bindings + Lua API together (both first-class)
  - Architecture diagrams with text descriptions
  - Configuration examples in TOML format
  - Phase roadmap at end
  - Related docs cross-references (relative paths)

## Spec Updates from Gap Analysis (2026-03-02 Session 4)

### Two Specs Updated Sequentially:

#### 1. renderer.md (188 → 315+ lines)
Added 6 new sections addressing rendering gaps from gap analysis:
- **Render Graph**: Core infrastructure for automatic barrier insertion, resource aliasing, composable post-processes
  - `RenderGraph` tracks frame resources, builds dependency DAG, validates pass order at compile time
  - Lambda-based pass execution for flexibility
  - Benefits: Automatic SRV→RTV transitions, transient texture aliasing, debug pass injection
- **Deferred Decal System**: Box-projection decals with blend modes (additive/modulate/override)
  - DBuffer integration after GBuffer fill
  - Up to 256 active decals per frame, sorted by screen-space area
- **Global Illumination (Phase 3)**: Hybrid GI approach
  - Static: Lightmap baking for static geometry
  - Dynamic: DDGI probe grid with quantized rays
  - Reflection: Captures + SSR contribution
  - Configuration via `.uzscene` INI (probe_spacing, probe_counts, rays_per_probe)
- **Bloom and Post-Processes**: Full post-process stack (Phase 2)
  - 6 effects: Bloom, Tone Mapping, Auto Exposure, Chromatic Aberration, Vignette, Color Grading
  - All are render graph passes, reorderable via `.uzproj` TOML
- **Physically Correct Light Attenuation Bug Fix**:
  - Old: `1/(d^2 + 1)` (distorts at short range)
  - New: `1/max(d^2, MIN_DIST^2)` (correct inverse square law)
  - Must be applied to `shaders/include/lighting.hlsli`
- **Documentation Error Correction**: Pipeline is Hybrid Deferred, not Forward+
  - GBuffer pass → deferred lighting → forward transparency

#### 2. asset-pipeline.md (378 → 530+ lines)
Added 5 new sections addressing asset pipeline gaps:
- **Source Asset Database**: SQLite tracking all source→output mappings
  - Schema: `assets` (id, source_path, output_path, importer, cook_key, last_cooked, status)
  - `dependencies` table for asset references
  - Enables incremental import, dependency tracking, asset search, budget reporting
- **Asset Dependency Graph**: Tracks which assets reference which others
  - Topological sort for cook order
  - On file change, marks dependents as `stale`
  - Example: PlayerMesh.fbx → PlayerSkin.png → all materials using that texture
- **Incremental Import — Fine-Grained**: CookKey = SHA256(source + settings + importer_version + engine_version)
  - Settings change → only that asset re-cooked
  - Importer upgrade → all assets of that type re-cooked
  - Unchanged assets never re-cooked regardless of mtime
- **Asset Redirectors**: Tombstone records for moved/renamed assets
  - SQLite `redirectors` table (old_path → new_path)
  - At load time, redirector table checked transparently
  - Stale redirectors reported in validation pass
- **Runtime Async Streaming**: Handle-based async load API
  - `AssetHandle<T>` with state tracking (Ready/Error/Pending)
  - Lua equivalent: `ctx.assets:load_async()` with coroutine wait
  - Configurable IO threads and priority levels in `.uzproj`
- **Cook Order and Cycle Detection**: Kahn's algorithm for topological sort
  - Detects cycles, breaks at alphabetically-first edge
  - Error logging for debugging

### Update Pattern Observed
- Both specs edited sequentially (read → single Edit call)
- New sections added at end of files
- Maintained consistent formatting with existing content
- Used code blocks for C++ API examples and SQL schemas
- Included configuration examples where applicable

## Three Rendering Documents Created (2026-03-02 Session 4 — Final)

### 1. Render Graph Spec (render-graph.md)
- **File**: `e:\UzEngine\docs\specs\render-graph.md` (950+ lines)
- **Status**: Planned (Phase 2)
- **Core Concept**: Frame-local DAG (directed acyclic graph) of rendering passes and resources
- **Key Components**:
  - **RgHandle/RgTexture/RgBuffer**: Virtual resource handles (not D3D12 resources)
  - **RgPass**: Rendering pass with read/write sets and lambda-based execution callback
  - **RenderGraph**: Container managing resources and passes; compile() + execute()
  - **Compile Phase**: DAG validation → topological sort → lifetime computation → aliasing → D3D12 allocation
  - **Execution Phase**: State transitions → pass callbacks → optional debug viz
- **Key Benefits**:
  - Automatic D3D12 barrier insertion at resource lifetime boundaries
  - Transient resource aliasing (saves 50-75% VRAM for overlapping lifetimes)
  - Composable post-processes (add/remove bloom/SSAO/SSR/TAA without shader rewrites)
  - Frame resource pooling with budget constraints
  - Debug visualization of intermediate textures via IPC
- **Performance**: Compile 1-5ms (negligible), execution zero overhead, aliasing scales with complexity
- **Test Coverage**: 5 unit tests (DAG validation, execution order, aliasing, budget), 1 integration test (bloom chain)

### 2. Global Illumination Spec (global-illumination.md)
- **File**: `e:\UzEngine\docs\specs\global-illumination.md` (900+ lines)
- **Status**: Planned (Phase 2–3)
- **Approach**: Hybrid GI (80% static, 20% dynamic)
- **Three Techniques**:
  - **Static Lightmaps**: Pre-baked texture atlases, offline baking, high quality
    - `.uzlightmap` binary format with texture + per-texel metadata
    - UV2 unwrapping required, non-overlapping UVs in [0,1]
    - GPU-accelerated path tracing baker (4+ bounces, 64+ samples/texel)
  - **DDGI Probes**: Real-time dynamic GI via irradiance sampling
    - DDGIVolume component (grid, probe spacing, ray count)
    - Each frame: ray dispatch → radiance sampling → irradiance accumulation (circular buffer)
    - Probe relocation for stuck geometry
    - 2.5ms/frame cost (128 rays, 256 probes)
  - **Reflection Captures**: Cubemap snapshots for specular reflections
    - CReflectionCapture component (position, influence radius)
    - Manual bake or auto-update, blends by distance
    - 0.5ms/frame for 2-3 captures
- **When to Use**: Lightmaps (static interiors), DDGI (dynamic scenes), captures (glossy surfaces)
- **Shader Integration**: Combined ambient = lightmap + DDGI diffuse + reflection capture specular
- **Test Coverage**: 3 unit tests (probe relocation, UV normalization, blending), 2 integration tests (probes update, captures affect glossy)

### 3. Rendering Guide (rendering-guide.md)
- **File**: `e:\UzEngine\docs\guides\rendering-guide.md` (1200+ lines)
- **Audience**: Artists and technical artists
- **Purpose**: Practical walkthrough for creating visually stunning scenes
- **Content**:
  - **Quick Start** (5 min): Minimal lit scene with ground, sphere, directional light
  - **PBR Materials**: Albedo/metallic/roughness/normal/AO/emissive
    - `.uzmat` INI format with texture references and parameter tweaks
    - 3 example materials: concrete (matte), brushed steel (anisotropic metal), glass (transparent)
    - Common mistakes: don't bake shadows in albedo, use linear for roughness/metallic
  - **Lighting**: Directional (sun), Point (lanterns), Spot (flashlights)
    - Shadow quality profiles (ultra/balanced/fast) with costs
    - Physically-based inverse-square falloff
    - Rotation via Euler angles (pitch/yaw)
  - **Post-Processing**: Bloom, SSAO, SSR, TAA, Tonemap (ACES)
    - Settings tables per quality level
    - Presets: neutral, cinematic, dark & moody, bright & airy
    - TAA temporal jitter, variance clamping, feedback
  - **Decals**: Bullet holes, splatters, paint marks
    - Projection-based, up to 50 active per scene
    - Decal materials with alpha blending
  - **Global Illumination**: Lightmaps (static), DDGI (dynamic), reflection captures (specular)
    - Workflow: mesh UV2 → lightmap bake (Phase 3) OR DDGI volume + play
    - Configuration examples in INI
  - **Advanced**: Parallax occlusion, emission, two-sided materials
  - **Performance Tips**: Draw call budget (1000-1500), texture memory (1-2GB), LOD, dynamic resolution scaling
  - **Debugging**: IPC commands (show_render_target, light_heatmap, shadow_cascades, ddgi_probes, gpu_frame_time)
  - **Complete Workflow**: 6-step scene setup from scratch
  - **FAQ**: Brightness, shadow artifacts, light leaks, flickering, banding

### README.md Updates (Sequential)
- Added `[specs/render-graph.md](specs/render-graph.md)` after screen-space-effects
- Added `[specs/global-illumination.md](specs/global-illumination.md)` after render-graph
- Added `[guides/rendering-guide.md](guides/rendering-guide.md)` after ai-development-guide

### Total New Documentation: ~3,050 Lines
- render-graph.md: 950 lines
- global-illumination.md: 900 lines
- rendering-guide.md: 1200 lines

### Consistency Maintained
- All specs: Status badge (Planned), Owner, Purpose, Thread Safety notes
- Architecture diagrams (ASCII + text), Core Types (C++ structs), Phase Roadmap, Related Docs
- All guides: Audience, Prerequisites, Quick Start, Examples, Performance Tips, FAQ
- Code examples: Proper syntax highlighting (```cpp, ```ini, ```hlsl, ```python)
- Cross-references: Relative paths to related docs

## Four Complementary Gameplay Documents Created (2026-03-02 Session 5)

### 1. gameplay-systems.md (Spec)
- **File**: `e:\UzEngine\docs\specs\gameplay-systems.md` (1100+ lines)
- **Status**: Planned (Phase 4)
- **Content Sections**:
  - **System Scheduler/TickGroups**: 6 execution phases (PrePhysics → Render → PostRender) with priority ordering, dependency declarations
  - **Gameplay Ability System (GAS)**: AbilitySpec, AbilityComponent, activation with cooldown/cost, ability tags, `.uzability` asset format
  - **Finite State Machines**: FSM<State, Event> template, enum-based states, transitions with guards, hot-reload support
  - **Behavior Trees**: Node types (Sequence/Selector/Parallel/Decorator/Leaf), `.uzbt` asset format, BlackboardComponent for data sharing
  - **Hot-Reload & Profiling**: Statistics API, IPC debug endpoints
  - **Test Coverage**: Unit + integration tests for all 4 systems
- **Key APIs**:
  - C++: `SystemScheduler`, `AbilitySystem`, `FSM<>`, `BehaviorTree`
  - Lua: `ctx.gameplay:*` (grant/revoke abilities, create FSM/BT, activate abilities)
- **Design Pattern**: Systems decoupled via registry context + EventBus

### 2. spatial-systems.md (Spec)
- **File**: `e:\UzEngine\docs\specs\spatial-systems.md` (1000+ lines)
- **Status**: Planned (Phase 4)
- **Content Sections**:
  - **Problem Statement**: O(n) queries vs O(log n) spatial indexing for 10k+ entities
  - **SpatialIndex Interface**: Generic API with multiple implementations
  - **BVH Implementation**: Top-down construction, SAH split heuristic, sphere/AABB/ray/frustum queries
  - **Octree Implementation**: Recursive subdivision, fixed depth, when to use vs BVH
  - **ECS Integration**: Reactive observer pattern on CTransform, batched updates
  - **Query API**: C++ low-level + Lua high-level (sphere/AABB/raycast/frustum)
  - **Physics Broad-Phase Integration**: Using BVH for collision candidate filtering
  - **Debug Visualization**: Editor wireframe of BVH/Octree structure
  - **Performance Characteristics**: Complexity table for both implementations
  - **Test Coverage**: BVH/Octree unit tests, spatial system integration tests
- **Lua API**: `ctx.world:query_sphere()`, `raycast()`, `query_aabb()`, `query_frustum()`

### 3. ecs-advanced.md (Engine Doc)
- **File**: `e:\UzEngine\docs\engine\ecs-advanced.md` (1200+ lines)
- **Status**: Reference (ongoing)
- **Content Sections**:
  - **Registry Context Pattern**: Service locator replacement for GameContext god object
  - **Reactive Systems**: on_construct, on_update, on_destroy signals, use cases
  - **Component Versioning**: __version field, migration functions for hot-reload
  - **Entity Templates/Archetypes**: `.uzentity` INI format, template instantiation API, overrides
  - **Prefab Deep Nesting**: Hierarchical prefabs, override propagation strategy
  - **Transform Dirty Flags**: Cached matrices, dirty flag propagation for efficient hierarchies
  - **Thread Safety**: Single-threaded registry, future multi-threading notes
  - **Best Practices**: DO/DON'T list (use context, avoid polling, cache refs)
  - **Testing**: Unit tests for registry, signals, versioning, dirty flags
- **Key Patterns**:
  - Service locator: `registry.ctx().emplace<System*>(this);`
  - Reactive: `registry.on_update<Component>().connect<&System::handler>(this);`
  - Versioning: `CComponentMigration::apply(old_version, comp);`

### 4. gameplay-programming-guide.md (Guide)
- **File**: `e:\UzEngine\docs\guides\gameplay-programming-guide.md` (1600+ lines)
- **Audience**: Game developers writing Lua scripts
- **Content Sections**:
  - **Script Anatomy**: on_start/on_update/on_stop pattern with self/dt/return table
  - **Coroutines**: Sequences without FSM, helpers (yield/yield_for/yield_until), cutscene example
  - **Finite State Machines**: Simple player FSM (Idle/Walk/Run/Jump/Dead), guards, global transitions
  - **Gameplay Ability System**: Grant abilities, basic attack, fireball AoE, dash with knockback
  - **Shared State (ctx.shared)**: Inter-script communication, decoupled systems
  - **Spatial Queries**: Finding enemies, line-of-sight raycasts, AABB overlaps
  - **Event Bus Patterns**: Custom events, combo system example
  - **6 Common Patterns**: Health/death, pickups, simple AI, character health, item pickups, enemy AI
  - **Best Practices**: Event-driven, query-based, caching, frame-rate independence, unsubscribe cleanup
  - **Performance Tips**: Query caching, allocation avoidance, batch operations
  - **Testing**: Unit + integration tests for gameplay patterns
- **Lua Examples**: 40+ code samples covering all patterns

### README.md Updates (Sequential)
1. Added `[engine/ecs-advanced.md]` to Engine C++ Documentation section (line 27)
2. Added `[specs/gameplay-systems.md]` and `[specs/spatial-systems.md]` to Specs section (after data-driven-config)
3. Added `[guides/gameplay-programming-guide.md]` to Guides section (after getting-started)

### Files Created: 4
- specs/gameplay-systems.md (1,100 lines)
- specs/spatial-systems.md (1,000 lines)
- engine/ecs-advanced.md (1,200 lines)
- guides/gameplay-programming-guide.md (1,600 lines)
- **Total**: 4,900 lines of new documentation

### Content Alignment
- All specs follow P4 status convention: `> **Status**: Planned (Phase 4)`
- All include Architecture section with C++ types and Lua APIs
- All include test coverage (unit + integration)
- All include Related Docs cross-references
- Guides follow narrative structure: anatomy → patterns → examples → best practices → testing
- Consistent with existing project style (ctx binding pattern, INI/TOML formats, etc.)

## Session 2026-03-03: Phase 1 Shared State Design Spec

### Shared State Spec (shared-state.md) - COMPLETED
- **File**: `/workspaces/UzEngine/docs/specs/scripting/shared-state.md` (783 lines)
- **Status**: Phase 1 design-only task, archived
- **Deliverable**: Comprehensive spec document for global game state system

#### Key Design Sections:
1. **C++ API** (GameState class):
   - `GameStateValue` = variant<bool, int32_t, float, string, Vec3>
   - `get<T>(key, default)`, `set<T>(key, value)`, `has()`, `remove()`, `clear()`
   - `keys()`, `get_all()`, `serialize()`, `deserialize()`
   - Deferred event dispatch: events queued during set(), dispatched at frame boundary

2. **Change Events**:
   - `GameStateChangedEvent<T>`: fires when value changes (different from old)
   - `GameStateRemovedEvent`: fires when key removed
   - Integrated with EventBus<T> for reactive systems

3. **Lua API** (ctx.shared):
   - Direct metatable access: `ctx.shared.health = 100`
   - Methods: `get()`, `has()`, `remove()`, `keys()`
   - Event subscription: `on_change(key, fn)` returns sub_id
   - Type inference: int vs float distinction preserved via Lua detection

4. **Serialization**:
   - msgpack format: flat key-value map
   - Integration point: `GameState::serialize` in save/load path
   - Persistent key filtering: `set_persistent(key, bool)` controls save inclusion
   - Deferred dispatch during deserialize (no event firing during load)

5. **Editor IPC Handlers**:
   - `scene.get_shared_state` → JSON dump of all entries
   - `scene.set_shared_state` → inject/update key at runtime
   - `scene.shared_state_changed` → event pushed to editor (throttled 10Hz)

6. **Thread Safety**:
   - Main-thread-only (Game Logic thread)
   - Assertions in debug builds for thread violations

#### Implementation Details:
- File structure: `src/game/game_state.h/cpp`, `src/game/lua_shared_state.h/cpp`
- Lua metatable: `__index` → get, `__newindex` → set
- Error handling: type mismatch warnings, corrupted load resilience
- Key naming convention: dot notation (player.health, quest_dragon.completed)

#### Test Coverage Specified:
- Unit tests: type preservation, events, serialization, Lua inference
- Integration tests: UI binding, save/load round-trip, hot-reload persistence
- Example tests provided (C++ GoogleTest format)

#### Phase Roadmap:
| Phase | Deliverable |
|-------|-------------|
| Phase 1 | GameState class, GameStateChangedEvent, ctx.shared proxy, deferred dispatch |
| Phase 2 | IPC handlers, inspector panel integration |
| Phase 3 | Serialization hooks, persistent flags, save-system integration |
| Phase 4 | Network replication, cloud save integration |

#### Deferred Items (Documented for Future Phases):
1. Network Replication (Phase 4)
2. Cloud Save Integration (Phase 4)
3. State Validation Schema (Phase 2)
4. Advanced Type Support - nested tables (Phase 4)
5. UI Binding Framework - data-bind syntax (Phase 2)
6. Audio Snapshot Triggers (Phase 3)

### Updates to Related Specs:
1. **event-bus.md**: Added two rows to predefined events table:
   - `GameStateChangedEvent<T>` | GameState | On shared state change (Phase 1+)
   - `GameStateRemovedEvent` | GameState | On key removal (Phase 1+)

### Related Docs Cross-References:
- lua-scripting.md (LuaJIT VM, ctx API, script lifecycle)
- event-bus.md (EventBus<T>, predefined engine events)
- save-system.md (save/load, msgpack format)
- ui-system.md (data binding to ctx.shared)
- audio-snapshots.md (audio state driven by shared state)

### Consistency Notes:
- Follows established spec format: Status badge, Owner, Thread safety, Serialization note at top
- C++ and Lua APIs presented as equal first-class APIs
- Architecture diagrams, implementation details, error handling table
- Phase roadmap and deferred items sections
- All code examples with proper syntax highlighting
- Cross-references use relative paths (../gameplay/event-bus.md)

### Task Completion:
- ✅ Created shared-state.md (783 lines)
- ✅ Updated event-bus.md with predefined events
- ✅ Marked all task items [x] in tasks.md
- ✅ Archived phase1-shared-state-design task
- ✅ No orphaned deferred items (all documented in spec)
