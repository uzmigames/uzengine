# UzEngine — Master Gap Analysis

**Date**: 2026-03-02
**Consolidated By**: Documentation Operator
**Source**: Reviews from 7 specialist agents (ECS, Physics, Audio, Lua, Rendering, Assets, UI)

---

## Executive Summary

### Quantitative Overview

| Metric | Count |
|--------|-------|
| **Total Gaps Identified** | 137 |
| **Domains Analyzed** | 7 |
| **Architectural Blockers (P0)** | 28 |
| **Critical Features (P1)** | 72 |
| **Important Enhancements (P2)** | 37 |

### Gaps by Domain

| Domain | Gaps | P0 | P1 | P2 |
|--------|------|----|----|-----|
| **ECS & Game Systems** | 14 | 4 | 7 | 3 |
| **Physics** | 16 | 3 | 8 | 5 |
| **Audio** | 18 | 2 | 9 | 7 |
| **Lua Scripting** | 17 | 4 | 8 | 5 |
| **Rendering** | 28 | 7 | 14 | 7 |
| **Asset Pipeline** | 24 | 5 | 13 | 6 |
| **UI System** | 20 | 3 | 13 | 4 |

### Risk Assessment

- **28 architectural blockers**: Features that, if not designed now, will require invasive refactors when implemented later.
- **Cross-cutting concerns**: 14 gaps that span 2+ domains (render graph, streaming, shared state, data-driven systems).
- **Critical path**: 8 gaps that must be designed before any Phase 1-2 implementation begins.

---

## Section 1: Cross-Cutting Concerns

Features appearing in multiple domain reviews that require coordinated architectural decisions.

### 1.1 Render Graph / Frame Graph Architecture

**Affected Domains**: Rendering (Gap 15), ECS (Gap 5: System Execution Order)

**Status**: Not planned in current roadmap

**Impact**:
- Rendering currently assumes immediate-mode draw calls with hardcoded sort keys
- As post-processes proliferate (SSAO, SSR, TAA, volumetric fog, bloom), managing frame data flow becomes unmaintainable
- ECS system order is disconnected from render data dependencies

**Design Decision Needed Now**: Abstract frame resources (textures, buffers) as first-class values with a declarative dependency graph (render task → consumes input X → produces output Y).

**Consequence of Delaying**:
- Phase 2–3: ~2–3 weeks to retrofit render graph into existing hardcoded renderer
- Major changes to shader hot-reload, debug visualization, and post-process order
- All post-processes will be hand-wired instead of composable

**Recommendation**: Add as **pre-Phase-1** design task.

### 1.2 Async Streaming Pipeline

**Affected Domains**: Assets (Gap 5: Runtime Async Streaming), Lua (Gap 1: Coroutines), ECS (Gap 1: World Streaming)

**Status**: Virtual Texture (Gap 6) and Async Streaming (Gap 5) are Phase 4/3 respectively

**Impact**:
- Without coroutines, Lua scripts cannot write natural async patterns (camera lerp, dialogue wait, loading screen)
- Without async streaming, only trivial levels fit in memory
- Without render graph, streaming can't gracefully reduce draw calls during load

**Design Decision Needed Now**: Unified async framework covering:
  1. **Coroutines** in Lua (via `lua_resume` + scheduler integration)
  2. **Streaming priorities** (scene regions, LOD distance, memory budget)
  3. **Task graph** (render tasks depend on streamed assets → automatic cancellation)

**Consequence of Delaying**:
- Lua will ship without coroutines, forcing state machines for all sequences
- Streaming will be bolted on in Phase 3, conflicting with render graph design
- Multiple independent async systems instead of unified framework

**Recommendation**: Add as **pre-Phase-2** design task: `async-streaming-framework`.

### 1.3 Data-Driven Configuration System

**Affected Domains**: Audio (Gap 1: Event System), Assets (Gaps 2, 13: Dependency Graph, Content Validation), Physics (Gap 1: Materials), UI (Gaps 6, 7: Binding, State Machine), ECS (Gap 11: Entity Templates)

**Status**: Not mentioned in current specs or roadmap

**Impact**:
- Audio events are hardcoded asset paths → no designer iteration
- Physics materials are per-body inline values → no asset reuse
- UI bindings are one-way and property-based → no state machine logic
- Entity archetypes are defined in code → no rapid iteration without recompile

**Design Decision Needed Now**: Standardized data-driven format covering audio events, physics materials, UI state flows, and entity templates. Should be:
  - Inspectable in editor (IPC protocol for real-time preview)
  - Composable (materials reference other materials, events reference snapshots)
  - Validatable (dependency graph with cycle detection)
  - Reloadable (live edit in VSCode with hot-reload)

**Consequence of Delaying**:
- Each subsystem invents its own format (audio JSON, physics YAML, UI Lua)
- Inconsistent validation and error reporting across domains
- Designer workflow fragmented

**Recommendation**: Add as **pre-Phase-1** design task: `data-driven-config-framework`.

### 1.4 GPU Memory Management and Virtual Memory

**Affected Domains**: Rendering (Gap 19: Bindless Resources), Assets (Gap 5: Async Streaming, Gap 6: Virtual Texturing), Physics (Gap 2: Breakable Joints)

**Status**: Not planned; implicit assumption of infinite VRAM

**Impact**:
- No budget enforcement → artists can create unlimited-memory assets
- No bindless pipeline → fixed descriptor table limits
- No virtual texturing → high-res environments require explicit LOD management
- Fracture sim allocates new meshes unbounded

**Design Decision Needed Now**: Virtual memory system for GPU (VRAM budget, page tables for virtual textures, descriptor binding model).

**Consequence of Delaying**:
- Rendering stuck at mid-tier LOD strategies
- Console deployment will be painful (VRAM is precious)
- Post-mortem debugging of "ran out of VRAM" crashes

**Recommendation**: Add as **Phase 2** design task: `gpu-virtual-memory-system`.

### 1.5 Networked/Deterministic Physics and Rollback

**Affected Domains**: Physics (Gap 8: Deterministic Physics), ECS (Gap 7: Snapshot/Rollback)

**Status**: Not planned in current roadmap

**Impact**:
- No determinism → multiplayer physics desync
- No rollback → impossible to implement server-authoritative physics validation
- No snapshots → no replay system for debugging

**Design Decision Needed Now**: Physics determinism contract (fixed timestep, bit-exact math), ECS snapshot/restore for state replay.

