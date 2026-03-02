# UzEngine Documentation

UzEngine is an AI-native 3D game engine that runs inside VSCode. All scene data, shaders, and materials are plain-text files designed for LLM generation and editing.

---

## Product Roadmap

**Start here for requirements and goals:** [PRD.md](PRD.md) — Product Requirements Document with vision, success metrics, functional & non-functional requirements for all phases.

**Sequential development plan:** [DEVELOPMENT-PLAN.md](DEVELOPMENT-PLAN.md) — 80 tasks organized in phases and blocks with clear deliverables, prerequisites, and task-id references. **Start here to begin development.**

**Full roadmap with dependencies:** [ROADMAP.md](ROADMAP.md) — 5-phase roadmap with complexity estimates, dependency graph, and definition of done per task.

---

## Engine C++ Documentation

Reference documentation for the engine's C++ codebase — patterns, module structure, and systems used to build games with UzEngine.

| Document | Description |
|----------|-------------|
| [engine/design-decisions.md](engine/design-decisions.md) | Why we made each architectural choice — Unity/Unreal/Godot comparison, lessons learned |
| [engine/lifecycle-and-concurrency.md](engine/lifecycle-and-concurrency.md) | Boot/shutdown sequence, all threads, ownership table, sync primitives, thread-safety invariants |
| [engine/cpp-architecture.md](engine/cpp-architecture.md) | Core layer APIs (Arena, Handle, Result, StringId), asset identity, prefab system, ECS |
| [engine/hot-reload.md](engine/hot-reload.md) | Shader hot-reload (DXC), scene/material reload, game DLL hot-reload (shadow copy + stable C ABI) |
| [engine/module-structure.md](engine/module-structure.md) | Six-module breakdown (Engine, Renderer, Runtime, Build, Editor, UI), source tree, dependency rules, CMake targets |
| [engine/ecs-and-scene.md](engine/ecs-and-scene.md) | ECS architecture (EnTT), component catalog, scene graph tree, .uzscene format, prefab instantiation |
| [engine/ecs-advanced.md](engine/ecs-advanced.md) | Advanced ECS patterns: registry context service locator, reactive systems with signals, component versioning, entity templates, prefab hierarchies, transform dirty flags, thread safety |
| [engine/scripting-system.md](engine/scripting-system.md) | LuaJIT VM lifecycle, script instances, sandboxing, C++ bindings, metatables, ctx table, hot-reload state |
| [engine/audio-system.md](engine/audio-system.md) | XAudio2 engine, 4-bus mixing, voice pool, .uzaudio format, spatial audio, Lua playback API |
| [engine/physics-system.md](engine/physics-system.md) | Jolt Physics integration, body/collider creation, collision events, character controller, spatial queries, Lua API |
| [engine/ui-architecture.md](engine/ui-architecture.md) | UI system implementation: uz_ui_html.dll C ABI, UIPass render integration, SDF font rendering, hot-reload, input routing, memory management |

---

## Modules

Internal module documentation describing the purpose, structure, and API of core engine subsystems.

| Module | Description |
|--------|-------------|
| [modules/game/README.md](modules/game/README.md) | Game logic DLL (game.dll): LuaJIT VM, physics step, hot-reload container, game loop coordination |
| [modules/audio/README.md](modules/audio/README.md) | Audio engine: XAudio2, voice pool, 4-bus mixing, 3D spatial audio, WAV/MP3/OGG import |
| [modules/physics/README.md](modules/physics/README.md) | Physics simulation: Jolt Physics, rigid bodies, colliders, character controller, queries, collision events |
| [modules/scripting/README.md](modules/scripting/README.md) | Lua scripting: Per-node LuaJIT VM, C++ bindings, sandboxing, hot-reload, error isolation |
| [modules/assets/README.md](modules/assets/README.md) | Asset pipeline: Import (glTF/FBX/PNG/WAV), ResourceManager, res:// paths, .uzpak format |
| [modules/build/README.md](modules/build/README.md) | Build system: CMake, vcpkg, UzBuild CLI tools, shader compiler integration |
| [modules/editor/README.md](modules/editor/README.md) | VSCode editor extension: IPC server, UI panels, scene editing, asset browser, play controls |
| [modules/engine/README.md](modules/engine/README.md) | Core engine: DX12 renderer, IPC/MCP, lifecycle management, memory, profiling |
| [modules/renderer/README.md](modules/renderer/README.md) | Rendering: Deferred GBuffer, PBR pipeline, shadows, post-process, shader hot-reload |
| [modules/runtime/README.md](modules/runtime/README.md) | Runtime: Scene loading, ECS iteration, physics/animation/audio step coordination |
| [modules/ui/README.md](modules/ui/README.md) | UI system: HTML/CSS game UI, data binding, event callbacks, UIRoot nodes |

