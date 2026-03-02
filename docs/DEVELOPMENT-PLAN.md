# UzEngine — Development Plan

Ordered implementation sequence. Each step has clear prerequisites and a concrete, testable deliverable.

**Current state:** Phase 0 complete — DX12 pyramid + JPEG streaming + IPC/MCP working.

---

## How to read this document

- **Steps within a phase** can be parallelized across agents/devs
- **Phases are blocking** — Phase N+1 only starts when Phase N is complete
- Each step has a **verifiable deliverable**
- `[task-id]` points to `.rulebook/tasks/{task-id}/`

---

## Phase 0-PRE — Design Before Coding
> Goal: Make architectural decisions that, if deferred, cost weeks of refactoring.

| # | Step | Deliverable | Task |
|---|------|-------------|------|
| 0.1 | Render Graph Design | `docs/specs/rendering/render-graph.md` finalized | `phase0-render-graph-design` |
| 0.2 | Data-Driven Framework Design | `docs/specs/scripting/data-driven-config.md` finalized | `phase0-data-driven-framework` |
| 0.3 | Debug Infrastructure Design | `docs/specs/engine/debug-infrastructure.md` finalized | `phase0-debug-infrastructure-design` |
| 0.4 | Async Streaming Design | `docs/specs/scripting/async-streaming.md` finalized | `phase0-async-streaming-design` |
| 0.5 | Shared State Design | `docs/specs/scripting/shared-state.md` finalized | `phase0-shared-state-design` |

**Output:** 5 architecture specs approved. No code written yet.

---

## Phase 1 — Playable Engine in the Editor
> Goal: Open a scene in the editor, run a Lua script, see the result in real time.

### Block 1A — Engine Foundation (nothing else works without this)

| # | Step | Deliverable | Task |
|---|------|-------------|------|
| 1.1 | Scene system (`.uzscene`) | SceneManager, node tree, working serialization | `phase1-scene-system` |
| 1.2 | Profiling and debug | Tracy integrated, asserts, ImGui overlay | `phase1-debug-profiling` |
| 1.3 | Crash reporting | SEH handler, minidump, crash JSON | `phase1-crash-reporting` |
| 1.4 | Render Graph | RgPass/RgTexture, automatic barriers, PSO cache | `phase1-render-graph` |

### Block 1B — Editor (runs in parallel with 1A after 1.1)

| # | Step | Deliverable | Task |
|---|------|-------------|------|
| 1.5 | Editor panels | Scene Tree, Inspector, Asset Browser, Play Controls | `phase1-editor-panels` |
| 1.6 | Asset browser | Asset visualization and import | `phase1-asset-browser` |
| 1.7 | 3D Gizmos | Translate/Rotate/Scale with ray cast | `phase1-editor-gizmos` |
| 1.8 | Multi-select | SelectionManager, box-select, bulk ops | `phase1-multi-select` |
| 1.9 | Undo/Redo | UndoStack, Ctrl+Z/Shift+Z | `phase1-undo-redo` |
| 1.10 | Debug console | `~` opens console, Lua + C++ commands | `phase1-console-commands` |

### Block 1C — Scripting (runs in parallel with 1B, depends on 1.1)

| # | Step | Deliverable | Task |
|---|------|-------------|------|
| 1.11 | LuaJIT scripting | `on_start`/`on_update`/`on_stop`, sandbox, hot-reload | `phase1-lua-scripting` |
| 1.12 | Lua coroutines | `ctx.coroutine.wait()`, integrated scheduler | `phase1-lua-coroutines` |
| 1.13 | Lua module system | Controlled `require()`, path resolution | `phase1-lua-module-system` |
| 1.14 | Shared state | `ctx.shared`, change events, serialization | `phase1-shared-state` |
| 1.15 | Timers | `ctx:schedule_after/every/cancel` | `phase1-timer-system` |
| 1.16 | DAP debugger | Lua breakpoints in VSCode, watches, stack trace | `phase1-lua-dap-server` |

### Block 1D — ECS and Gameplay Core (depends on 1.1 and 1.11)

| # | Step | Deliverable | Task |
|---|------|-------------|------|
| 1.17 | System scheduler | TickGroups, priorities, inter-system dependencies | `phase1-ecs-system-scheduler` |
| 1.18 | Spatial queries (BVH) | `ctx.world:query_sphere/raycast`, O(log n) | `phase1-ecs-spatial-query` |
| 1.19 | Entity templates | `.uzentity` format, instantiation with overrides | `phase1-entity-templates` |

### Block 1E — Editor Quality (depends on 1B complete)

| # | Step | Deliverable | Task |
|---|------|-------------|------|
| 1.20 | Shader hot-reload | DXC, FileWatcher, error in VSCode | `phase1-shader-hot-reload` |
| 1.21 | Play mode | Separate process, start/stop/pause | `phase1-play-mode` |
| 1.22 | Gamepad UI navigation | FocusManager, d-pad navigation | `phase1-ui-gamepad-navigation` |
| 1.23 | Widget library | `<uz-button>`, `<uz-slider>`, `<uz-list-view>`, etc. | `phase1-ui-widget-library` |