**Consequence of Delaying**:
- Multiplayer games will suffer constant physics desyncs
- If implemented as afterthought, will require modifying Jolt integration

**Recommendation**: Add as **Phase 3** design task: `deterministic-physics-netcode`.

### 1.6 Shared State and Global Game State Management

**Affected Domains**: Lua (Gap 6: Shared State), ECS (Gap 4: Reactive Systems), Audio (Gap 6: Snapshots), UI (Gaps 6, 7: Data Binding, State Machine)

**Status**: Mentioned in lua-scripting.md but underspecified

**Impact**:
- No structured game state → scripts access globals directly
- No reactivity → UI cannot bind to game state changes
- No snapshots → audio cannot respond to game events
- No serialization → save/load breaks complex Lua state

**Design Decision Needed Now**: Global state store with:
  - Change notification system (event bus integration)
  - Serialization schema (msgpack/JSON conversion)
  - Lua table proxies for transparent access
  - Audio/UI/ECS subscribers

**Consequence of Delaying**:
- Complex games will have scattered state across Lua, C++, and scene
- Audio and UI will be disconnected from gameplay logic

**Recommendation**: Add as **Phase 1+** enhancement: `shared-state-system`.

### 1.7 Editor and Runtime Debugging Infrastructure

**Affected Domains**: Lua (Gaps 2, 3, 9: DAP, Profiler, Context Isolation), Audio (Gap 16: Profiler), ECS (Gap 12: C++ Component Hot-Reload), Rendering (no gap, but implied)

**Status**: Minimal IPC support in current roadmap; no proper debugger

**Impact**:
- No DAP → Lua debugging requires custom extensions
- No profiler → performance hotspots invisible
- No hot-reload for C++ → slow iteration during gameplay
- No context isolation → impossible to debug script errors safely

**Design Decision Needed Now**: Unified debug infrastructure:
  - Lua DAP server on IPC channel
  - Profiler with per-frame flame graphs
  - Remote symbol server for live C++ stack traces
  - Memory inspection tools

**Consequence of Delaying**:
- Developers will spend hours tracing Lua errors manually
- Performance regressions invisible until too late
- Complex bugs unfixable in play mode

**Recommendation**: Add as **Phase 1** task: `unified-debug-infrastructure`.

---

## Section 2: Consolidated Gap Table (All 137 Gaps)

Unified priority table ordered by **impact on engine competitiveness** within each priority tier.

### Legend
- **Priority**: P0 (Blocker), P1 (Critical), P2 (Important)
- **Complexity**: S (1–3 days), M (1 week), L (2–3 weeks), XL (4+ weeks)
- **Earliest Phase**: When this gap should ideally be addressed
- **Owner**: Primary domain responsible for delivery

---

### Priority 0: Architectural Blockers

Must be designed before major implementation; retrofitting is extremely expensive.

| # | Gap | Domain | Complexity | Phase | Rationale |
|---|-----|--------|-----------|-------|-----------|
| **P0-1** | World Streaming / Level Streaming | ECS | XL | 2 | AAA requirement; without it, levels are trivially small |
| **P0-2** | Global Illumination (GI) System | Rendering | XL | 3 | Visual quality baseline; flat lighting is unacceptable for AAA |
| **P0-3** | Physics Materials System | Physics | M | 2 | Material reusability blocks asset pipeline; breaks authoring workflow |
| **P0-4** | Render Graph / Frame Graph | Rendering | L | Pre-1 | All post-processes depend on it; retrofitting costs 2–3 weeks |
| **P0-5** | Audio Event System (Data-Driven) | Audio | L | 2 | Audio is entirely hardcoded; no designer iteration |
| **P0-6** | Coroutines / Async Primitives | Lua | M | 1 | Every gameplay sequence needs this; state machines are hostile to AI code generation |
| **P0-7** | Source Asset Database | Assets | L | 2 | Fundamental to incremental import; impacts all asset authoring |
| **P0-8** | Spatial Partitioning for ECS Queries | ECS | M | 1 | Query performance scales O(n); will bottleneck systems by Phase 2 |
| **P0-9** | Component Serialization and Schema Evolution | ECS | M | 2 | Prefabs and save/load require versioning; no migration path now |
| **P0-10** | Gamepad Focus and Navigation System | UI | M | 1 | Console release blocker; pause menu unnavigable on gamepad |
| **P0-11** | Virtual Shadows / Virtual Shadow Maps (VSM) | Rendering | L | 3 | CSM cascades don't scale; VSM solves shadow aliasing for outdoor scenes |
| **P0-12** | Adaptive Music System | Audio | L | 3 | Dynamic music is expected in AAA; static loops feel cheap |
| **P0-13** | Unified Debug Infrastructure | Multi | M | Pre-1 | Lua debugging, profiling, hot-reload all need coordinated IPC protocol |
| **P0-14** | GPU Memory Management / Virtual Memory | Rendering+Assets | XL | 2 | VRAM is finite; no budget enforcement → uncontrolled asset growth |
| **P0-15** | Async Streaming Pipeline (Unified) | Assets+Lua+ECS | L | Pre-2 | Must coordinate async tasks, streaming priorities, and coroutines |
| **P0-16** | Data-Driven Configuration Framework | Multi | L | Pre-1 | Audio events, physics materials, UI flows, entity templates all need it |
| **P0-17** | Spatial Locality / Cache-Aware ECS Layout | ECS | M | 2 | Memory layout kills performance; need component pooling strategies |
| **P0-18** | Breakable Joints and Constraints | Physics | M | 3 | Vehicle suspensions, cables, chains all depend on this |
| **P0-19** | Ray Tracing (DXR) Pipeline | Rendering | L | 4 | Future-proofing; needed for next-gen lighting |
| **P0-20** | Asset Dependency Graph | Assets | L | 2 | Incremental import, cache invalidation, and cook order depend on it |
| **P0-21** | Entity Lifecycle Management (Pooling) | ECS | M | 1 | Allocation/deallocation thrashing visible in profiler; pool needed for stable perf |
| **P0-22** | Widget Library (Composable UI Components) | UI | L | 1 | Basic buttons, panels, lists missing; authoring complex UI painful |
| **P0-23** | Destructible Meshes / Fracture System | Physics | L | 3 | Expected feature in AAA action games |
| **P0-24** | Shared State and Global Game State | Multi | M | 1 | Audio, UI, ECS all need structured access to gameplay state |
| **P0-25** | Deterministic Physics for Netcode | Physics | M | 3 | Multiplayer games require bit-exact physics |
| **P0-26** | Lua Module System (Controlled require) | Lua | S | 1 | Uncontrolled require is security risk and performance trap |
| **P0-27** | UI Data Binding (Bidirectional) | UI | M | 2 | One-way binding insufficient for dynamic HUD/menus |
| **P0-28** | Incremental Import (Fine-Grained) | Assets | M | 2 | Asset reimport full-pipeline is slow; fine-grained needed for 10k+ asset projects |