---

## Specs

Design and architecture specifications for all engine systems, organized by domain.

**Overview:** [specs/engine-vision.md](specs/engine-vision.md) — Full architecture, ownership model, phase roadmap, tech stack

### Rendering

| Spec | Description |
|------|-------------|
| [specs/rendering/renderer.md](specs/rendering/renderer.md) | Hybrid Deferred PBR pipeline, G-Buffer, shadows, post-process |
| [specs/rendering/render-graph.md](specs/rendering/render-graph.md) | Frame resource abstraction, pass dependencies, automatic D3D12 barrier insertion, transient resource aliasing |
| [specs/rendering/shader-system.md](specs/rendering/shader-system.md) | HLSL/GLSL pipeline, DXC, hot-reload, built-in shaders |
| [specs/rendering/shader-variants.md](specs/rendering/shader-variants.md) | Shader permutation system: variant keys, PSO cache, DXC compilation |
| [specs/rendering/shader-library.md](specs/rendering/shader-library.md) | Shared HLSL include files (BRDF, lighting, shadows, skinning, tonemapping, noise) |
| [specs/rendering/screen-space-effects.md](specs/rendering/screen-space-effects.md) | SSAO (HBAO+), SSR (Hi-Z ray march), TAA (temporal resolve), velocity buffer |
| [specs/rendering/global-illumination.md](specs/rendering/global-illumination.md) | Hybrid GI: static lightmaps + dynamic DDGI probes + reflection captures |
| [specs/rendering/point-spot-light-shadows.md](specs/rendering/point-spot-light-shadows.md) | Point light dual-paraboloid shadows, spot light perspective shadows, shadow atlases |
| [specs/rendering/gpu-instancing.md](specs/rendering/gpu-instancing.md) | Dynamic GPU instancing: BatchBuilder, StructuredBuffer transforms, SV_InstanceID |
| [specs/rendering/particle-system.md](specs/rendering/particle-system.md) | GPU compute particle simulation, `.uzfx` format, behavior modules, LOD |

### Gameplay

| Spec | Description |
|------|-------------|
| [specs/gameplay/scene-system.md](specs/gameplay/scene-system.md) | Node types, `.uzscene` format, SceneManager API, entity pooling, schema evolution |
| [specs/gameplay/gameplay-systems.md](specs/gameplay/gameplay-systems.md) | System Scheduler (TickGroups), Gameplay Ability System, FSM, Behavior Trees |
| [specs/gameplay/spatial-systems.md](specs/gameplay/spatial-systems.md) | BVH / Octree spatial indexing, O(log n) queries, physics broad-phase integration |
| [specs/gameplay/event-bus.md](specs/gameplay/event-bus.md) | C++ typed EventBus<T> + Lua string-dispatch signals, predefined engine events |
| [specs/gameplay/tags-groups.md](specs/gameplay/tags-groups.md) | Node tags (bitset<64>), find_by_tag queries, Lua API |
| [specs/gameplay/timer-system.md](specs/gameplay/timer-system.md) | Lua timer API: schedule_after, schedule_every, cancel_timer |
| [specs/gameplay/input-system.md](specs/gameplay/input-system.md) | Enhanced Input (InputAction + MappingContext), XInput, rebinding |
| [specs/gameplay/camera-system.md](specs/gameplay/camera-system.md) | SpringArm, camera modes, shake, DoF, `.uzcam` presets |
| [specs/gameplay/save-system.md](specs/gameplay/save-system.md) | Save/load game state, msgpack serialization, save slots, autosave, Lua API |
| [specs/gameplay/localization.md](specs/gameplay/localization.md) | I18n via CSV, multi-language support, runtime locale switching, font fallback |
| [specs/gameplay/navmesh.md](specs/gameplay/navmesh.md) | Recast + Detour navmesh baking, NavMeshAgent, RVO2 local avoidance |
| [specs/gameplay/data-tables.md](specs/gameplay/data-tables.md) | Typed row-based game data (items, enemies, abilities, loot tables); `.uztable` format, Table Editor panel, Lua API, CSV import/export, AI row generation |

