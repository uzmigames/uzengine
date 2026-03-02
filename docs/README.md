# UzEngine Documentation

UzEngine is an AI-native 3D game engine that runs inside VSCode. All scene data, shaders, and materials are plain-text files designed for LLM generation and editing.

---

## Product Roadmap

**Start here for requirements and goals:** [PRD.md](PRD.md) — Product Requirements Document with vision, success metrics, functional & non-functional requirements for all phases.

**Implementation tasks:** [ROADMAP.md](ROADMAP.md) — 5-phase implementation plan with dependencies, complexity estimates, and "definition of done" for each task.

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
| [engine/scripting-system.md](engine/scripting-system.md) | LuaJIT VM lifecycle, script instances, sandboxing, C++ bindings, metatables, ctx table, hot-reload state |
| [engine/audio-system.md](engine/audio-system.md) | XAudio2 engine, 4-bus mixing, voice pool, .uzaudio format, spatial audio, Lua playback API |
| [engine/physics-system.md](engine/physics-system.md) | Jolt Physics integration, body/collider creation, collision events, character controller, spatial queries, Lua API |

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

Design and architecture specifications for all engine systems.

| Spec | Description |
|------|-------------|
| [specs/engine-vision.md](specs/engine-vision.md) | Full architecture, ownership model, phase roadmap, tech stack |
| [specs/lua-scripting.md](specs/lua-scripting.md) | LuaJIT scripting per node, engine API, hot-reload (~60ms), sandboxing, shipping |
| [specs/scene-system.md](specs/scene-system.md) | Node types, `.uzscene` format, SceneManager API, IPC handlers |
| [specs/renderer.md](specs/renderer.md) | Deferred PBR pipeline, G-Buffer, shadows, post-process |
| [specs/shader-system.md](specs/shader-system.md) | HLSL/GLSL pipeline, DXC, hot-reload, built-in shaders |
| [specs/memory-system.md](specs/memory-system.md) | Arena allocators, ownership model, handle system |
| [specs/debug-profiling.md](specs/debug-profiling.md) | Assert system, Tracy CPU/GPU profiler, ImGui overlay, doctest unit tests |
| [specs/console-commands.md](specs/console-commands.md) | In-game debug console, command registration API (Lua + C++), IPC handlers, auto-complete |
| [specs/crash-reporting.md](specs/crash-reporting.md) | Unhandled exception capture, minidump, crash log JSON, system info, Lua state, IPC integration |
| [specs/ui-system.md](specs/ui-system.md) | HTML/CSS game UI authoring, data binding with CSS custom properties, event callbacks, UIRoot scene nodes |
| [specs/editor-panels.md](specs/editor-panels.md) | Scene Tree, Inspector, Asset Browser, Play Controls |
| [specs/asset-pipeline.md](specs/asset-pipeline.md) | FBX/GLTF import, LOD, texture compression, `.uzpak` format |
| [specs/skeletal-mesh.md](specs/skeletal-mesh.md) | Skeleton, skinned mesh, GPU skinning, morph targets, physics asset |
| [specs/animation-system.md](specs/animation-system.md) | AnimClip, state machine, blending, AnimNotify, IK, root motion |
| [specs/input-system.md](specs/input-system.md) | Enhanced Input (InputAction + MappingContext), XInput, rebinding |
| [specs/camera-system.md](specs/camera-system.md) | SpringArm, camera modes, shake, DoF, `.uzcam` presets |
| [specs/build-system.md](specs/build-system.md) | Cook → Pack → Compile → Stage → Package pipeline, NSIS, platform abstraction |
| [specs/timer-system.md](specs/timer-system.md) | Lua timer API: schedule_after, schedule_every, cancel_timer, schedule_next_frame |
| [specs/event-bus.md](specs/event-bus.md) | C++ typed EventBus<T> + Lua string-dispatch signals, predefined engine events |
| [specs/tags-groups.md](specs/tags-groups.md) | Node tags (bitset<64>), find_by_tag queries, Lua API, .uzscene format |
| [specs/undo-redo.md](specs/undo-redo.md) | Command pattern, UndoStack, debounce, dirty tracking, Ctrl+Z/Ctrl+Shift+Z |
| [specs/editor-gizmos.md](specs/editor-gizmos.md) | Translate/Rotate/Scale 3D handles, ray-cast hit test, drag mechanics, grid snap |
| [specs/multi-select.md](specs/multi-select.md) | SelectionManager, Ctrl+click, range select, shared Inspector, bulk operations |
| [specs/prefab-edit-mode.md](specs/prefab-edit-mode.md) | Prefab isolation, override panel, save/reload instances, state machine |
| [specs/shader-variants.md](specs/shader-variants.md) | Shader permutation system: variant keys, auto-detection from materials, PSO cache, DXC compilation |
| [specs/screen-space-effects.md](specs/screen-space-effects.md) | SSAO (HBAO+), SSR (Hi-Z ray march), TAA (temporal resolve), velocity buffer, post-process stack order |
| [specs/shader-library.md](specs/shader-library.md) | Shared HLSL include files (BRDF, lighting, shadows, skinning, tonemapping, noise, global cbuffer) |
| [specs/point-spot-light-shadows.md](specs/point-spot-light-shadows.md) | Point light dual-paraboloid shadows, spot light perspective shadows, shadow atlases, PCF 2x2 |
| [specs/gpu-instancing.md](specs/gpu-instancing.md) | Dynamic GPU instancing: batch builder, StructuredBuffer transforms, SV_InstanceID |
| [specs/audio-system.md](specs/audio-system.md) | XAudio2 audio engine, 4-bus mixing, spatial 3D audio, hot-reload, Lua/C++ playback API |
| [specs/particle-system.md](specs/particle-system.md) | GPU compute particle simulation, `.uzfx` format, behavior modules, billboard rendering, LOD |
| [specs/navmesh.md](specs/navmesh.md) | Recast + Detour navmesh baking, NavMeshAgent, RVO2 local avoidance, dynamic obstacles |
| [specs/physics-system.md](specs/physics-system.md) | Jolt Physics rigid bodies, colliders, triggers, character controller, Lua physics API, collision events |
| [specs/plugin-system.md](specs/plugin-system.md) | `.uzplugin` manifest, stable C ABI, VSCode extension side, IPC namespace, hot-reload |
| [specs/terrain-system.md](specs/terrain-system.md) | `.uzterrain` + heightmap, splatmap, chunk LOD, foliage GPU instancing, water plane, collision |
| [specs/ai-native-features.md](specs/ai-native-features.md) | Prompt-to-Scene, shader co-authoring, natural language property editing, scene analysis, script scaffolding |
| [specs/save-system.md](specs/save-system.md) | Save/load game state, msgpack serialization, save slots, autosave, thumbnail capture, Lua API |
| [specs/localization.md](specs/localization.md) | I18n via CSV, multi-language support, runtime locale switching, font fallback, hot-reload |

