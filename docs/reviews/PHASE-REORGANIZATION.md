# Phase Reorganization Plan

**Date**: 2026-03-02
**Status**: Approved
**Author**: Planning review

---

## Overview

The original 5-phase structure (Phase 0–4, plus an informal "Phase 5 Future" wishlist) has grown unwieldy. Phases 2 and 3 each contain 40–65 tasks that span wildly different dependency levels — from fundamental PBR rendering to AAA atmospheric scattering, from basic input handling to motion matching and HRTF binaural audio. This mix makes it impossible to declare a phase "done" and ship a milestone.

**Current state**: 178 tasks across 5 phases.
- Phase 0: 9 tasks (architecture design) — well-scoped
- Phase 1: 26 tasks (MVP engine) — slightly overloaded with ECS/Lua extras
- Phase 2: 53 tasks (rendering + assets + audio + UI + AI tools) — severely overloaded
- Phase 3: 65 tasks (game systems + advanced rendering + animation AAA) — severely overloaded
- Phase 4: 25 tasks (AAA + shipping) — reasonable scope but mixed priorities

**Proposed state**: 8 phases + Tools track (~196 tasks including 18 new tool tasks).
- Each phase has a single, clear deliverable milestone
- Dependencies flow forward cleanly: no phase requires tasks from a later phase
- "Tools" is a parallel track, not a blocking phase

---

## Phase Structure

### Phase 0 — Architecture & Design (9 tasks) [UNCHANGED]

**Goal**: All architectural decisions documented before Phase 1 begins. No code produced.

These tasks exist to prevent expensive rewrites later. Decisions made here cascade into every phase.

| Task ID | Description | Blocks |
|---------|-------------|--------|
| phase0-rhi-design | RHI vtable for DX12/Vulkan/Metal | All rendering |
| phase0-render-graph-design | Render graph DAG + barrier scheduling | Phase 2 renderer |
| phase0-job-system-design | JobDispatcher API, work-stealing, enkiTS eval | All multi-threaded systems |
| phase0-audio-abstraction-layer | IAudioEngine interface + NullAudioEngine | Phase 2 audio |
| phase0-platform-abstraction-layer | IPlatform for filesystem/threads/timers | CI + cross-platform |

The remaining 4 Phase 0 tasks are documented in `.rulebook/tasks/phase0-*`.

**Migration**: No change. These tasks stay in Phase 0.

---

### Phase 1 — Core Engine MVP (18 tasks) [REDUCED from 26]

**Goal**: A playable demo scene loadable from VSCode within 3 months. This is the "show it works" milestone — a developer can open a `.uzscene` file, see it rendered live in the VSCode viewport, attach a Lua script, press Play, and interact with the scene.

**Exit criteria**:
- `.uzscene` opens and displays visually in Scene Tree + viewport
- Lua scripts attach to nodes and execute OnInit/OnUpdate
- Play mode spawns a separate `render.exe --play` process
- Shader compilation errors annotated inline in VSCode
- Crash reporter captures SEH exceptions with minidump

#### Tasks STAYING in Phase 1 (core MVP only):

| Task ID | Complexity | Notes |
|---------|------------|-------|
| phase1-scene-system | L | Foundational — nothing works without this |
| phase1-debug-profiling | M | Logging + Tracy + ImGui overlay |
| phase1-crash-reporting | M | SEH + minidump — safety feature |
| phase1-console-commands | M | ~ key console + IPC handler |
| phase1-shader-hot-reload | M | DXC + file watcher + VSCode diagnostics |
| phase1-lua-scripting | L | LuaJIT VM + ctx.* bindings + hot-reload |
| phase1-timer-system | S | ctx:schedule_after/every/cancel_timer |
| phase1-editor-panels | L | Scene Tree + Inspector + Asset Browser panels |
| phase1-play-mode | M | --editor vs --play process separation |
| phase1-asset-browser | M | File browser panel + drag-and-drop |
| phase1-undo-redo | M | EditorCommand + UndoStack (100) |
| phase1-editor-gizmos | L | Translate/Rotate/Scale handles in viewport |
| phase1-multi-select | M | SelectionManager + rubber-band select |
| phase1-vscode-syntax-highlighting | S | TextMate grammar for .uzscene/.uzmat/.uzproj |
| phase1-lua-coroutines | S | LuaJIT coroutine support for async logic |
| phase1-lua-module-system | S | require() path resolution |
| phase1-script-execution-safety | M | Watchdog + instruction budget |
| phase1-entity-templates | S | Prefab-lite entity templates |