### Physics

| Spec | Description |
|------|-------------|
| [specs/physics/physics-system.md](specs/physics/physics-system.md) | Jolt Physics rigid bodies, colliders, triggers, character controller, physics materials, CCD |
| [specs/physics/vehicle-physics.md](specs/physics/vehicle-physics.md) | Multi-wheel vehicle simulation: VehicleConstraint, suspension, drivetrain, Ackermann steering |
| [specs/physics/cloth-simulation.md](specs/physics/cloth-simulation.md) | Cloth via Jolt SoftBody: vertex constraints, skeleton blending, wind zones, LOD |
| [specs/physics/destructibles.md](specs/physics/destructibles.md) | Destructible objects: Voronoi fracture, impulse-based breaking, debris pooling |
| [specs/physics/terrain-system.md](specs/physics/terrain-system.md) | `.uzterrain` + heightmap, splatmap, chunk LOD, foliage GPU instancing, water plane |

### Audio

| Spec | Description |
|------|-------------|
| [specs/audio/audio-system.md](specs/audio/audio-system.md) | XAudio2 engine, 4-bus mixing, spatial 3D audio, audio events, occlusion, DSP chains |
| [specs/audio/audio-snapshots.md](specs/audio/audio-snapshots.md) | Mixer presets, per-bus overrides, stack-based blending, state-driven transitions |
| [specs/audio/dialogue-system.md](specs/audio/dialogue-system.md) | Dialogue trees, voice acting pipeline, subtitle timing, localized audio, Lua callbacks |

### Scripting

| Spec | Description |
|------|-------------|
| [specs/scripting/lua-scripting.md](specs/scripting/lua-scripting.md) | LuaJIT per-node scripts, coroutines, modules, shared state, hot-reload (~60ms), sandboxing |
| [specs/scripting/shared-state.md](specs/scripting/shared-state.md) | Global game state store, reactive bindings for UI/Audio, change notifications, save/load |
| [specs/scripting/async-streaming.md](specs/scripting/async-streaming.md) | Async asset loading, AssetHandle API, IO thread pool, streaming memory budgets, level streaming |
| [specs/scripting/data-driven-config.md](specs/scripting/data-driven-config.md) | Unified config framework: `.uzevent`, `.uzphysmat`, `.uzentity`, `.uzflow`; hot-reload, validation |

### Editor

| Spec | Description |
|------|-------------|
| [specs/editor/editor-panels.md](specs/editor/editor-panels.md) | Scene Tree, Inspector, Asset Browser, Play Controls |
| [specs/editor/editor-gizmos.md](specs/editor/editor-gizmos.md) | Translate/Rotate/Scale 3D handles, ray-cast hit test, drag mechanics, grid snap |
| [specs/editor/multi-select.md](specs/editor/multi-select.md) | SelectionManager, Ctrl+click, range select, shared Inspector, bulk operations |
| [specs/editor/prefab-edit-mode.md](specs/editor/prefab-edit-mode.md) | Prefab isolation, override panel, save/reload instances, state machine |
| [specs/editor/undo-redo.md](specs/editor/undo-redo.md) | Command pattern, UndoStack, debounce, dirty tracking, Ctrl+Z/Ctrl+Shift+Z |
| [specs/editor/console-commands.md](specs/editor/console-commands.md) | In-game debug console, command registration API (Lua + C++), auto-complete |
| [specs/vscode-syntax-highlighting.md](specs/vscode-syntax-highlighting.md) | TextMate grammars for all `.uz*` formats, HLSL, Lua, and Markdown code fence injection |

### Engine Core

| Spec | Description |
|------|-------------|
| [specs/engine/memory-system.md](specs/engine/memory-system.md) | Arena allocators, ownership model, handle system |
| [specs/engine/debug-profiling.md](specs/engine/debug-profiling.md) | Assert system, Tracy CPU/GPU profiler, ImGui overlay, doctest unit tests |
| [specs/engine/crash-reporting.md](specs/engine/crash-reporting.md) | Unhandled exception capture, minidump, crash log JSON, system info, IPC integration |
| [specs/engine/build-system.md](specs/engine/build-system.md) | Cook → Pack → Compile → Stage → Package pipeline, NSIS, platform abstraction |
| [specs/engine/plugin-system.md](specs/engine/plugin-system.md) | `.uzplugin` manifest, stable C ABI, VSCode extension side, IPC namespace, hot-reload |
| [specs/engine/ai-native-features.md](specs/engine/ai-native-features.md) | Prompt-to-Scene, shader co-authoring, natural language property editing, script scaffolding |