---

## Guides

Implementation documentation for the current JPEG streaming PoC.

| Guide | Description |
|-------|-------------|
| [guides/getting-started.md](guides/getting-started.md) | First-run setup |
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

| Phase | Focus | Status |
|-------|-------|--------|
| **0** | DX12 renderer, JPEG stream, IPC/MCP | ✅ Done |
| **1** | Scene system, editor panels, play mode, shader hot-reload, asset browser | Planned |
| **2** | PBR renderer, GLTF/FBX import, shadow maps, texture tools, asset PAK, AI-native features | Planned |
| **3** | Skeletal mesh, animation, ragdoll, input system, camera system, particle system | Planned |
| **4** | Build pipeline, shipping binary, Windows installer, NavMesh, plugin system, terrain system | Planned |
| **5** | Networking (rollback netcode), world partition / streaming, Cinemachine, Dialogue, Tilemap | Planned |

---

## Rulebook Tasks

Implementation tasks are tracked in `.rulebook/tasks/`. Run `rulebook task list` to see current status.

| Task ID | Phase | Description |
|---------|-------|-------------|
| `phase1-debug-profiling` | 1 | Tracy profiler, ImGui overlay, doctest tests, ASAN, assert macros |
| `phase1-scene-system` | 1 | Node tree, `.uzscene` parser, Scene Manager |
| `phase1-editor-panels` | 1 | VSCode Scene Tree, Inspector, Asset Browser |
| `phase1-play-mode` | 1 | Separate play process, status bar controls |
| `phase1-shader-hot-reload` | 1 | DXC integration, file watcher, error display |
| `phase1-asset-browser` | 1 | Asset browser panel, import pipeline |
| `phase2-pbr-renderer` | 2 | G-Buffer, GGX BRDF, IBL, post-process |
| `phase2-gltf-import` | 2 | fastgltf, meshoptimizer LODs, MeshBuffer |
| `phase2-shadow-maps` | 2 | 3-cascade CSM, PCF 3×3, cascade blending |
| `phase2-fbx-import` | 2 | ufbx, FBX → `.uzmesh`, model preview panel |
| `phase2-texture-tools` | 2 | DirectXTex, BC7/BC5/BC6H, texture preview panel |
| `phase2-asset-pak` | 2 | `.uzpak` format, zstd, VFS, CLI packer |
| `phase3-skeletal-mesh` | 3 | GPU skinning, morph targets, physics asset |
| `phase3-animation-system` | 3 | State machine, blending, notifies, IK |
| `phase3-physics-system` | 3 | Jolt rigid bodies, colliders, triggers, character controller, Lua API, collision events |
| `phase3-ragdoll` | 3 | Jolt ragdoll, active ragdoll motors |
| `phase3-input-system` | 3 | Enhanced Input, XInput, runtime rebinding |
| `phase3-camera-system` | 3 | SpringArm, modes, shake, DoF |
| `phase1-timer-system` | 1 | Lua timer API: schedule_after, schedule_every, cancel_timer, schedule_next_frame |
| `phase3-event-bus` | 3 | C++ EventBus<T>, Lua string-dispatch signals, predefined engine events |
| `phase3-tags-groups` | 3 | Node tags, CTags ECS component, find_by_tag queries, IPC handlers |
| `phase3-save-system` | 3 | Save/load game state, msgpack serialization, save slots, autosave, thumbnails, Lua API |
| `phase3-localization` | 3 | CSV-based i18n, multi-language support, runtime locale switching, font fallback, hot-reload |
| `phase4-build-pipeline` | 4 | Cook, pack, shipping binary, NSIS, platform abstraction |
| `phase1-undo-redo` | 1 | Command pattern UndoStack, Ctrl+Z/Ctrl+Shift+Z, dirty tracking, debounce |
| `phase1-editor-gizmos` | 1 | 3D translate/rotate/scale handles, ray-cast, drag, grid snap, node picking |
| `phase1-multi-select` | 1 | SelectionManager, bulk Inspector, box-select, bulk delete/duplicate |
| `phase2-prefab-edit-mode` | 2 | Prefab isolation mode, override panel, save/reload instances |
| `phase2-shader-variants` | 2 | Shader variant/permutation system, variant keys, PSO cache, offline compilation |
| `phase2-shader-library` | 2 | Shared HLSL include library, GlobalParams cbuffer, include dependency tracking |
| `phase2-point-spot-shadows` | 2 | Point light dual-paraboloid + spot light perspective shadows, shadow atlases |
| `phase2-gpu-instancing` | 2 | Dynamic GPU instancing, BatchBuilder, StructuredBuffer transforms |
| `phase2-audio-system` | 2 | XAudio2 engine, 4-bus mixing, WAV/MP3/OGG import, CAudioSource/Listener ECS, Lua API, IPC handlers |
| `phase2-ssao` | 2 | HBAO+ compute shader, bilateral blur, ambient lighting integration, quality levels |
| `phase2-ai-native-features` | 2 | MCP tools: scene_generate, shader_assist, script_generate, scene_analyze, ai.context |
| `phase3-taa` | 3 | TAA resolve, Halton jitter, velocity G-Buffer channel, variance clamping, sharpening |
| `phase3-ssr` | 3 | Hi-Z ray march SSR, temporal reprojection, IBL fallback resolve |
| `phase3-particle-system` | 3 | GPU compute particles, `.uzfx` format, behavior modules, VFX system, AnimNotify integration |
| `phase4-navmesh` | 4 | Recast/Detour baking, NavMeshAgent, DetourCrowd, RVO2, dynamic obstacles, debug draw |
| `phase4-plugin-system` | 4 | `.uzplugin` manifest, stable C ABI plugin API, VSCode extension side, DLL hot-reload |
| `phase4-terrain-system` | 4 | `.uzterrain` + heightmap, splatmap, chunk LOD, foliage GPU instancing, water plane |
