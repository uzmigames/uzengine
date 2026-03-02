# UzEngine — Development Plan

Ordered implementation sequence. Each step has clear prerequisites and a concrete, testable deliverable.

**Current state:** Phase 0 complete — DX12 pyramid + JPEG streaming + IPC/MCP working.

---

## How to read this document

- **Steps within a phase** can be parallelized across agents/devs unless "Depends on" says otherwise
- **Phases are blocking** — Phase N+1 only starts when Phase N is complete
- Each step has a **verifiable deliverable**
- `[task-id]` points to `.rulebook/tasks/{task-id}/`
- **"Depends on" column** — lists step numbers or "RHI" (shorthand for `phase0-rhi-design`)
- **RHI rule** — all rendering code above `rhi_dx12.c` / `rhi_vulkan.c` / `rhi_metal.c` uses only `rhi()->*` calls. No `ID3D12*`, `VkCmd*`, or MTL types leak into higher layers.

---

## Phase 0-PRE — Design Before Coding
> Goal: Make architectural decisions that, if deferred, cost weeks of refactoring.

| # | Step | Deliverable | Task |
|---|------|-------------|------|
| 0.1 | **RHI Design** | `docs/specs/rendering/rhi.md` finalized + DX12 pyramid ported to RHI API | `phase0-rhi-design` |
| 0.2 | Render Graph Design | `docs/specs/rendering/render-graph.md` finalized (must use RHI handles, not DX12) | `phase0-render-graph-design` |
| 0.3 | Data-Driven Framework Design | `docs/specs/scripting/data-driven-config.md` finalized | `phase0-data-driven-framework` |
| 0.4 | Debug Infrastructure Design | `docs/specs/engine/debug-infrastructure.md` finalized | `phase0-debug-infrastructure-design` |
| 0.5 | Async Streaming Design | `docs/specs/scripting/async-streaming.md` finalized | `phase0-async-streaming-design` |
| 0.6 | Shared State Design | `docs/specs/scripting/shared-state.md` finalized | `phase0-shared-state-design` |

> ⚠️ **0.1 RHI blocks 0.2 Render Graph** — the Render Graph must be designed on top of RHI handles (not DX12 types). All Phase 1+ rendering code must use only the RHI API.

**Output:** 6 architecture specs approved. Only the RHI port (0.1) writes C code — everything else is design only.

---

## Phase 1 — Playable Engine in the Editor
> Goal: Open a scene in the editor, run a Lua script, see the result in real time.
>
> **Prerequisite:** Phase 0-PRE complete — RHI API defined and DX12 backend working (`phase0-rhi-design`).
> All rendering code in this phase uses only `rhi()→*` calls. No direct DX12 includes outside `rhi_dx12.c`.

### Block 1A — Engine Foundation (nothing else works without this)

| # | Step | Deliverable | Depends on | Task |
|---|------|-------------|------------|------|
| 1.1 | Scene system (`.uzscene`) | SceneManager, node tree, working serialization | — | `phase1-scene-system` |
| 1.2 | Profiling and debug | Tracy integrated, asserts, ImGui overlay | — | `phase1-debug-profiling` |
| 1.3 | Crash reporting | SEH handler, minidump, crash JSON | — | `phase1-crash-reporting` |
| 1.4 | Render Graph | RgPass/RgTexture, automatic barriers via RHI; PSO cache lives in RHI (`rhi_common.c`) | RHI (0.1) | `phase1-render-graph` |

> ⚠️ **1.4 Render Graph** operates on `RhiTexture`/`RhiBuffer`/`RhiCmdList` handles exclusively. No D3D12 types (`ID3D12Resource`, `D3D12_RESOURCE_STATES`, etc.) appear above `rhi_dx12.c`. The PSO cache (`pso_cache_*`) is implemented in `rhi_common.c` (part of `phase0-rhi-design`) — the Render Graph calls it via the RHI, does not own it.

### Block 1B — Editor (runs in parallel with 1A after 1.1)