### UI

| Spec | Description |
|------|-------------|
| [specs/ui/ui-system.md](specs/ui/ui-system.md) | HTML/CSS game UI, data binding, gamepad focus/navigation, localization, UIRoot nodes |
| [specs/ui/ui-widget-library.md](specs/ui/ui-widget-library.md) | uz-button, uz-slider, uz-checkbox, uz-dropdown, uz-scroll-view, uz-modal, uz-toast, etc. |
| [specs/ui/ui-animation.md](specs/ui/ui-animation.md) | Tweening API, easing curves, sequences, page transitions |

### Assets

| Spec | Description |
|------|-------------|
| [specs/assets/asset-pipeline.md](specs/assets/asset-pipeline.md) | FBX/GLTF import, LOD, texture compression, `.uzpak` format, source asset DB, dependency graph |
| [specs/assets/skeletal-mesh.md](specs/assets/skeletal-mesh.md) | Skeleton, skinned mesh, GPU skinning, morph targets, physics asset |
| [specs/assets/animation-system.md](specs/assets/animation-system.md) | AnimClip, state machine, blending, AnimNotify, IK, root motion |

---

## Guides

Implementation documentation for the current JPEG streaming PoC.

| Guide | Description |
|-------|-------------|
| [guides/getting-started.md](guides/getting-started.md) | First-run setup |
| [guides/audio-guide.md](guides/audio-guide.md) | Audio implementation guide: playback, 3D positioning, buses, events, adaptive music, snapshots, dialogue integration |
| [guides/gameplay-programming-guide.md](guides/gameplay-programming-guide.md) | Practical guide to building gameplay: script anatomy, coroutines for sequences, FSM for characters, Gameplay Ability System, spatial queries, EventBus patterns, common gameplay patterns (health, pickups, AI) |
| [guides/build.md](guides/build.md) | CMake build, vcpkg dependencies |
| [guides/architecture.md](guides/architecture.md) | Current system: DX12 + JPEG stream + VSCode |
| [guides/streaming-pipeline.md](guides/streaming-pipeline.md) | Frame lifecycle: GPU → JPEG → WebSocket → VSCode |
| [guides/ipc-mcp.md](guides/ipc-mcp.md) | IPC protocol, MCP tools, AI agent usage |
| [guides/vscode-extension.md](guides/vscode-extension.md) | Extension commands, ServerManager, StreamPanel |
| [guides/ai-interaction.md](guides/ai-interaction.md) | Controlling the engine from Claude Code |
| [guides/scene-authoring.md](guides/scene-authoring.md) | Creating and editing .uzscene files, node types, properties, prefabs |
| [guides/shader-authoring.md](guides/shader-authoring.md) | Writing HLSL shaders, include library, hot-reload, G-Buffer layout, variants |
| [guides/editor-workflow.md](guides/editor-workflow.md) | VSCode editor panels, gizmos, play mode, prefab editing, debug console |
| [guides/testing-guide.md](guides/testing-guide.md) | Writing tests with Google Test, mocks, fixtures, coverage requirements (95%+) |
| [guides/ai-development-guide.md](guides/ai-development-guide.md) | AI-assisted development with Claude, MCP tools, prompts, scene/script/shader co-authoring |
| [guides/ui-guide.md](guides/ui-guide.md) | UI development: creating HUDs and menus with HTML/CSS/Lua, data binding, widgets, animations, keyboard/gamepad navigation, world-space UI, patterns and best practices |
| [guides/physics-advanced-guide.md](guides/physics-advanced-guide.md) | Advanced physics: vehicle setup (cars, trucks), cloth integration (capes, hair), destructible objects (crates, vases), custom constraints (doors, ropes), debugging, performance optimization |
| [guides/rendering-guide.md](guides/rendering-guide.md) | PBR materials, lighting setup (directional/point/spot), post-processing (bloom/SSAO/SSR/TAA), global illumination, decals, performance tuning |
| [guides/streaming-guide.md](guides/streaming-guide.md) | Async loading patterns, level streaming with chunks, memory budgets, preloading strategy, debug overlay, best practices |