**Count**: 18 tasks

#### Tasks MOVING from Phase 1 to later phases:

| Task ID | Moved To | Reason |
|---------|----------|--------|
| phase1-lua-dap-server | Phase 3 | DAP debugger is quality-of-life, not MVP blocker |
| phase1-ecs-spatial-query | Phase 3 | Gameplay optimization, not needed for basic demo |
| phase1-ecs-system-scheduler | Phase 3 | Parallel ECS — optimization feature |

---

### Phase 2 — Rendering Foundation (22 tasks) [RESTRUCTURED]

**Goal**: Full PBR deferred renderer with imported 3D assets, physics simulation, spatial audio, and game UI. After Phase 2, a team can build a complete playable game level — just not an AAA-quality one.

**Exit criteria**:
- PBR materials with GGX BRDF, IBL, and tone-mapping working on imported glTF assets
- Cascaded + point + spot shadow maps
- Physics-driven characters (Jolt + character controller)
- XAudio2 spatial audio with 4-bus mixing
- UI system rendering game HUDs
- Asset PAK for shipping-mode asset loading

#### Tasks in Phase 2:

| Task ID | Complexity | Source |
|---------|------------|--------|
| phase2-pbr-renderer | XL | Original Phase 2 |
| phase2-bindless-resources | XL | Gap analysis — P0 prerequisite |
| phase2-ibl-pipeline | L | Gap analysis — P0 (metallic surfaces currently black) |
| phase2-shader-library | M | Original Phase 2 |
| phase2-shader-variants | M | Original Phase 2 |
| phase2-point-spot-shadows | L | Original Phase 2 (shadow maps) |
| phase2-gltf-import | L | Original Phase 2 |
| phase2-fbx-import | M | Original Phase 2 |
| phase2-texture-tools | M | Original Phase 2 |
| phase2-asset-pak | M | Original Phase 2 |
| phase2-gpu-instancing | M | Original Phase 2 |
| phase2-ssao | M | Original Phase 2 |
| phase2-audio-system | L | Original Phase 2 |
| phase2-ui-system | L | Original Phase 2 |
| phase2-ai-native-features | L | Original Phase 2 |
| phase2-prefab-edit-mode | M | Original Phase 2 |
| phase2-auto-exposure-color-grading | M | Gap analysis |
| phase2-sdf-font-atlas | M | Gap analysis |
| phase2-asset-hot-reload-textures-meshes | M | Gap analysis |
| phase2-frame-pacing-vsync-modes | M | Gap analysis |
| phase2-cvar-developer-config-system | M | Gap analysis |
| phase2-simd-math-optimization | M | Gap analysis |

**Count**: 22 tasks

#### Tasks from original Phase 2 MOVING to later phases:

| Task ID | Moved To | Reason |
|---------|----------|--------|
| phase2-render-graph-async-compute | Phase 4 | Optimization — not needed for functional rendering |
| phase2-occlusion-culling-hzb | Phase 4 | Performance feature — not MVP rendering |
| phase2-physics-multithreading | Phase 3 | Belongs with physics system |
| phase2-memory-profiler | Phase 3 | Developer tooling, not game feature |
| All phase2-ui-* subtasks (10) | Phase 3 | UI advanced features belong after base UI works |
| phase2-profiler-instrumentation-api | Phase 3 | Developer tooling |
| phase2-audio-profiler-metering | Phase 3 | Advanced audio feature |
| phase2-audio-bus-dynamic-graph | Phase 3 | Advanced audio feature |
| phase2-script-intellisense-lsp | Phase 3 | Developer quality-of-life |
| phase2-script-linting-static-analysis | Phase 3 | Developer quality-of-life |

---

### Phase 3 — Engine Systems (28 tasks)

**Goal**: Full game development capability. After Phase 3, a studio can ship a complete game: animation, UI, audio polish, physics, input, camera, scripting tools, and localization all working together.