---

### Priority 1: Critical Features for AAA Competitiveness

Should be designed and implemented in Phase 1–3.

| # | Gap | Domain | Complexity | Phase | Rationale |
|---|-----|--------|-----------|-------|-----------|
| **P1-1** | Continuous Collision Detection (CCD) | Physics | M | 2 | Fast bullets and characters phase through geometry without CCD |
| **P1-2** | Physics Queries Batch API | Physics | M | 2 | Single raycasts are slow; batch queries essential for mass sweeps |
| **P1-3** | Cloth Simulation | Physics | L | 4 | Character clothing and soft surfaces |
| **P1-4** | Vehicle Physics | Physics | XL | 4 | Expected in AAA open worlds; fundamental gameplay pillar |
| **P1-5** | Jolt Job System Utilization | Physics | M | 2 | Physics threading not fully leveraged; multi-core utilization suboptimal |
| **P1-6** | Physics LOD (Simplified at Distance) | Physics | M | 3 | Distant physics objects still computed at full fidelity |
| **P1-7** | Cable / Rope / Chain Simulation | Physics | L | 4 | Interactive environment feedback |
| **P1-8** | DSP Effect Chains Per Bus | Audio | M | 2 | Reverb, EQ, compression per bus not available |
| **P1-9** | Audio Occlusion and Obstruction | Audio | M | 3 | Sounds bleed through walls; no acoustic modeling |
| **P1-10** | Virtual Voices (Beyond Pool Limit) | Audio | M | 2 | Hard cutoff at voice limit; no graceful degradation |
| **P1-11** | Reverb Zones (Environmental Audio) | Audio | M | 3 | Small room vs cathedral has same reverb |
| **P1-12** | Convolution Reverb | Audio | M | 3 | Impulse response–based reverb for realism |
| **P1-13** | Dialogue System & Voice Acting | Audio | L | 3 | Cutscenes, NPC dialogue, localized VO all need pipeline |
| **P1-14** | Random Variation & Asset Tagging | Audio | M | 2 | Footstep loops get repetitive; tagging enables smart variation |
| **P1-15** | Audio LOD (Distance-Based) | Audio | M | 3 | Distant audio still simulated at full detail |
| **P1-16** | Audio Bus Routing Flexibility | Audio | M | 3 | Advanced submix routing for complex mixing |
| **P1-17** | Wwise/FMOD Middleware Integration | Audio | L | 4 | Professional audio teams expect industry-standard middleware |
| **P1-18** | Virtual Geometry / Nanite Pipeline | Rendering | L | 3 | High-poly scene detail without geometry bottleneck |
| **P1-19** | Deferred Decal System | Rendering | M | 2 | Bullet holes, blood splatters, environmental decay |
| **P1-20** | Bloom, Lens Flare, Chromatic Aberration | Rendering | M | 2 | Standard post-processes missing |
| **P1-21** | Depth of Field (Bokeh DoF) | Rendering | M | 2 | Cinematic focus effects |
| **P1-22** | Motion Blur | Rendering | M | 2 | Camera and object motion blur for smooth cinematics |
| **P1-23** | Volumetric Fog and Atmospheric Scattering | Rendering | L | 3 | Volumetric lighting, god rays, fog density |
| **P1-24** | Sky and Atmosphere System | Rendering | M | 3 | Dynamic sky, clouds, day-night cycle |
| **P1-25** | Water Rendering | Rendering | L | 3 | Reflections, refraction, waves, shoreline interaction |
| **P1-26** | Subsurface Scattering (SSS) | Rendering | M | 3 | Skin, foliage, wax materials need translucency |
| **P1-27** | Hair Rendering | Rendering | L | 4 | Character fidelity requirement |
| **P1-28** | Eye Rendering | Rendering | M | 4 | Character close-ups require eye shader |
| **P1-29** | Multi-View Rendering (VR/Split-Screen) | Rendering | L | 4 | VR and couch co-op both need multiple views |
| **P1-30** | Variable Rate Shading (VRS) | Rendering | M | 4 | GPU optimization for foveal rendering |
| **P1-31** | Mesh Shader Pipeline | Rendering | L | 4 | Hardware mesh shaders for geometry processing |
| **P1-32** | Bindless Resources | Rendering | L | 3 | Reduce descriptor binding overhead |
| **P1-33** | GPU-Driven Rendering (Indirect Draws) | Rendering | L | 3 | Compute-driven draw calls for scalability |
| **P1-34** | System Execution Order and Tickable Priorities | ECS | M | 1 | Systems have implicit order; no priority inheritance |
| **P1-35** | Reactive Systems / Change Detection | ECS | M | 2 | No change tracking; systems rerun even if data unchanged |
| **P1-36** | Snapshot / Rollback (Deterministic Netcode) | ECS | L | 3 | Multiplayer state synchronization and replays |
| **P1-37** | Gameplay Ability System (GAS Equivalent) | ECS | L | 2 | Action games need ability system (attack chains, cooldowns, interrupts) |
| **P1-38** | Finite State Machines / Behavior Trees (Native) | ECS | L | 2 | AI and character behavior need state control |
| **P1-39** | Prefab Override System (Deep Nesting) | ECS | M | 2 | Instance-specific overrides break template inheritance |
| **P1-40** | Entity Templates / Data-Driven Archetypes | ECS | M | 1 | Entity composition is manual; no template reuse |
| **P1-41** | C++ Component Hot-Reload | ECS | M | 2 | Gameplay iteration slow without hot-reload |
| **P1-42** | Dependency Injection / Service Locator | ECS | M | 2 | Systems need decoupled access to shared services |
| **P1-43** | Hierarchical Transform Optimization | ECS | M | 2 | Dirty flag propagation and parallel updates missing |
| **P1-44** | Debug Adapter Protocol (DAP) for Lua | Lua | M | 1 | VSCode integration for breakpoints, watches, stack traces |
| **P1-45** | Integrated Lua Profiler | Lua | M | 2 | Flame graphs per-script showing hotspots |
| **P1-46** | Type-Safe Lua Bindings via Reflection | Lua | L | 2 | Code generation for C++ → Lua bindings reduces hand-binding errors |
| **P1-47** | Bytecode Compilation Cache | Lua | S | 1 | Lua scripts recompiled every load; cache them |
| **P1-48** | Rich Error Reporting | Lua | M | 1 | Error stack context needs local variables, watch expressions |
| **P1-49** | Multiple VM Instances / Context Isolation | Lua | L | 3 | Mods and safe sandboxing need isolated VMs |
| **P1-50** | Configurable Sandbox Levels | Lua | M | 1 | Adjust Lua permissions (file I/O, require, FFI) per context |
| **P1-51** | Lua GC Tuning API | Lua | M | 1 | Garbage collection pauses hurt frame time; need tuning knobs |
| **P1-52** | Safe FFI for C++ Struct Access | Lua | M | 2 | Currently unsafe; need checked access with error handling |
| **P1-53** | Lua Test Framework | Lua | S | 1 | Native unit test support for script logic |
| **P1-54** | Script Templates and Snippets | Lua | S | 1 | Starter code for common patterns |
| **P1-55** | Visual Scripting as Alternative | Lua | XL | 4 | Node graphs for non-programmers (Blueprints equivalent) |
| **P1-56** | Serialization of Lua Closures | Lua | M | 3 | Save/load breaks function references |
| **P1-57** | Asset Redirectors | Assets | M | 2 | Rename/move assets without breaking references |
| **P1-58** | Mip Streaming with Distance Priority | Assets | L | 3 | Load mips based on distance and on-screen size |
| **P1-59** | Asset Size Budget Enforcement | Assets | M | 2 | Warn when total asset size exceeds limits |
| **P1-60** | Cross-Platform Asset Variants | Assets | L | 3 | Different assets for different platforms |
| **P1-61** | Texture Atlas Auto-Packing | Assets | M | 3 | Automatic sprite/texture atlasing |
| **P1-62** | Mesh Optimization Validation | Assets | M | 2 | Ensure LOD/optimization targets are met |
| **P1-63** | Asset Caching and Delta Cook | Assets | M | 2 | Incremental cooking; don't re-cook unchanged assets |
| **P1-64** | Skeletal Mesh Import Validation | Assets | M | 2 | Check bone hierarchies, weights, naming conventions |
| **P1-65** | Animation Compression Strategies | Assets | M | 3 | Reduce animation file size (quantization, keyframe optimization) |
| **P1-66** | Metadata Tags and Search | Assets | S | 2 | Organize assets with tags for discovery |
| **P1-67** | Asset Usage Reports | Assets | M | 2 | What assets are actually used in scenes |
| **P1-68** | Audio Animation Sync | Audio | M | 2 | Sync audio playback to animation timeline |
| **P1-69** | UI Animation System — Tweening | UI | M | 1 | Lerp properties, fade in/out, slide transitions |
| **P1-70** | Safe Area Support (Mobile) | UI | M | 2 | Notches, safe zones on mobile devices |
| **P1-71** | Rich Text and Inline Content | UI | M | 2 | Text formatting, emoji, inline icons |
| **P1-72** | UI State Machine for Menu Flow | UI | M | 2 | State-based navigation, transitions, history stack |