---

## Gap Analysis & Reviews

Expert reviews of current specifications against AAA-quality standards. Read these to understand what's missing and why.

| Review | Description |
|--------|-------------|
| [reviews/MASTER-GAP-ANALYSIS.md](reviews/MASTER-GAP-ANALYSIS.md) | Consolidated master analysis of 137 gaps across 7 domains. Executive summary, prioritization (P0–P2), cross-cutting concerns, architectural decisions needed before Phase 1, and recommended Rulebook tasks. Start here. |
| [reviews/ecs-systems-review.md](reviews/ecs-systems-review.md) | 14 critical gaps in ECS and game systems (world streaming, spatial queries, entity pooling, system order, prefabs, hot-reload) |
| [reviews/physics-review.md](reviews/physics-review.md) | 16 critical gaps in physics (materials system, CCD, constraints, vehicles, destruction, cloth, soft bodies) |
| [reviews/audio-review.md](reviews/audio-review.md) | 18 critical gaps in audio (event system, adaptive music, DSP chains, occlusion, dialogue, middleware) |
| [reviews/lua-scripting-review.md](reviews/lua-scripting-review.md) | 17 critical gaps in Lua (coroutines, DAP debugging, profiler, modules, serialization, type-safe bindings) |
| [reviews/rendering-review.md](reviews/rendering-review.md) | 28 critical gaps in rendering (GI, Nanite, VSM, ray tracing, decals, bloom, DoF, volumetric fog, water, SSS, hair, eye, VRS, mesh shaders, GPU-driven rendering) |
| [reviews/asset-pipeline-review.md](reviews/asset-pipeline-review.md) | 24 critical gaps in assets (source DB, dependency tracking, streaming, virtual texturing, cross-platform variants, atlasing) |
| [reviews/ui-review.md](reviews/ui-review.md) | 20 critical gaps in UI (gamepad navigation, widget library, animation, binding, state machine, safe area, drag-drop, accessibility) |

---

## Phase Roadmap

| Phase | Tasks | Focus | Status |
|-------|-------|-------|--------|
| **0** | — | DX12 renderer, JPEG stream, IPC/MCP | ✅ Done |
| **0-PRE** | 5 | Architecture design specs (Render Graph, Data-Driven, Debug, Async Streaming, Shared State) | Planned |
| **1** | 23 | Editor funcional, Lua scripting, ECS core, render graph, crash reporting | Planned |
| **2** | 24 | PBR rendering, glTF/FBX import, audio, UI system, asset pipeline | Planned |
| **3** | 20 | Skeletal mesh, animation, physics, particle system, save system, GI | Planned |
| **4** | 8 | Build pipeline, NavMesh, terrain, plugins, destructibles, cloth, virtual texturing | Planned |

---

## Rulebook Tasks

Implementation tasks are tracked in `.rulebook/tasks/`. Run `rulebook task list` to see current status.

See [DEVELOPMENT-PLAN.md](DEVELOPMENT-PLAN.md) for the full sequential implementation order.

### Phase 0-PRE — Architecture Design (5 tasks)

| Task ID | Description |
|---------|-------------|
| `phase0-render-graph-design` | Render Graph architecture spec: RgHandle, RgPass, barrier automation |
| `phase0-data-driven-framework` | Data-driven config framework spec: `.uzevent`, `.uzphysmat`, `.uzentity`, `.uzflow` |
| `phase0-debug-infrastructure-design` | Debug infrastructure spec: Tracy, ImGui overlay, crash reporting, structured logging |
| `phase0-async-streaming-design` | Async streaming spec: AssetHandle<T>, IO thread pool, level streaming, memory budgets |
| `phase0-shared-state-design` | Shared state spec: GameState store, reactive bindings, save/load serialization |

### Phase 1 — Editor Funcional (23 tasks)