**Exit criteria**:
- Skeletal mesh + animation state machine + blending
- Physics system (Jolt) with character controller and ragdoll
- Enhanced Input system with runtime rebinding
- Camera system (SpringArm + multiple modes)
- Save/Load system with autosave
- Localization with CSV translations
- Particle system with GPU compute simulation
- Event Bus and Tags for gameplay logic

#### Tasks in Phase 3:

| Task ID | Complexity | Source |
|---------|------------|--------|
| phase3-event-bus | S | Original Phase 3 |
| phase3-tags-groups | S | Original Phase 3 |
| phase3-physics-system | XL | Original Phase 3 |
| phase3-skeletal-mesh | L | Original Phase 3 |
| phase3-animation-system | L | Original Phase 3 |
| phase3-ragdoll | M | Original Phase 3 |
| phase3-input-system | M | Original Phase 3 |
| phase3-camera-system | M | Original Phase 3 |
| phase3-particle-system | L | Original Phase 3 |
| phase3-save-system | M | Original Phase 3 |
| phase3-localization | M | Original Phase 3 |
| phase3-game-mode-state-machine | M | Gap analysis |
| phase3-game-settings-framework | M | Gap analysis |
| phase3-day-night-cycle | M | Gap analysis |
| phase3-time-dilation | S | Gap analysis |
| phase3-seeded-rng | S | Gap analysis |
| phase3-controller-haptics | S | Gap analysis |
| phase3-accessibility-framework | L | Gap analysis |
| phase2-script-intellisense-lsp | M | Moved from Phase 2 |
| phase2-script-linting-static-analysis | S | Moved from Phase 2 |
| phase1-lua-dap-server | L | Moved from Phase 1 |
| phase1-ecs-spatial-query | S | Moved from Phase 1 |
| phase1-ecs-system-scheduler | M | Moved from Phase 1 |
| phase2-memory-profiler | M | Moved from Phase 2 |
| phase2-audio-profiler-metering | M | Moved from Phase 2 |
| phase2-audio-bus-dynamic-graph | M | Moved from Phase 2 |
| phase2-ui-profiler-devtools | M | Moved from Phase 2 |
| phase2-ui-hot-reload-stable | M | Moved from Phase 2 |

**Count**: 28 tasks

---

### Phase 4 — Advanced Rendering (20 tasks)

**Goal**: AAA visual quality milestone. After Phase 4, the engine can produce screenshots and gameplay video indistinguishable from commercial games. This is the "trailer-quality" milestone.

**Exit criteria**:
- TAA with proper motion vectors and variance clamping
- SSR (Screen-Space Reflections)
- Volumetric fog (froxel scatter)
- Atmospheric scattering (Bruneton)
- Clustered shading (200+ point lights)
- Virtual shadow maps
- Async compute utilization
- HZB occlusion culling

#### Tasks in Phase 4:

| Task ID | Complexity | Source |
|---------|------------|--------|
| phase3-taa | M | Moved from original Phase 3 |
| phase3-ssr | L | Moved from original Phase 3 |
| phase3-volumetric-fog-froxel | L | Gap analysis — moved from Phase 3 |
| phase3-sky-atmosphere-bruneton | L | Gap analysis — moved from Phase 3 |
| phase3-clustered-shading-3d | L | Gap analysis — moved from Phase 3 |
| phase3-virtual-shadow-maps | L | Gap analysis — moved from Phase 3 |
| phase3-area-lights-ltc | L | Gap analysis — moved from Phase 3 |
| phase3-subsurface-scattering | M | Gap analysis — moved from Phase 3 |
| phase3-motion-blur | M | Gap analysis — moved from Phase 3 |
| phase2-render-graph-async-compute | M | Moved from Phase 2 |
| phase2-occlusion-culling-hzb | L | Moved from Phase 2 |
| phase3-animation-compression | M | Gap analysis |
| phase3-animation-lod | M | Gap analysis |
| phase3-audio-hrtf-binaural | M | Gap analysis |
| phase3-audio-reverb-zones | M | Gap analysis |
| phase3-audio-lod | M | Gap analysis |
| phase3-audio-occlusion | M | Gap analysis |
| phase3-ktx2-basis-universal | L | Gap analysis |
| phase3-normal-map-baking | L | Gap analysis |
| phase3-auto-uv-unwrap | M | Gap analysis |