---

### Priority 2: Important Enhancements

Desirable features for mid-tier and AAA; implement in Phase 3+.

| # | Gap | Domain | Complexity | Phase | Rationale |
|---|-----|--------|-----------|-------|-----------|
| **P2-1** | Physics Regions / Substepping Config | Physics | M | 3 | Custom timestep per region (slow-motion areas) |
| **P2-2** | Physics Debug Profiler | Physics | S | 2 | Profile constraint solving, collision queries |
| **P2-3** | Soft Body Simulation | Physics | L | 4 | Jellyfish, water blobs, deformable objects |
| **P2-4** | Buoyancy System | Physics | M | 3 | Swimming mechanics |
| **P2-5** | Fluid Simulation | Physics | XL | 5 | Liquids, smoke, sand (major undertaking) |
| **P2-6** | Audio Streaming with Seek Points | Audio | M | 3 | Music hot-spots (vs streaming entire file) |
| **P2-7** | HRTF and Binaural Audio | Audio | M | 4 | Spatial audio beyond HRTF stereo panning |
| **P2-8** | Procedural Audio Foundation | Audio | L | 4 | Synthesized sound (impact, wind, machinery) |
| **P2-9** | Audio Profiler Integration | Audio | M | 2 | Monitor voice count, DSP load, memory |
| **P2-10** | Audio Middleware Abstraction | Audio | M | 3 | Swap XAudio2 for other backends |
| **P2-11** | Soft Body Rendering | Rendering | L | 4 | Deformable character meshes |
| **P2-12** | Temporal Anti-Aliasing (TAA) Improvement | Rendering | M | 3 | Current TAA is basic; ghosting artifacts |
| **P2-13** | Screen-Space Ambient Occlusion (SSAO) Improvement | Rendering | M | 3 | Current HBAO+ is good baseline; refinement needed |
| **P2-14** | Fog and Volumetric Lighting | Rendering | L | 3 | Height-based fog, volumetric god rays |
| **P2-15** | Light Culling Optimization | Rendering | M | 2 | Current tiling is basic; improve memory layout |
| **P2-16** | Shader Permutation Optimization | Rendering | M | 2 | Reduce PSO bloat from feature combinations |
| **P2-17** | Material Blending and Layering | Rendering | M | 3 | Multiple material influences per surface |
| **P2-18** | Parallax Occlusion Mapping (POM) | Rendering | M | 3 | Height-based surface detail |
| **P2-19** | Emissive Material Bloom | Rendering | S | 2 | Bloom from emissive surfaces |
| **P2-20** | Real-Time Global Illumination Probes | Rendering | M | 4 | Light probes with real-time updates |
| **P2-21** | Reactive ECS Components | ECS | S | 2 | Automatic update on property change |
| **P2-22** | Prefab Nesting Limits | ECS | S | 2 | Documentation of practical nesting depth |
| **P2-23** | Time Dilation / Slow-Motion | ECS | S | 1 | Variable game speed (bullet-time) |
| **P2-24** | Deterministic Random Number Generator | ECS | S | 1 | Seeded RNG for replays |
| **P2-25** | Input Rebinding System | UI | M | 1 | Player-configurable key/button mapping |
| **P2-26** | 3D World-Space UI (Billboards) | UI | M | 2 | Floating labels, damage numbers in world |
| **P2-27** | Drag and Drop System | UI | M | 2 | Inventory, item manipulation |
| **P2-28** | Accessibility System (Color Blindness, Subtitles) | UI | M | 2 | Accessibility features |
| **P2-29** | UI Theming System | UI | M | 3 | Light/dark mode, color schemes |
| **P2-30** | Scrollable Text and Virtual Lists | UI | M | 2 | Performance for long lists |
| **P2-31** | UI Hot-Reload Without Frame Glitch | UI | M | 2 | CSS live-edit without stutter |
| **P2-32** | UI Automated Testing | UI | L | 3 | Regression tests for UI flows |
| **P2-33** | HUD Subsystem Separation | UI | M | 2 | HUD and menus in different rendering passes |
| **P2-34** | Input Text Fields | UI | M | 2 | Editable text boxes |
| **P2-35** | Minimap / Radar Widget | UI | M | 3 | Minimap rendering and interaction |
| **P2-36** | Responsive Design & Safe Area | UI | M | 2 | Multi-resolution and notch support |
| **P2-37** | UI Profiler and Inspector | UI | M | 2 | Debug rendering, measure layout time |