| Task ID | Description |
|---------|-------------|
| `phase1-scene-system` | Node tree, `.uzscene` parser, SceneManager API, entity pooling, schema evolution |
| `phase1-debug-profiling` | Tracy profiler, ImGui overlay, doctest tests, ASAN, assert macros |
| `phase1-crash-reporting` | SEH handler, minidump, JSON crash log, system info, IPC integration |
| `phase1-render-graph` | RgPass/RgTexture, automatic D3D12 barriers, transient resource aliasing, PSO cache |
| `phase1-editor-panels` | VSCode Scene Tree, Inspector, Asset Browser, Play Controls |
| `phase1-asset-browser` | Asset browser panel, import pipeline, thumbnail generation |
| `phase1-editor-gizmos` | 3D translate/rotate/scale handles, ray-cast, drag, grid snap, node picking |
| `phase1-multi-select` | SelectionManager, Ctrl+click, box-select, shared Inspector, bulk operations |
| `phase1-undo-redo` | Command pattern UndoStack (100 entries), Ctrl+Z/Ctrl+Shift+Z, dirty tracking |
| `phase1-console-commands` | In-game debug console (~key), command registration API (Lua + C++), auto-complete |
| `phase1-lua-scripting` | LuaJIT per-node scripts, on_start/on_update/on_stop, sandbox, hot-reload (~60ms) |
| `phase1-lua-coroutines` | `coroutine.yield` across C boundary, co-routine scheduler, LuaJIT NYI handling |
| `phase1-lua-module-system` | `require()` path resolution (res://scripts/lib/), package loader, module cache |
| `phase1-shared-state` | `ctx.shared` global store, reactive bindings for UI/Audio, change notifications |
| `phase1-timer-system` | Lua timer API: schedule_after, schedule_every, cancel_timer, schedule_next_frame |
| `phase1-lua-dap-server` | DAP debugger server, VSCode breakpoints, step/watches, Lua stack frames |
| `phase1-ecs-system-scheduler` | TickGroups, system priorities, dependency DAG, parallel system execution |
| `phase1-ecs-spatial-query` | BVH spatial index, O(log n) query_sphere/raycast, ECS integration |
| `phase1-entity-templates` | `.uzentity` format, template instantiation with property overrides |
| `phase1-shader-hot-reload` | DXC integration, FileWatcher, error display in VSCode, PSO recompile |
| `phase1-play-mode` | Separate play process, start/stop/pause controls, status bar integration |
| `phase1-ui-gamepad-navigation` | FocusManager, data-focusable attribute, d-pad/stick navigation, focus ring |
| `phase1-ui-widget-library` | uz-button, uz-slider, uz-checkbox, uz-dropdown, uz-scroll-view, uz-modal, uz-toast |
| `phase1-vscode-syntax-highlighting` | TextMate grammars for all `.uz*` formats, HLSL, Lua, and Markdown code fence injection |

### Phase 2 — Visual + Assets + Audio (24 tasks)

| Task ID | Description |
|---------|-------------|
| `phase2-pbr-renderer` | G-Buffer, GGX BRDF, IBL, deferred lighting, PBR material properties |
| `phase2-shadow-maps` | 3-cascade CSM, PCF 3×3, cascade blending, depth bias |
| `phase2-point-spot-shadows` | Point light dual-paraboloid + spot light perspective shadows, shadow atlases |
| `phase2-ssao` | HBAO+ compute shader, bilateral blur, ambient lighting integration, quality levels |
| `phase2-render-postprocess` | Bloom, tone mapping, auto-exposure, vignette, color LUT |
| `phase2-deferred-decals` | DBuffer decal system, box projection, bullet holes, dirt overlays |
| `phase2-gpu-instancing` | Dynamic GPU instancing, BatchBuilder, StructuredBuffer transforms |
| `phase2-shader-variants` | Shader permutation system, variant keys, PSO cache, offline DXC compilation |
| `phase2-shader-library` | Shared HLSL include library, GlobalParams cbuffer at b15, include dependency tracking |
| `phase2-gltf-import` | fastgltf parser, meshoptimizer LODs, MeshBuffer, material conversion |
| `phase2-fbx-import` | ufbx parser, FBX → `.uzmesh`, skeleton extraction, model preview panel |
| `phase2-texture-tools` | DirectXTex, BC7/BC5/BC6H compression, mip generation, texture preview panel |
| `phase2-asset-pak` | `.uzpak` format, zstd compression, VFS mount, CLI packer tool |
| `phase2-source-asset-db` | SQLite asset database, CookKey, dependency graph, incremental import |
| `phase2-audio-system` | XAudio2 engine, 4-bus mixing, WAV/MP3/OGG, CAudioSource/Listener ECS, Lua API |
| `phase2-audio-event-system` | `.uzevent` data-driven audio, random variations, parameter mapping |
| `phase2-audio-dsp-chains` | XAPO DSP chains per bus: reverb, EQ, compressor |
| `phase2-ui-system` | uz_ui_html.dll C ABI, HTML+CSS game UI, data binding, UIRoot scene nodes |
| `phase2-ui-animation` | Tween API (ctx.ui:tween/sequence), easing curves, page transitions |
| `phase2-prefab-edit-mode` | Prefab isolation viewport, breadcrumb navigation, override panel (blue) |
| `phase2-physics-materials` | `.uzphysmat` format, friction/restitution assets, surface response library |
| `phase2-gameplay-ability-system` | AbilitySpec, cooldowns, costs, tags, EventBus integration |
| `phase2-behavior-trees` | `.uzbt` format, Sequence/Selector/Parallel, Blackboard, tick loop |
| `phase2-ai-native-features` | MCP tools: scene_generate, shader_assist, script_generate, scene_analyze |
| `phase2-data-tables` | `.uztable` format, DataTableManager, Table Editor VSCode panel, Lua API, CSV import/export, AI row generation |

### Phase 3 — Gameplay Completo (20 tasks)

| Task ID | Description |
|---------|-------------|
| `phase3-skeletal-mesh` | GPU skinning, morph targets, physics asset binding |
| `phase3-animation-system` | State machine, blending tree, AnimNotify, FABRIK/Two-bone IK, root motion |
| `phase3-ragdoll` | Jolt ragdoll, active ragdoll motors, blend in/out |
| `phase3-physics-system` | Jolt rigid bodies, triggers, CharacterVirtual, CCD, Lua API, collision events |
| `phase3-vehicle-physics` | VehicleConstraint, suspension, drivetrain, Ackermann steering |
| `phase3-ssr` | Hi-Z ray march SSR, temporal reprojection, IBL fallback resolve |
| `phase3-taa` | TAA resolve, Halton jitter, velocity G-Buffer channel, variance clamping |
| `phase3-virtual-shadow-maps` | VSM atlas, page table, feedback buffer, per-object granularity |
| `phase3-global-illumination` | DDGI probes, static lightmaps, reflection captures, hybrid blending |
| `phase3-particle-system` | GPU compute particles, `.uzfx` format, behavior modules, AnimNotify integration |
| `phase3-input-system` | Enhanced Input (InputAction + MappingContext), XInput, runtime rebinding |
| `phase3-camera-system` | SpringArm, camera modes, shake, DoF, `.uzcam` presets |
| `phase3-event-bus` | C++ EventBus<T>, Lua string-dispatch signals, predefined engine events |
| `phase3-tags-groups` | CTags bitset<64>, TagRegistry, find_by_tag queries, Lua API |
| `phase3-save-system` | Msgpack saves, slots 0–9 + autosave, thumbnails, Lua ctx.saves API |
| `phase3-localization` | CSV i18n, runtime locale switch, font fallback, hot-reload |
| `phase3-world-streaming` | Scene chunks, streaming volumes, async load/unload, memory budget |
| `phase3-adaptive-music` | Stems synchronization, crossfade transitions, MusicClock, quantized bars |
| `phase3-audio-occlusion` | Jolt raycast-based LPF, smooth lerp, update rate config |
| `phase3-dialogue-system` | `.uzdialogue` format, voice acting pipeline, subtitle timing, ducking, Lua callbacks |

### Phase 4 — Produção (8 tasks)

| Task ID | Description |
|---------|-------------|
| `phase4-build-pipeline` | Cook → Pack → Stage → NSIS installer, platform abstraction |
| `phase4-navmesh` | Recast/Detour baking, NavMeshAgent, DetourCrowd, RVO2, dynamic obstacles |
| `phase4-terrain-system` | `.uzterrain` + heightmap, splatmap, 4-LOD chunks, foliage GPU instancing, water plane |
| `phase4-plugin-system` | `.uzplugin` TOML manifest, stable C ABI, DLL hot-reload, IPC namespace |
| `phase4-destructible-meshes` | Voronoi fracture, `.uzfrac` format, debris pool, DestructibleBrokenEvent |
| `phase4-cloth-simulation` | Jolt SoftBody, skeleton blending, wind zones, cloth LOD |
| `phase4-virtual-texturing` | Page table, tile cache, streaming feedback, shader integration |
| `phase4-visual-scripting` | Node graph editor, Lua code generation, hot-reload integration |