**Count**: 20 tasks

---

### Phase 5 — Gameplay Systems (25 tasks)

**Goal**: Fully-featured gameplay authoring. After Phase 5, designers can build complex game systems without writing C++ — behavior trees, dialogue, ability systems, vehicle physics, and procedural content all available.

**Exit criteria**:
- Behavior trees for AI agents
- Dialogue system with branching + subtitles
- Day/night cycle with keyframe light curves
- Vehicle physics (wheels, suspension, drivetrain)
- Advanced animation features (motion warping, IK, distance matching)
- Opus audio codec for efficient audio bundles

#### Tasks in Phase 5:

| Task ID | Complexity | Source |
|---------|------------|--------|
| phase3-motion-warping | L | Gap analysis |
| phase3-aim-offset | M | Gap analysis |
| phase3-procedural-foot-placement | M | Gap analysis |
| phase3-sync-markers-inertialization | L | Gap analysis |
| phase3-jiggle-bones-secondary-anim | M | Gap analysis |
| phase3-distance-matching | M | Gap analysis |
| phase3-stride-orientation-warping | M | Gap analysis |
| phase3-mirror-animation | S | Gap analysis |
| phase3-physics-batch-queries | M | Gap analysis |
| phase3-compound-colliders | M | Gap analysis |
| phase3-physics-regions-gravity-zones | M | Gap analysis |
| phase3-explosion-radial-force | S | Gap analysis |
| phase3-physics-debug-profiler | S | Gap analysis |
| phase3-audio-animation-sync | M | Gap analysis |
| phase3-opus-audio-codec | M | Gap analysis |
| phase3-ui-drag-drop | M | Gap analysis |
| phase3-ui-world-space-billboards | L | Gap analysis |
| phase3-ui-minimap-widget | L | Gap analysis |
| phase3-ui-accessibility | M | Gap analysis |
| phase3-font-fallback-cjk | M | Gap analysis |
| phase3-ui-rich-text | M | Moved from Phase 2 |
| phase3-ui-text-input-fields | M | Moved from Phase 2 |
| phase3-ui-state-machine-nav-stack | M | Moved from Phase 2 |
| phase3-ui-theme-system | M | Moved from Phase 2 |
| phase2-ui-testing-framework | L | Moved from Phase 2 |

**Count**: 25 tasks

---

### Phase 6 — World & Tools (28 tasks)

**Goal**: Open-world game capability and specialized content-authoring tools. After Phase 6, a team can build an open-world MMORPG or large landscape game with AI-assisted world generation.

**Exit criteria**:
- Terrain system with heightmap, splatmap, LOD, foliage
- NavMesh with Recast+Detour+RVO2 pathfinding
- Particle system VFX editor
- Worldgen graph compiler and MMORPG node library
- Plugin system with stable C ABI
- Build pipeline (cook + pack + stage + package)

#### Tasks in Phase 6:

| Task ID | Complexity | Source |
|---------|------------|--------|
| phase4-terrain-system | XL | Original Phase 4 |
| phase4-navmesh | L | Original Phase 4 |
| phase4-plugin-system | L | Original Phase 4 |
| phase4-build-pipeline | L | Original Phase 4 |
| phase4-rope-chain-simulation | M | Original Phase 4 |
| phase4-soft-body-physics | L | Original Phase 4 |
| phase4-buoyancy-system | M | Original Phase 4 |
| phase4-wind-system-unified | M | Original Phase 4 |
| phase4-convex-decomposition-vhacd | M | Original Phase 4 |
| phase3-ui-reference-resolution | S | Moved from Phase 2 |
| phase2-ui-reference-resolution | S | Moved from Phase 2 |
| (worldgen-graph-compiler) | XL | New — worldgen pipeline |
| (worldgen-mmorpg-nodes) | L | New — MMORPG biome nodes |
| (worldgen-ai-generator) | L | New — AI-driven worldgen |
| (worldgen-node-editor) | L | New — VSCode node editor UI |
| (vfx-node-editor) | L | New — visual VFX authoring |
| (vfx-ai-generator) | M | New — AI generates .uzfx |
| (impostor-billboards) | M | New — distant mesh impostors |
| (proximity-lod-swap) | M | New — distance LOD transitions |
| (modular-skeleton-merge) | L | New — runtime skeleton composition |
| (material-merge-atlas) | M | New — atlas merge for draw call reduction |