**Phase 1 output:** Functional editor in VSCode. Open `.uzscene`, edit nodes, run Lua script, see result live.

---

## Phase 2 — Quality Visual Engine
> Goal: Correct PBR rendering, assets imported from external tools, functional audio.

### Block 2A — PBR Rendering (depends on 1.4 Render Graph)

| # | Step | Deliverable | Task |
|---|------|-------------|------|
| 2.1 | PBR Renderer | G-Buffer, GGX BRDF, IBL, deferred lighting | `phase2-pbr-renderer` |
| 2.2 | Shadow maps | CSM 3 cascades, PCF 3×3 | `phase2-shadow-maps` |
| 2.3 | Point/spot shadows | Dual-paraboloid (point), perspective (spot), atlas | `phase2-point-spot-shadows` |
| 2.4 | SSAO | HBAO+ compute, bilateral blur | `phase2-ssao` |
| 2.5 | Post-process stack | Bloom, tone mapping, auto exposure, vignette, LUT | `phase2-render-postprocess` |
| 2.6 | Deferred decals | DBuffer, box projection, bullet holes/dirt | `phase2-deferred-decals` |
| 2.7 | GPU instancing | BatchBuilder, StructuredBuffer, SV_InstanceID | `phase2-gpu-instancing` |
| 2.8 | Shader variants | Permutation system, PSO cache, DXC offline | `phase2-shader-variants` |
| 2.9 | Shader library | HLSL includes: BRDF, lighting, shadows, skinning | `phase2-shader-library` |

### Block 2B — Asset Pipeline (can run in parallel with 2A)

| # | Step | Deliverable | Task |
|---|------|-------------|------|
| 2.10 | glTF import | fastgltf, meshoptimizer LODs, MeshBuffer | `phase2-gltf-import` |
| 2.11 | FBX import | ufbx, `.uzmesh`, editor preview | `phase2-fbx-import` |
| 2.12 | Texture tools | BC7/BC5/BC6H via DirectXTex, preview | `phase2-texture-tools` |
| 2.13 | Asset PAK | `.uzpak` with zstd, VFS, CLI packer | `phase2-asset-pak` |
| 2.14 | Source asset DB | SQLite, CookKey, dependency graph, incremental import | `phase2-source-asset-db` |

### Block 2C — Audio (can run in parallel with 2A)

| # | Step | Deliverable | Task |
|---|------|-------------|------|
| 2.15 | XAudio2 engine | 4 buses, voice pool, WAV/OGG, CAudioSource/Listener | `phase2-audio-system` |
| 2.16 | Audio event system | `.uzevent`, `ctx.audio:trigger()`, random variations | `phase2-audio-event-system` |
| 2.17 | DSP chains per bus | Reverb, EQ, compressor via XAPO | `phase2-audio-dsp-chains` |

### Block 2D — UI and Gameplay (can run in parallel with 2A)

| # | Step | Deliverable | Task |
|---|------|-------------|------|
| 2.18 | UI system | uz_ui_html.dll, HTML+CSS game UI, data binding | `phase2-ui-system` |
| 2.19 | UI animation | `ctx.ui:tween/sequence`, easing, page transitions | `phase2-ui-animation` |
| 2.20 | Prefab edit mode | Isolated viewport, breadcrumb, override panel | `phase2-prefab-edit-mode` |
| 2.21 | Physics materials | `.uzphysmat`, reusable friction/restitution | `phase2-physics-materials` |
| 2.22 | Gameplay ability system | AbilitySpec, cooldowns, costs, EventBus integration | `phase2-gameplay-ability-system` |
| 2.23 | Behavior trees | `.uzbt`, Sequence/Selector, Blackboard, tick loop | `phase2-behavior-trees` |
| 2.24 | AI-native features | MCP: scene_generate, shader_assist, script_generate | `phase2-ai-native-features` |

**Phase 2 output:** Simple playable game with correct PBR, assets imported from Blender, audio, and UI.

---

## Phase 3 — Complete Game Engine
> Goal: Animated characters, full physics, advanced gameplay systems, dynamic audio.

### Block 3A — Characters (sequential: mesh → anim → ragdoll)

| # | Step | Deliverable | Task |
|---|------|-------------|------|
| 3.1 | Skeletal mesh | GPU skinning, morph targets, physics asset binding | `phase3-skeletal-mesh` |
| 3.2 | Animation system | State machine, blending, AnimNotify, IK, root motion | `phase3-animation-system` |
| 3.3 | Ragdoll | Jolt ragdoll, active ragdoll motors, blend in/out | `phase3-ragdoll` |

### Block 3B — Full Physics (can run in parallel with 3A)