| # | Step | Deliverable | Depends on | Task |
|---|------|-------------|------------|------|
| 1.5 | Editor panels | Scene Tree, Inspector, Asset Browser, Play Controls | 1.1 | `phase1-editor-panels` |
| 1.6 | Asset browser | Asset visualization and import | 1.1 | `phase1-asset-browser` |
| **1.6b** | **Asset drag & drop** | **TreeView re-parent, asset→scene, asset→viewport, OS import** | **1.5, 1.6** | `phase1-asset-drag-drop` |
| 1.7 | 3D Gizmos | Translate/Rotate/Scale with ray cast | 1.4, 1.5 | `phase1-editor-gizmos` |
| 1.8 | Multi-select | SelectionManager, box-select, bulk ops | 1.5 | `phase1-multi-select` |
| 1.9 | Undo/Redo | UndoStack, Ctrl+Z/Shift+Z | 1.5 | `phase1-undo-redo` |
| 1.10 | Debug console | `~` opens console, Lua + C++ commands | 1.5 | `phase1-console-commands` |

> ⚠️ **1.6b Asset Drag & Drop** — fundamental editor UX requiring VSCode-specific workarounds. VSCode WebViews are sandboxed: TreeView→WebView DnD is not natively supported. Solution: `DragDropCoordinator` singleton + postMessage phantom drag for viewport drops; native `TreeDragAndDropController` for tree re-parenting; `FileSystemWatcher` for OS-level file import. See `docs/specs/editor/asset-drag-drop.md`.

### Block 1C — Scripting (runs in parallel with 1B, depends on 1.1)

| # | Step | Deliverable | Depends on | Task |
|---|------|-------------|------------|------|
| 1.11 | LuaJIT scripting | `on_start`/`on_update`/`on_stop`, sandbox, hot-reload | 1.1 | `phase1-lua-scripting` |
| 1.12 | Lua coroutines | `ctx.coroutine.wait()`, integrated scheduler | 1.11 | `phase1-lua-coroutines` |
| 1.13 | Lua module system | Controlled `require()`, path resolution | 1.11 | `phase1-lua-module-system` |
| 1.14 | Shared state | `ctx.shared`, change events, serialization | 1.11 | `phase1-shared-state` |
| 1.15 | Timers | `ctx:schedule_after/every/cancel` | 1.11 | `phase1-timer-system` |
| 1.16 | DAP debugger | Lua breakpoints in VSCode, watches, stack trace | 1.11 | `phase1-lua-dap-server` |

### Block 1D — ECS and Gameplay Core (depends on 1.1 and 1.11)

| # | Step | Deliverable | Depends on | Task |
|---|------|-------------|------------|------|
| 1.17 | System scheduler | TickGroups, priorities, inter-system dependencies | 1.1, 1.11 | `phase1-ecs-system-scheduler` |
| 1.18 | Spatial queries (BVH) | `ctx.world:query_sphere/raycast`, O(log n) | 1.1 | `phase1-ecs-spatial-query` |
| 1.19 | Entity templates | `.uzentity` format, instantiation with overrides | 1.1, 1.11 | `phase1-entity-templates` |

### Block 1E — Editor Quality (depends on 1B complete)

| # | Step | Deliverable | Depends on | Task |
|---|------|-------------|------------|------|
| 1.20 | Shader hot-reload | FileWatcher → DXC → SPIR-V → RHI PSO swap, error in VSCode | RHI (0.1), 1.4 | `phase1-shader-hot-reload` |
| 1.21 | Play mode | Separate process, start/stop/pause | 1.5 | `phase1-play-mode` |
| 1.22 | Gamepad UI navigation | FocusManager, d-pad navigation | 1.5 | `phase1-ui-gamepad-navigation` |
| 1.23 | Widget library | `<uz-button>`, `<uz-slider>`, `<uz-list-view>`, etc. | 1.5 | `phase1-ui-widget-library` |

> ⚠️ **1.20 Shader hot-reload** pipeline: FileWatcher detects `.hlsl` change → DXC recompiles HLSL → SPIR-V → `rhi_common.c` invalidates PSO cache entries → new PSO compiled on next frame via `rhi()->pipeline_create()` → VSCode notified via IPC `shader.reloaded`. No direct DX12 PSO creation outside `rhi_dx12.c`.

**Phase 1 output:** Functional editor in VSCode. Open `.uzscene`, edit nodes, drag assets into the scene, run Lua script, see result live.

---