**Note**: Items marked "(new)" are not yet tracked as Rulebook tasks and need task creation.

**Count**: ~21 tracked + 10 new = ~28 tasks

---

### Phase 7 — AAA & Shipping (22 tasks)

**Goal**: Cutting-edge visual features and full production shipping pipeline. After Phase 7, the engine is competitive with UE5 and Unity for AAA-tier productions.

**Exit criteria**:
- DXR ray tracing (reflections, shadows, AO)
- GPU-driven rendering (ExecuteIndirect, 10,000+ objects)
- DLSS 3 / FSR 3 / XeSS temporal upsampling
- Nanite-like virtual geometry (meshlets)
- Virtual texturing
- Cloth simulation
- Destructible meshes
- Visual scripting (node graph)

#### Tasks in Phase 7:

| Task ID | Complexity | Source |
|---------|------------|--------|
| phase4-dxr-ray-tracing-rhi | XL | Original Phase 4 |
| phase4-gpu-driven-rendering | XL | Original Phase 4 |
| phase4-temporal-upsampling-dlss-fsr | L | Original Phase 4 |
| phase4-nanite-virtual-geometry | XL | Original Phase 4 |
| phase4-virtual-texturing | XL | Original Phase 4 |
| phase4-cloth-simulation | L | Original Phase 4 |
| phase4-destructible-meshes | L | Original Phase 4 |
| phase4-visual-scripting | XL | Original Phase 4 |

**Count**: 8 tracked + space for 14 future tasks

---

### Tools Track — Developer Tools (18 tasks) [NEW PARALLEL TRACK]

**Goal**: Production-grade developer tooling that improves iteration velocity, shipping confidence, and content creation quality. Tools tasks run in parallel with any phase — they are not blocking dependencies.

**Note**: This is not a phase. Tools tasks can be implemented as soon as their underlying systems exist. For example, `tools-shader-debugger` requires Phase 2 PBR renderer; `tools-scene-linter` requires Phase 1 scene system.

**Minimum system requirements per tool**:

| Tool Task ID | Min Phase Required | Priority |
|-------------|-------------------|----------|
| tools-content-audit | Phase 2 (Asset PAK) | P0 |
| tools-shader-debugger | Phase 2 (PBR Renderer) | P0 |
| tools-project-packager | Phase 4 (Build Pipeline) | P0 |
| tools-draw-call-analyzer | Phase 2 (PBR Renderer) | P0 |
| tools-scene-linter | Phase 1 (Scene System) | P1 |
| tools-asset-validator | Phase 2 (GLTF Import) | P1 |
| tools-gpu-memory-viz | Phase 2 (PBR Renderer) | P1 |
| tools-uzmesh-inspector | Phase 2 (GLTF Import) | P1 |
| tools-spline-editor | Phase 6 (Terrain) | P1 |
| tools-performance-budget-enforcer | Phase 2 (Debug Profiling) | P1 |
| tools-texture-batch-processor | Phase 2 (Texture Tools) | P2 |
| tools-mesh-optimizer-cli | Phase 2 (GLTF Import) | P2 |
| tools-hlsl-formatter | Phase 1 (Shader Hot-Reload) | P2 |
| tools-shader-profiler-view | Phase 2 (PBR Renderer) | P2 |
| tools-localization-export | Phase 3 (Localization) | P2 |
| tools-heightmap-converter | Phase 6 (Terrain) | P2 |
| tools-level-streaming-config | Phase 6 (Terrain) | P3 |
| tools-network-simulator | Future (Networking) | P3 |

**Count**: 18 tasks

---

## Missing Tool Tasks (NEW)

These tasks are not currently tracked in the Rulebook but represent critical production gaps. All 18 are documented below. The top 10 have `proposal.md` files in `.rulebook/tasks/`.