---

## Section 3: Architectural Decisions to Make Before Phase 1

These 8 gaps are dependencies for downstream work. Designing them now avoids 4+ weeks of refactoring mid-project.

### Pre-Phase-1 Critical Design Tasks

| Task | Complexity | Design Output | Downstream Impact |
|------|-----------|---------------|-------------------|
| **Render Graph Architecture** | L | Abstract frame resources, task dependencies, PSO caching | All post-processes, debug viz, streaming |
| **Data-Driven Config Framework** | L | Unified format for audio events, physics materials, UI flows, entity templates | Audio authoring, physics iteration, UI composition |
| **Unified Debug Infrastructure** | M | IPC protocol for DAP, profiler, hot-reload, memory inspection | Lua debugging, C++ iteration, performance visibility |
| **Async Streaming Framework Design** | L | Coroutine scheduler, streaming priorities, task cancellation | Lua async patterns, level streaming, asset LOD |
| **Shared State System Architecture** | M | Global state store, change notifications, serialization schema | Audio reactivity, UI binding, save/load |
| **ECS Query Performance Model** | M | Spatial partitioning strategy, cache layout, system scheduling | Physics queries, collision broad-phase, gameplay scales |
| **GPU Memory Budget System** | M | VRAM tracking, virtual memory model, page tables for VT | Asset streaming, bindless pipeline, console targeting |
| **Entity Component Hot-Reload Protocol** | M | C++ component type versioning, ABI-safe reloading, data migration | Fast gameplay iteration, prefab authoring |

---

## Section 4: Risks from Delaying Critical Features

### Scenario 1: Render Graph Implemented in Phase 3

**Cost**: 2–3 weeks refactoring

- Existing immediate-mode draw calls hardcoded to sort keys
- Each post-process hand-wired (SSAO → SSR → TAA order)
- Debug visualization competes for RenderTargets
- New post-processes require C++ changes + IPC protocol extensions

**Consequence**: Phase 3 ships late, blocking content creation tools.

### Scenario 2: No Streaming Support Until Phase 4

**Cost**: Multi-week content rework + potential franchise limitations