## Phase 2 — Quality Visual Engine
> Goal: Correct PBR rendering, assets imported from external tools, functional audio.
>
> **Prerequisite:** Phase 1 complete. All rendering blocks require RHI (0.1) + Render Graph (1.4).
> Shader variant permutations produce SPIR-V blobs; RHI backend selects DXIL/SPIR-V/MSL at load time.

### Block 2A — PBR Rendering (depends on 1.4 Render Graph + RHI)

| # | Step | Deliverable | Depends on | Task |
|---|------|-------------|------------|------|
| 2.1 | PBR Renderer | G-Buffer, GGX BRDF, IBL, deferred lighting | 1.4, RHI | `phase2-pbr-renderer` |
| 2.2 | Shadow maps | CSM 3 cascades, PCF 3×3 | 2.1 | `phase2-shadow-maps` |
| 2.3 | Point/spot shadows | Dual-paraboloid (point), perspective (spot), atlas | 2.2 | `phase2-point-spot-shadows` |
| 2.4 | SSAO | HBAO+ compute, bilateral blur | 2.1 | `phase2-ssao` |
| 2.5 | Post-process stack | Bloom, tone mapping, auto exposure, vignette, LUT | 2.1 | `phase2-render-postprocess` |
| 2.6 | Deferred decals | DBuffer, box projection, bullet holes/dirt | 2.1 | `phase2-deferred-decals` |
| 2.7 | GPU instancing | BatchBuilder, StructuredBuffer, SV_InstanceID | 2.1, RHI | `phase2-gpu-instancing` |
| 2.8 | Shader variants | HLSL → SPIR-V permutations; PSO cache keyed by SPIR-V hash in RHI | RHI (0.1), 1.20 | `phase2-shader-variants` |
| 2.9 | Shader library | `shaders/include/*.hlsli`: BRDF, lighting, shadows, skinning | 2.8 | `phase2-shader-library` |

> ⚠️ **2.8 Shader variants** — permutations are SPIR-V blobs stored in `.uzpak`. The PSO cache key is `PsoCacheKey` (SPIR-V hash + RT format hash), defined in `rhi_common.c`. `phase2-shader-variants` uses `pso_cache_get/insert` via the RHI; it does not implement its own cache.

### Block 2B — Asset Pipeline (can run in parallel with 2A)

| # | Step | Deliverable | Depends on | Task |
|---|------|-------------|------------|------|
| 2.10 | glTF import | fastgltf, meshoptimizer LODs, MeshBuffer | — | `phase2-gltf-import` |
| 2.11 | FBX import | ufbx, `.uzmesh`, editor preview | — | `phase2-fbx-import` |
| 2.12 | Texture tools | BC7/BC5/BC6H via DirectXTex, preview | — | `phase2-texture-tools` |
| 2.13 | Asset PAK | `.uzpak` with zstd, VFS, CLI packer; includes SPIR-V + DXIL shader blobs | 2.8 | `phase2-asset-pak` |
| 2.14 | Source asset DB | SQLite, CookKey, dependency graph, incremental import | 2.13 | `phase2-source-asset-db` |

> ⚠️ **2.13 Asset PAK** stores shader blobs produced by `UzBuild --cook`: SPIR-V (canonical) + DXIL (DX12 fast path). At runtime, `rhi_dx12.c` loads DXIL directly; `rhi_vulkan.c` loads SPIR-V; `rhi_metal.c` loads pre-compiled MSL or derives from SPIR-V via SPIRV-Cross.

### Block 2C — Audio (can run in parallel with 2A)

| # | Step | Deliverable | Depends on | Task |
|---|------|-------------|------------|------|
| 2.15 | XAudio2 engine | 4 buses, voice pool, WAV/OGG, CAudioSource/Listener | — | `phase2-audio-system` |
| 2.16 | Audio event system | `.uzevent`, `ctx.audio:trigger()`, random variations | 2.15 | `phase2-audio-event-system` |
| 2.17 | DSP chains per bus | Reverb, EQ, compressor via XAPO | 2.15 | `phase2-audio-dsp-chains` |

### Block 2D — UI and Gameplay (can run in parallel with 2A)