### Asset Tools

**tools-texture-batch-processor**
Standalone CLI for bulk texture compression (BC7/BC5/BC6H). CI pipelines need to recompress textures in batch without opening VSCode. Without this, artists must manually reimport each texture after bulk asset delivery from external vendors.

**tools-mesh-optimizer-cli**
CLI wrapper for meshoptimizer: LOD generation, vertex cache optimization, overdraw optimization. Enables batch processing of FBX/glTF files in CI without interactive import. Critical for studios with automated asset pipelines from DCC tools.

**tools-asset-validator**
Pre-import validation: duplicate vertices, degenerate triangles, non-power-of-2 textures, file size limits, naming conventions. Prevents broken assets from entering the project — a corrupted mesh can silently crash the engine 2 hours into a play session.

**tools-uzmesh-inspector**
VSCode panel showing .uzmesh internals: vertex/index count per LOD, material slots, bone count, AABB, disk vs. GPU size. Developers currently have no visibility into what the mesh compiler produced — this enables diagnosing import quality issues without external tools.

**tools-scene-linter**
Static analysis for .uzscene files: missing asset references, invalid property values, node cycles, broken prefab references, scripts referencing non-existent nodes. Catches errors before runtime — essential for large scenes with dozens of contributors.

### Shader Tools

**tools-shader-debugger**
PIX/RenderDoc integration: VSCode command "Capture DX12 Frame" writes a PIX capture file and opens it in PIX or RenderDoc. Eliminates the manual workflow of launching PIX separately, attaching to process, and capturing. GPU debugging currently requires 5 manual steps; this reduces to 1.

**tools-hlsl-formatter**
Auto-format HLSL files on save (clang-format HLSL rules). Without consistent HLSL formatting, AI-generated shaders and human-authored shaders diverge in style, making diffs noisy and code review slow.

**tools-shader-profiler-view**
VSCode panel showing per-shader GPU timing from D3D12 timestamp queries. Developers currently have no in-editor visibility into which shaders are consuming GPU time — they must launch PIX for every profiling session.

### Build & Package Tools

**tools-project-packager**
VSCode GUI for the build pipeline: select target platform, configure content packs, set DLC groups, run cook+stage+package with live progress bar and error log. Without this, shipping a build requires memorizing CLI flags — a barrier for junior developers and external collaborators.

**tools-content-audit**
Full project asset analysis: VRAM budget (total + per category), unused assets (never referenced in scenes), duplicate textures (hash match), oversized textures (4K in UI), meshes missing LODs. Without content auditing, shipped games accumulate MB of dead assets and approach VRAM limits silently.

**tools-localization-export**
Exports all registered localization keys to CSV for external translator handoff, then imports translated CSV back and validates that no keys are missing. Manual CSV management at scale is error-prone — this ensures translators receive the exact keys the game needs.

**tools-changelog-generator**
Generates CHANGELOG entries from git commits between two version tags using conventional commit format. Reduces release documentation effort from 2 hours to 30 seconds.

### World & Level Tools

**tools-heightmap-converter**
Converts between heightmap formats: RAW16 (Houdini export), PNG16, 32-bit EXR, and UzEngine .uzterrain binary. Artists and level designers work in different DCC tools; without conversion, each new terrain source requires manual format negotiation.

**tools-spline-editor**
In-viewport Bezier spline authoring (roads, rivers, paths, fences) that outputs spline data to .uzscene. A fundamental world-building primitive in every open-world game engine — its absence forces developers to manually place hundreds of individual nodes.

**tools-level-streaming-config**
Visual editor for configuring world streaming volumes and chunk boundaries. Replaces JSON-only configuration for large world partitioning — misconfigurations cause visible pop-in and are very hard to diagnose.

### Performance Tools

**tools-gpu-memory-viz**
VSCode panel visualizing GPU heap allocations: texture VRAM breakdown by category, buffer allocations, heap fragmentation. VRAM oversubscription is the #1 cause of unexplained GPU crashes on customer hardware — this makes VRAM usage visible before shipping.