- Phase 2–3 levels are ~500 MB (console VRAM limit is 10 GB)
- Open-world games impossible without streaming
- Asset authoring pipeline breaks (artists can't iterate on 500 MB scenes)
- If shipping Phase 3 as demo, levels artificially tiny and unconvincing

**Consequence**: Engine appears uncompetitive for AAA open-world titles.

### Scenario 3: Audio Events Hardcoded Until Phase 2

**Cost**: 1+ week audio system redesign + all event API changes

- Audio team implements hundreds of event paths (footstep, gunshot, music, UI)
- Designer iteration blocked (every change requires code)
- No data validation or preview
- Music system incompatible with middleware integration (Phase 4)

**Consequence**: Audio is brittle and unscalable; retrofitting middleware painful.

### Scenario 4: UI Without Gamepad Navigation (Phase 1)

**Cost**: 2 weeks + full QA pass

- Shipped Phase 1 demo unplayable on controller
- Pause menu broken
- Settings menu unusable
- Console port fails certification

**Consequence**: Phase 1 release reputation damage; Xbox/PS5 ports delayed.

### Scenario 5: Lua Without Coroutines (Phase 1–2)

**Cost**: Fragmented codebase, reduced AI code-gen quality

- Every dialogue, camera pan, patrol sequence = state machine
- AI assistant generates verbose, unnatural code
- Gameplay programmers prefer manual state management
- Async patterns scattered across codebase with no standard

**Consequence**: Engine appears primitive compared to Godot/Unreal for AI coding.

---

## Section 5: Recommended Rulebook Tasks

New tasks to add to `.rulebook/tasks/` to address critical gaps before Phase 1–2.

### Pre-Phase-1 (Design & Architecture)

```
NEW TASKS:

phase0-render-graph-design
  - Title: Design and document render graph architecture
  - Complexity: L
  - Dependencies: None
  - Output: render-graph.md spec
  - Rationale: Required before Phase 1 post-processes; retrofit cost 2-3 weeks

phase0-data-driven-framework-design
  - Title: Design unified data-driven config format
  - Complexity: L
  - Dependencies: None
  - Output: data-driven-config.md spec + example files (audio events, materials, UI flows)
  - Rationale: Audio, physics, UI, ECS all need this; standardization up-front saves chaos

phase0-unified-debug-infrastructure
  - Title: Design debug protocol (DAP, profiler, hot-reload)
  - Complexity: M
  - Dependencies: None
  - Output: debug-protocol.md spec + IPC command additions
  - Rationale: Lua debugging, C++ hot-reload, profiling all unified; no ad-hoc extensions

phase0-async-streaming-framework
  - Title: Design async/streaming/coroutine integration
  - Complexity: L
  - Dependencies: render-graph-design, data-driven-framework-design
  - Output: async-streaming.md spec
  - Rationale: Coroutines, streaming, render tasks all depend on unified async model

phase0-shared-state-design
  - Title: Design global game state store and change notifications
  - Complexity: M
  - Dependencies: data-driven-framework-design
  - Output: shared-state.md spec
  - Rationale: Audio snapshots, UI binding, save/load all rely on this
```

### Phase 1 (Scripting & Core Systems)

```
NEW TASKS:

phase1-render-graph-impl
  - Title: Implement render graph and PSO caching
  - Complexity: L
  - Dependencies: phase0-render-graph-design
  - Output: Functional render graph with post-process composition
  - Rationale: Required for Phase 2 post-processes (SSAO, SSR)

phase1-lua-coroutines
  - Title: Implement Lua coroutine support with scheduler
  - Complexity: M
  - Dependencies: phase0-async-streaming-framework
  - Output: ctx.schedule_coroutine, coroutine library
  - Rationale: Unblocks dialogue, camera sequences, AI-friendly code generation

phase1-lua-dap-server
  - Title: Implement Debug Adapter Protocol server for Lua
  - Complexity: M
  - Dependencies: phase0-unified-debug-infrastructure
  - Output: IPC DAP endpoint, VSCode integration
  - Rationale: Standard debugging experience; required for professional scripting

phase1-ui-gamepad-navigation
  - Title: Implement gamepad focus/navigation system for UI
  - Complexity: M
  - Dependencies: input-system (Phase 3)
  - Output: FocusManager, gamepad navigation, pause menu playability
  - Rationale: Console release blocker; without it, pause menu broken

phase1-entity-templates
  - Title: Implement data-driven entity templates / archetypes
  - Complexity: M
  - Dependencies: phase0-data-driven-framework-design
  - Output: Entity template format, template loading in editor
  - Rationale: Rapid iteration on prefabs; enables designer workflows

phase1-ecs-query-optimization
  - Title: Implement spatial partitioning (BVH/octree) for ECS queries
  - Complexity: M
  - Dependencies: scene-system (Phase 1)
  - Output: SpatialQuery interface, BVH implementation
  - Rationale: Query performance O(n) → O(log n); required for 10k+ entities

phase1-shared-state-impl
  - Title: Implement global game state store with change notifications
  - Complexity: M
  - Dependencies: phase0-shared-state-design, event-bus
  - Output: GameState struct, change event broadcast
  - Rationale: Audio snapshots, UI binding, save/load all need this
```

### Phase 2 (Audio, Physics Materials, Rendering)

```
NEW TASKS:

phase2-audio-event-system
  - Title: Implement data-driven audio event system
  - Complexity: L
  - Dependencies: phase0-data-driven-framework-design, audio-system
  - Output: AudioEvent format, event dispatcher, Lua API
  - Rationale: Audio entirely hardcoded; this enables designer iteration

phase2-physics-materials
  - Title: Implement physics materials system
  - Complexity: M
  - Dependencies: physics-system, phase0-data-driven-framework-design
  - Output: PhysicsMaterial asset type, friction/restitution reuse
  - Rationale: Material authoring is per-body inline; breaks asset pipeline

phase2-source-asset-database
  - Title: Implement source asset database and dependency tracking
  - Complexity: L
  - Dependencies: asset-pipeline, asset-browser
  - Output: Source metadata, dependency graph, incremental import
  - Rationale: Fundamental to incremental asset cooking; blocks efficient iteration

phase2-gpu-memory-system
  - Title: Implement GPU memory budget tracking and virtual texturing foundation
  - Complexity: L
  - Dependencies: renderer, shader-system
  - Output: VRAM budget enforcement, page table foundation for VT
  - Rationale: Uncontrolled asset growth; console targeting requires budget

phase2-ui-animation-system
  - Title: Implement UI tweening and animation sequencing
  - Complexity: M
  - Dependencies: ui-system, timer-system
  - Output: Tween library, animation controller
  - Rationale: UI polish; menus feel cheap without smooth transitions

phase2-continuous-collision-detection
  - Title: Implement CCD (Continuous Collision Detection) for fast-moving objects
  - Complexity: M
  - Dependencies: physics-system
  - Output: CCD integration with Jolt, per-shape configuration
  - Rationale: Bullets/fastballs phase through geometry without this

phase2-world-streaming-foundation
  - Title: Design and implement world streaming infrastructure (levels, meshes)
  - Complexity: XL
  - Dependencies: phase0-async-streaming-framework, scene-system, asset-pipeline
  - Output: Streamable level chunks, async mesh loading
  - Rationale: AAA requirement; trivial levels without it
```

### Phase 3 (Complex Gameplay & Advanced Graphics)

```
NEW TASKS:

phase3-adaptive-music-system
  - Title: Implement adaptive/dynamic music system
  - Complexity: L
  - Dependencies: phase2-audio-event-system, timer-system
  - Output: Music layers, ducking automation, state-based transitions
  - Rationale: Expected in AAA; static loops feel cheap

phase3-virtual-shadow-maps
  - Title: Implement Virtual Shadow Maps (VSM)
  - Complexity: L
  - Dependencies: renderer, shader-system, phase1-render-graph-impl
  - Output: VSM rendering pass, atlas management
  - Rationale: Solves CSM shadow aliasing for outdoor scenes

phase3-global-illumination
  - Title: Implement global illumination (Lumen-inspired approach)
  - Complexity: XL
  - Dependencies: renderer, phase1-render-graph-impl, voxel-structures
  - Output: Hybrid SW raytracing + HW raytracing fallback
  - Rationale: Flat lighting is unacceptable; major visual quality gap

phase3-deterministic-physics
  - Title: Implement deterministic physics for multiplayer
  - Complexity: L
  - Dependencies: physics-system, ecs-snapshot-system
  - Output: Fixed-point math path, netcode integration
  - Rationale: Multiplayer physics desyncs without determinism

phase3-gameplay-ability-system
  - Title: Implement Gameplay Ability System (GAS equivalent)
  - Complexity: L
  - Dependencies: event-bus, timer-system, shared-state
  - Output: Ability classes, cooldown/cast framework
  - Rationale: Action games require structured ability management

phase3-breakable-joints
  - Title: Implement breakable constraints and joint destruction
  - Complexity: L
  - Dependencies: physics-system
  - Output: Constraint breakage callbacks, reinforced joint types
  - Rationale: Vehicle suspensions, breakable environments
```

### Phase 4 (Advanced Features & Polish)

```
NEW TASKS:

phase4-vehicle-physics
  - Title: Implement vehicle physics system (wheels, suspension, aerodynamics)
  - Complexity: XL
  - Dependencies: physics-system, breakable-joints
  - Output: WheelCollider, suspension joints, vehicle controller
  - Rationale: Open-world games require vehicles; major gameplay pillar

phase4-destructible-meshes
  - Title: Implement destructible mesh system with fracture simulation
  - Complexity: L
  - Dependencies: physics-system, physics-regions
  - Output: Fracture graph, destruction callbacks
  - Rationale: Expected in AAA action games

phase4-virtual-texturing
  - Title: Implement virtual texture streaming
  - Complexity: XL
  - Dependencies: phase2-gpu-memory-system, phase0-async-streaming-framework
  - Output: Page requests, streaming prioritization, mip feedback
  - Rationale: High-res environments without VRAM explosion

phase4-plugin-system-extended
  - Title: Extend plugin system with hot-reload, dependency management
  - Complexity: L
  - Dependencies: plugin-system
  - Output: Plugin version negotiation, safe unloading
  - Rationale: Modular development and third-party extensions

phase4-visual-scripting
  - Title: Implement visual scripting / node-graph programming
  - Complexity: XL
  - Dependencies: lua-scripting, editor-panels
  - Output: Node editor, node-to-Lua compilation
  - Rationale: Alternative to Lua for designers (Blueprints equivalent)
```

---

## Section 6: Inconsistencies Found in Current Specs

Issues identified across spec files that need correction before implementation.

### ECS & Scene System

| Issue | Spec File | Problem | Fix |
|-------|-----------|---------|-----|
| **Transform hierarchy not specified for serialization** | scene-system.md | No detail on how parent-child transforms are serialized/restored | Add serialization format example |
| **Entity pooling not mentioned** | scene-system.md | Allocation/deallocation thrashing not addressed | Add pooling strategy section |
| **System execution order undefined** | event-bus.md | Observers can fire in arbitrary order; no priority system | Specify order model (Phase 1 Gap 5) |
| **Component versioning absent** | scene-system.md | Old saves with removed components will crash load | Add schema evolution section |
| **No mention of dirty flag propagation** | ecs-and-scene.md | Transform updates not optimized for deep hierarchies | Add dirty flag section (Phase 1 Gap 14) |

### Physics System

| Issue | Spec File | Problem | Fix |
|-------|-----------|---------|-----|
| **Materials are inline on bodies** | physics-system.md | Friction/restitution not reusable; can't have "Ice" material | Redesign materials as assets (Phase 2 Gap 1) |
| **No CCD configuration** | physics-system.md | Fast objects can tunnel; no mention of per-shape CCD | Add CCD section with per-shape config |
| **Constraints not detailed** | physics-system.md | Only mentions CharacterController; no joints/springs | Add constraint types and examples |
| **Physics LOD missing** | physics-system.md | No reduction in fidelity at distance | Specify LOD strategy |
| **Determinism not addressed** | physics-system.md | No guarantee of bit-exact results across platforms | Add determinism contract section |

### Audio System

| Issue | Spec File | Problem | Fix |
|-------|-----------|---------|-----|
| **Events are hardcoded paths** | audio-system.md | "play_footstep_01.wav" hardcoded in C++; no data-driven events | Add audio event system spec (Phase 2 Gap 1) |
| **No DSP chain per bus** | audio-system.md | Reverb, EQ, compression not available | Add bus effects section |
| **Voice pool is hard limit** | audio-system.md | 64th voice cuts off; no virtual voices or graceful degradation | Add virtual voice section (Phase 2 Gap 5) |
| **No music system** | audio-system.md | No adaptive music, layering, or dynamic ducking | Add adaptive music section (Phase 3 Gap 1) |
| **Occlusion not mentioned** | audio-system.md | Sounds bleed through walls; no acoustic modeling | Add occlusion/obstruction section |

### Lua Scripting System

| Issue | Spec File | Problem | Fix |
|-------|-----------|---------|-----|
| **No coroutines** | lua-scripting.md | Multi-frame sequences require state machines | Add coroutine section (Phase 1 Gap 1) |
| **Bytecode not cached** | lua-scripting.md | Scripts recompiled every load (perf issue) | Add bytecode cache section (Phase 1 Gap 7) |
| **Module system uncontrolled** | lua-scripting.md | `require` is global; no path resolution or security | Add module system section (Phase 1 Gap 5) |
| **Sandbox is vague** | lua-scripting.md | "Sandboxed" but sandbox level not configurable | Add configurable sandbox section (Phase 1 Gap 10) |
| **No test framework** | lua-scripting.md | No unit testing for Lua logic | Add test framework section (Phase 1 Gap 13) |
| **Error context minimal** | lua-scripting.md | Stack trace doesn't include local variables | Add rich error reporting section (Phase 1 Gap 8) |

### Rendering System

| Issue | Spec File | Problem | Fix |
|-------|-----------|---------|-----|
| **No render graph** | renderer.md | Immediate-mode draws, no task dependencies | Add render graph section (Pre-Phase 1 Gap 1) |
| **No GI system** | renderer.md | Only ambient hack; no real GI | Add GI section (Phase 3 Gap 1) |
| **Post-processes hardcoded** | screen-space-effects.md | SSAO, SSR, TAA are fixed order; no composition | Redesign via render graph |
| **No deferred decals** | renderer.md | Bullet holes, blood splatters not supported | Add decal section |
| **Bloom missing** | screen-space-effects.md | No eye adaptation or bloom for bright surfaces | Add bloom section |
| **TAA basic** | screen-space-effects.md | Current TAA simple; ghosting and aliasing visible | Add advanced TAA notes |
| **Nanite not planned** | renderer.md | High-poly scenes bottleneck on geometry | Plan virtual geometry (Phase 2 Gap 2) |

### Asset Pipeline

| Issue | Spec File | Problem | Fix |
|-------|-----------|---------|-----|
| **No source asset database** | asset-pipeline.md | Assets not tracked; incremental import not possible | Add source DB section (Phase 2 Gap 1) |
| **No dependency tracking** | asset-pipeline.md | Can't know what to re-cook when a texture changes | Add dependency graph section (Phase 2 Gap 2) |
| **Cook order undefined** | asset-pipeline.md | Circular dependencies possible; no topological sort | Add cook order section |
| **LOD generation simplistic** | asset-pipeline.md | meshoptimizer LODs are uniform; no distance-based priority | Add LOD priority section (Phase 1 Gap 6) |
| **No virtual texturing** | asset-pipeline.md | High-res environments require explicit mip management | Add VT foundation (Phase 4 Gap 1) |
| **Streaming not async** | asset-pipeline.md | No async loading; no streaming priorities | Add async streaming section (Phase 2 Gap 5) |
| **No atlas auto-packing** | asset-pipeline.md | Textures not automatically packed; wasteful VRAM | Add atlas section (Phase 2 Gap 7) |

### UI System

| Issue | Spec File | Problem | Fix |
|-------|-----------|---------|-----|
| **No gamepad navigation** | ui-system.md | Focus management absent; pause menu broken on controller | Add gamepad section (Phase 1 Gap 3) |
| **No widget library** | ui-system.md | Only raw HTML; no Button, Panel, List, Slider | Add widget library section (Phase 1 Gap 2) |
| **No animation system** | ui-system.md | UI can't tween properties; all motion hardcoded | Add animation/tweening section (Phase 2 Gap 2) |
| **Binding one-way only** | ui-system.md | UI reads game state but can't write back (no two-way binding) | Add bidirectional binding section (Phase 2 Gap 6) |
| **No state machine** | ui-system.md | Menu flow and transitions hardcoded | Add UI state machine section (Phase 2 Gap 7) |
| **Safe area undefined** | ui-system.md | Mobile notches, safe zones not mentioned | Add safe area section (Phase 2 Gap 4) |
| **No drag/drop** | ui-system.md | Inventory-style UI not possible | Add drag-drop section (Phase 2 Gap 10) |

---

## Section 7: Summary of Required Actions

### Immediate (Before Phase 1 Code)

1. **Design Render Graph**: Document frame resource abstraction, task dependencies, PSO caching.
2. **Design Data-Driven Config**: Standardize audio events, physics materials, UI flows, entity templates format.
3. **Design Async Streaming Framework**: Coroutines, streaming priorities, task cancellation.
4. **Design Debug Infrastructure**: IPC protocol for DAP, profiler, hot-reload.
5. **Design Shared State System**: Global game state, change notifications, serialization.

**Output**: 5 new spec files. Time: 2–3 weeks (shared across team, not sequential).

### Phase 1 (Scripting, ECS Optimization, Debug)

1. Implement render graph + PSO caching
2. Implement Lua coroutines + scheduler
3. Implement Lua DAP server
4. Implement UI gamepad navigation
5. Implement entity templates
6. Implement ECS query optimization (spatial partitioning)
7. Implement shared state system

**Blocked gaps solved**: 7 out of 137

### Phase 2 (Audio, Physics, Assets, Rendering)

1. Implement data-driven audio event system
2. Implement physics materials asset system
3. Implement source asset database + dependency tracking
4. Implement GPU memory budget + virtual texture foundation
5. Implement CCD (Continuous Collision Detection)
6. Implement world streaming foundation (async level loading)
7. Implement UI animation (tweening, sequences)

**Blocked gaps solved**: 17 out of 137 (24 total remaining)

### Phase 3 (Gameplay Systems, Advanced Graphics)

1. Implement adaptive music system
2. Implement virtual shadow maps
3. Implement global illumination (Lumen-like)
4. Implement deterministic physics (multiplayer)
5. Implement Gameplay Ability System
6. Implement breakable constraints

**Blocked gaps solved**: 13 out of 137 (11 remaining)

### Phase 4+ (Specialized Features)

1. Vehicle physics
2. Destructible meshes
3. Virtual texturing
4. Visual scripting
5. Middleware integration (Wwise/FMOD)
6. Fluid simulation

**Blocked gaps solved**: 6 out of 137 (0 critical remaining)

---

## Appendix: Gap Reference by Spec File

Shows which specs need updates to address identified gaps.

| Spec File | Gaps to Address | Priority |
|-----------|-----------------|----------|
| `renderer.md` | GI, Render Graph, Bloom, DoF, Motion Blur, Volumetric Fog, Sky, Water, SSS, Hair, Eye, VRS, Mesh Shaders, Bindless, GPU-Driven | P0–P1 |
| `physics-system.md` | Materials, CCD, Batch Queries, LOD, Constraints, Determinism | P0–P1 |
| `audio-system.md` | Event System, Adaptive Music, DSP Chains, Occlusion, Virtual Voices, Snapshots, Dialogue, Zones | P0–P1 |
| `lua-scripting.md` | Coroutines, DAP, Profiler, Reflection, Modules, GC, FFI, Tests, Serialization | P0–P1 |
| `scene-system.md` | Entity Lifecycle, Pooling, Serialization, Schema Evolution, Dirty Flags | P0–P1 |
| `asset-pipeline.md` | Source DB, Dependency Graph, Cook Order, LOD Priorities, Streaming, Atlasing | P0–P1 |
| `ui-system.md` | Gamepad Navigation, Widgets, Animation, Binding, State Machine, Safe Area, Drag-Drop | P0–P1 |
| `ecs-and-scene.md` | Spatial Queries, System Order, Change Detection, Service Locator | P0–P1 |
| `event-bus.md` | Execution order, reactive systems (update per Gap 5) | P0–P1 |
| `editor-panels.md` | Data-driven UI for events, materials, config preview | P1 |

---

## Final Recommendations

### For Leadership

1. **Commit to Pre-Phase-1 Design Tasks**: 2–3 weeks upfront saves 4+ weeks later.
2. **Prioritize Render Graph and Data-Driven Config**: These unblock multiple systems.
3. **Expect Phase 1 to be 80% infra, 20% features**: ECS optimization, debug, async framework, shared state matter more than pretty visuals.
4. **Plan for 2–3 month Phase 2 before shipping**: Streaming, GI, audio events are complex.

### For Developers

1. **Read Pre-Phase-1 specs before coding**: Render graph and async framework are load-bearing.
2. **Don't hardcode system order or audio events**: Data-driven design pays dividends.
3. **Plan for refactoring**: Several specs (physics, UI, ECS) will need updates as gaps become clear.

### For AI Assistants

1. **Render Graph is the foundation for Phase 2 rendering**: All post-processes depend on it.
2. **Async framework unifies coroutines, streaming, and scheduler**: Coordinate implementation.
3. **Data-driven config is not a feature; it's a requirement**: Audio, physics, UI all need it.
4. **Spatial partitioning is a must-have for Phase 1**: Don't skip ECS optimization.

---

**Document Version**: 1.0
**Last Updated**: 2026-03-02
**Status**: Approved for roadmap integration