| # | Step | Deliverable | Depends on | Task |
|---|------|-------------|------------|------|
| 2.18 | UI system | uz_ui_html.dll, HTML+CSS game UI, data binding | 1.4, RHI | `phase2-ui-system` |
| 2.19 | UI animation | `ctx.ui:tween/sequence`, easing, page transitions | 2.18 | `phase2-ui-animation` |
| 2.20 | Prefab edit mode | Isolated viewport, breadcrumb, override panel | 1.5 | `phase2-prefab-edit-mode` |
| 2.21 | Physics materials | `.uzphysmat`, reusable friction/restitution | — | `phase2-physics-materials` |
| 2.22 | Gameplay ability system | AbilitySpec, cooldowns, costs, EventBus integration | 1.11 | `phase2-gameplay-ability-system` |
| 2.23 | Behavior trees | `.uzbt`, Sequence/Selector, Blackboard, tick loop | 1.11 | `phase2-behavior-trees` |
| 2.24 | AI-native features | MCP: scene_generate, shader_assist, script_generate | 1.4, RHI | `phase2-ai-native-features` |

**Phase 2 output:** Simple playable game with correct PBR, assets imported from Blender, audio, and UI.

---

## Phase 3 — Complete Game Engine
> Goal: Animated characters, full physics, advanced gameplay systems, dynamic audio.
>
> **Prerequisite:** Phase 2 complete. All rendering blocks continue to use only the RHI API.

### Block 3A — Characters (sequential: mesh → anim → ragdoll)

| # | Step | Deliverable | Depends on | Task |
|---|------|-------------|------------|------|
| 3.1 | Skeletal mesh | GPU skinning HLSL compute via RHI, morph targets, physics asset binding | 2.1, RHI | `phase3-skeletal-mesh` |
| 3.2 | Animation system | State machine, blending, AnimNotify, IK, root motion | 3.1 | `phase3-animation-system` |
| 3.3 | Ragdoll | Jolt ragdoll, active ragdoll motors, blend in/out | 3.2 | `phase3-ragdoll` |

### Block 3B — Full Physics (can run in parallel with 3A)

| # | Step | Deliverable | Depends on | Task |
|---|------|-------------|------------|------|
| 3.4 | Physics system | Jolt rigid bodies, triggers, CharacterController, CCD | 1.1 | `phase3-physics-system` |
| 3.5 | Vehicle physics | VehicleConstraint, suspension, drivetrain, Ackermann | 3.4 | `phase3-vehicle-physics` |

### Block 3C — Advanced Rendering (can run in parallel, depends on 2A + RHI)

| # | Step | Deliverable | Depends on | Task |
|---|------|-------------|------------|------|
| 3.6 | SSR | Hi-Z ray march, temporal reprojection, IBL fallback | 2.1, 1.4, RHI | `phase3-ssr` |
| 3.7 | TAA | Temporal resolve, Halton jitter, variance clamping | 2.1, 1.4, RHI | `phase3-taa` |
| 3.8 | Virtual Shadow Maps | VSM atlas, page table, feedback buffer | 2.2, RHI | `phase3-virtual-shadow-maps` |
| 3.9 | Global Illumination | DDGI probes, offline lightmaps, reflection captures | 2.1, RHI | `phase3-global-illumination` |
| 3.10 | Particle system | GPU compute via `rhi()->cmd_dispatch()`, `.uzfx` format, AnimNotify | 1.4, RHI | `phase3-particle-system` |

> ⚠️ **Block 3C** — all compute shaders (skinning, particles, SSAO, GI probes) dispatch via `rhi()->cmd_dispatch()`. The RHI backend maps this to `ID3D12GraphicsCommandList::Dispatch` (DX12), `vkCmdDispatch` (Vulkan), or Metal compute encoder. No direct dispatch calls outside `rhi_dx12.c`.

### Block 3D — Gameplay Systems (can run in parallel)

| # | Step | Deliverable | Depends on | Task |
|---|------|-------------|------------|------|
| 3.11 | Input system | Enhanced Input, XInput, runtime rebinding | — | `phase3-input-system` |
| 3.12 | Camera system | SpringArm, modes, shake, DoF, `.uzcam` | 1.1 | `phase3-camera-system` |
| 3.13 | Event bus | C++ EventBus<T>, Lua signals, engine events | 1.11 | `phase3-event-bus` |
| 3.14 | Tags & groups | CTags bitset<64>, find_by_tag, Lua API | 1.1 | `phase3-tags-groups` |
| 3.15 | Save system | Msgpack, slots, autosave, thumbnails, Lua API | 1.11 | `phase3-save-system` |
| 3.16 | Localization | CSV i18n, runtime locale switch, font fallback | 1.11 | `phase3-localization` |
| 3.17 | World streaming | Scene chunks, streaming volumes, memory budget | 1.1, 1.4 | `phase3-world-streaming` |