| # | Step | Deliverable | Task |
|---|------|-------------|------|
| 3.4 | Physics system | Jolt rigid bodies, triggers, CharacterController, CCD | `phase3-physics-system` |
| 3.5 | Vehicle physics | VehicleConstraint, suspension, drivetrain, Ackermann | `phase3-vehicle-physics` |

### Block 3C — Advanced Rendering (can run in parallel, depends on 2A)

| # | Step | Deliverable | Task |
|---|------|-------------|------|
| 3.6 | SSR | Hi-Z ray march, temporal reprojection, IBL fallback | `phase3-ssr` |
| 3.7 | TAA | Temporal resolve, Halton jitter, variance clamping | `phase3-taa` |
| 3.8 | Virtual Shadow Maps | VSM atlas, page table, feedback buffer | `phase3-virtual-shadow-maps` |
| 3.9 | Global Illumination | DDGI probes, offline lightmaps, reflection captures | `phase3-global-illumination` |
| 3.10 | Particle system | GPU compute, `.uzfx` format, AnimNotify integration | `phase3-particle-system` |

### Block 3D — Gameplay Systems (can run in parallel)

| # | Step | Deliverable | Task |
|---|------|-------------|------|
| 3.11 | Input system | Enhanced Input, XInput, runtime rebinding | `phase3-input-system` |
| 3.12 | Camera system | SpringArm, modes, shake, DoF, `.uzcam` | `phase3-camera-system` |
| 3.13 | Event bus | C++ EventBus<T>, Lua signals, engine events | `phase3-event-bus` |
| 3.14 | Tags & groups | CTags bitset<64>, find_by_tag, Lua API | `phase3-tags-groups` |
| 3.15 | Save system | Msgpack, slots, autosave, thumbnails, Lua API | `phase3-save-system` |
| 3.16 | Localization | CSV i18n, runtime locale switch, font fallback | `phase3-localization` |
| 3.17 | World streaming | Scene chunks, streaming volumes, memory budget | `phase3-world-streaming` |

### Block 3E — Advanced Audio (can run in parallel)

| # | Step | Deliverable | Task |
|---|------|-------------|------|
| 3.18 | Adaptive music | Synchronized stems, crossfade, quantized transitions | `phase3-adaptive-music` |
| 3.19 | Audio occlusion | Raycast-based LPF, smooth lerp, update rate config | `phase3-audio-occlusion` |
| 3.20 | Dialogue system | `.uzdialogue`, voice acting, subtitles, ducking, Lua | `phase3-dialogue-system` |

**Phase 3 output:** Engine with animated characters, full physics, dynamic audio, streamable world. Ready for a simple AAA game.

---

## Phase 4 — Production Engine
> Goal: Distribution build, advanced systems (navmesh, terrain, plugins, destruction).

| # | Step | Deliverable | Task |
|---|------|-------------|------|
| 4.1 | Build pipeline | Cook → Pack → Stage → NSIS installer | `phase4-build-pipeline` |
| 4.2 | NavMesh | Recast/Detour, NavMeshAgent, RVO2, debug draw | `phase4-navmesh` |
| 4.3 | Terrain system | `.uzterrain`, heightmap, splatmap, LOD chunks, water | `phase4-terrain-system` |
| 4.4 | Plugin system | `.uzplugin`, stable C ABI, hot-reload, IPC namespace | `phase4-plugin-system` |
| 4.5 | Destructible meshes | Voronoi fracture, debris pool, DestructibleBrokenEvent | `phase4-destructible-meshes` |
| 4.6 | Cloth simulation | Jolt SoftBody, skeleton blend, wind zones, LOD | `phase4-cloth-simulation` |
| 4.7 | Virtual texturing | Page table, tile cache, streaming, shader integration | `phase4-virtual-texturing` |
| 4.8 | Visual scripting | Node graph editor, Lua code gen, hot-reload | `phase4-visual-scripting` |

**Phase 4 output:** Complete engine, game exportable for Windows, plugin ecosystem.

---

## Overview

```
Phase 0-PRE │  5 design tasks          │ Architecture defined, no code yet
Phase 1     │ 23 tasks (5 blocks)      │ Functional editor, Lua running
Phase 2     │ 24 tasks (4 blocks)      │ PBR + assets + audio + UI
Phase 3     │ 20 tasks (5 blocks)      │ Characters + physics + GI + complete gameplay
Phase 4     │  8 tasks                 │ Build + navmesh + terrain + plugins
─────────────────────────────────────────────────────────────────────────────
Total       │ 80 tasks                 │
```

---

## Completion Criteria per Phase

| Phase | Criterion |
|-------|-----------|
| 0-PRE | All 5 specs reviewed and approved |
| 1 | Open `.uzscene`, edit in editor, run Lua, see result in real time |
| 2 | Import glTF from Blender, render correct PBR, play audio, show UI |
| 3 | Animated character with physics, navigates the world, saves progress, adaptive audio |
| 4 | `UzBuild --package` generates Windows installer, game runs standalone |