**tools-draw-call-analyzer**
Captures one frame, groups draw calls by material/mesh/pass, identifies batching opportunities and redundant state changes. A single unoptimized material can multiply draw calls 10x — this tool surfaces the worst offenders without requiring an external GPU profiler.

**tools-performance-budget-enforcer**
CI check that fails a build if frame time, VRAM, or draw call count exceeds configured thresholds. Without automated budget enforcement, performance regressions reach main branch silently — discovered only when a tester reports stuttering.

### Network & Worldgen Tools

**tools-network-simulator**
Simulates latency, packet loss, and jitter for local multiplayer testing. Required when networking is added (Phase 5+ future track).

**tools-biome-painter**
Standalone VSCode panel for painting biome boundaries on a map before worldgen runs. Gives designers manual control over the high-level world layout before the AI/procedural systems fill in details.

**tools-terrain-heightmap-preview**
Fast 3D preview of any heightmap PNG without loading the full engine. Enables rapid terrain iteration directly in VSCode — comparing 10 heightmap variants currently requires 10 full engine imports.

---

## Rulebook Task ID Naming Convention

All new tool tasks follow the pattern `tools-{category}-{name}`:
- `tools-texture-batch-processor`
- `tools-mesh-optimizer-cli`
- `tools-asset-validator`
- `tools-uzmesh-inspector`
- `tools-scene-linter`
- `tools-shader-debugger`
- `tools-hlsl-formatter`
- `tools-shader-profiler-view`
- `tools-project-packager`
- `tools-content-audit`
- `tools-localization-export`
- `tools-changelog-generator`
- `tools-heightmap-converter`
- `tools-spline-editor`
- `tools-level-streaming-config`
- `tools-gpu-memory-viz`
- `tools-draw-call-analyzer`
- `tools-performance-budget-enforcer`

---

## Migration Notes

1. **Existing `.rulebook/tasks/phase*/` directories do NOT need to be renamed or moved.** Rulebook task IDs are tracking identifiers — the `phase1-`, `phase2-` prefix in a task ID is informational, not a binding contract. A task originally named `phase2-occlusion-culling-hzb` can be re-scheduled to Phase 4 without renaming its folder.

2. **Only ROADMAP.md and MEMORY.md need updating** to reflect the new phase assignments. The task folders, proposal.md files, and tasks.md checklists remain unchanged.

3. **New `tools-*` tasks require new Rulebook task directories** to be created. The top 10 priority tools have `proposal.md` files created at `.rulebook/tasks/{task-id}/proposal.md`.

4. **Phase numbering in ROADMAP.md** uses the new 0–7 + Tools structure going forward. The old Phase 5 "Future" ideas are redistributed into Phases 5–7 or noted as unscheduled.

5. **Task count by phase** (new structure):
   - Phase 0: 9 tasks
   - Phase 1: 18 tasks
   - Phase 2: 22 tasks
   - Phase 3: 28 tasks
   - Phase 4: 20 tasks
   - Phase 5: 25 tasks
   - Phase 6: ~28 tasks (10 need Rulebook task creation)
   - Phase 7: ~22 tasks (14 need Rulebook task creation)
   - Tools: 18 tasks (all need Rulebook task creation)
   **Total**: ~190 tasks

---

## Summary Table

| Phase | Name | Tasks | Goal | Release Milestone |
|-------|------|-------|------|-------------------|
| 0 | Architecture & Design | 9 | Design docs, no code | Before Phase 1 |
| 1 | Core Engine MVP | 18 | Playable demo in VSCode | Month 3 |
| 2 | Rendering Foundation | 22 | Full PBR, physics, audio, glTF | Month 6 |
| 3 | Engine Systems | 28 | Animation, UI, scripting, input | Month 9 |
| 4 | Advanced Rendering | 20 | TAA, SSR, volumetrics, clustering | Month 12 |
| 5 | Gameplay Systems | 25 | Abilities, dialogue, AI, vehicles | Month 18 |
| 6 | World & Tools | 28 | Terrain, WorldGen, VFX, NavMesh | Month 24 |
| 7 | AAA & Shipping | 22 | DXR, Nanite, DLSS, build pipeline | Month 30 |
| Tools | Developer Tools | 18 | Asset/shader/perf/level tools | Parallel track |