### Block 3E — Advanced Audio (can run in parallel)

| # | Step | Deliverable | Depends on | Task |
|---|------|-------------|------------|------|
| 3.18 | Adaptive music | Synchronized stems, crossfade, quantized transitions | 2.15 | `phase3-adaptive-music` |
| 3.19 | Audio occlusion | Raycast-based LPF, smooth lerp, update rate config | 2.15, 3.4 | `phase3-audio-occlusion` |
| 3.20 | Dialogue system | `.uzdialogue`, voice acting, subtitles, ducking, Lua | 2.15, 1.11 | `phase3-dialogue-system` |

**Phase 3 output:** Engine with animated characters, full physics, dynamic audio, streamable world. Ready for a simple AAA game.

---

## Phase 4 — Production Engine
> Goal: Distribution build, advanced systems (navmesh, terrain, plugins, destruction).
>
> **Prerequisite:** Phase 3 complete. `UzBuild --cook` bakes HLSL → SPIR-V + DXIL into `.uzpak` for all target platforms.

| # | Step | Deliverable | Depends on | Task |
|---|------|-------------|------------|------|
| 4.1 | Build pipeline | Cook (HLSL→SPIR-V→DXIL/MSL) → Pack → Stage → NSIS installer | RHI, 2.13 | `phase4-build-pipeline` |
| 4.2 | NavMesh | Recast/Detour, NavMeshAgent, RVO2, debug draw | 3.4 | `phase4-navmesh` |
| 4.3 | Terrain system | `.uzterrain`, heightmap, splatmap, LOD chunks, water | 2.1, RHI | `phase4-terrain-system` |
| 4.4 | Plugin system | `.uzplugin`, stable C ABI, hot-reload, IPC namespace | — | `phase4-plugin-system` |
| 4.5 | Destructible meshes | Voronoi fracture, debris pool, DestructibleBrokenEvent | 3.4, 2.1 | `phase4-destructible-meshes` |
| 4.6 | Cloth simulation | Jolt SoftBody, skeleton blend, wind zones, LOD | 3.4, 3.1 | `phase4-cloth-simulation` |
| 4.7 | Virtual texturing | Page table, tile cache, streaming, RHI texture upload | RHI, 2.1 | `phase4-virtual-texturing` |
| 4.8 | Visual scripting | Node graph editor, Lua code gen, hot-reload | 1.11 | `phase4-visual-scripting` |

> ⚠️ **4.1 Build pipeline** — `UzBuild --cook` runs DXC on all `.hlsl` files: outputs SPIR-V (canonical), DXIL (DX12 fast path on Windows), and optionally MSL (via SPIRV-Cross, for macOS/iOS distribution). All outputs packed into `.uzpak` alongside mesh/texture assets.

**Phase 4 output:** Complete engine, game exportable for Windows, plugin ecosystem.

---

## Overview

```
Phase 0-PRE │  6 design tasks          │ Architecture defined (RHI port = only code)
Phase 1     │ 24 tasks (5 blocks)      │ Functional editor, Lua running
Phase 2     │ 24 tasks (4 blocks)      │ PBR + assets + audio + UI
Phase 3     │ 20 tasks (5 blocks)      │ Characters + physics + GI + complete gameplay
Phase 4     │  8 tasks                 │ Build + navmesh + terrain + plugins
─────────────────────────────────────────────────────────────────────────────
Total       │ 82 tasks                 │
```

---

## Completion Criteria per Phase

| Phase | Criterion |
|-------|-----------|
| 0-PRE | All 6 specs reviewed and approved; DX12 pyramid running via RHI (no direct DX12 in renderer.c) |
| 1 | Open `.uzscene`, edit in editor, run Lua, see result in real time |
| 2 | Import glTF from Blender, render correct PBR, play audio, show UI |
| 3 | Animated character with physics, navigates the world, saves progress, adaptive audio |
| 4 | `UzBuild --package` generates Windows installer, game runs standalone |
