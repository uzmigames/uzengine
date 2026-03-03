# UzEngine — 10-Phase Task Redistribution Plan

**Date**: 2026-03-03
**Status**: Approved
**Scope**: 188 Rulebook tasks across 10 phases + Tools track
**Previous structure**: Phase 0–4 (5 phases, severely overloaded Phase 2/3)
**This structure**: Phase 1–10 + Tools parallel track

> **Important**: Task folder IDs (e.g. `phase2-pbr-renderer`) are NOT renamed.
> The `phase` prefix in a folder name is a legacy tracking label, not the new phase number.
> Only ROADMAP.md and this document track the new phase assignments.

---

## Why Redistribute?

1. Phase 2 had 53 tasks and Phase 3 had 65 tasks — milestones were impossible to sign off.
2. Dependency violations: `phase2-gameplay-ability-system` required `phase3-event-bus` (forward dep).
3. The Tools track had no explicit unlock-phase assignments.
4. No clear MVP milestone — "Phase 4 complete" was not a shippable state.

**Design principle**: A task's new phase is determined solely by its dependency depth, not its legacy prefix.

---

## Critical Dependency Chains

```
Foundation:
  rhi-design → render-graph → pbr-renderer → bindless-resources → gpu-driven-rendering → nanite

Scene:
  scene-system → lua-scripting → timer-system → gameplay-ability-system → game-mode-state-machine

Animation:
  gltf-import → skeletal-mesh → animation-system → ragdoll / motion-warping / aim-offset

Physics:
  scene-system → event-bus → physics-system → vehicle-physics → soft-body / buoyancy

Audio:
  audio-abstraction-layer (design) → audio-system → audio-dsp-chains → audio-hrtf-binaural

Rendering quality:
  pbr-renderer → shadow-maps → taa → ssr → virtual-shadow-maps → dxr-ray-tracing

UI:
  ui-system → ui-animation → ui-hot-reload → ui-state-machine → ui-drag-drop

World:
  physics-system → terrain-system → navmesh → worldgen-graph-compiler

AAA:
  taa → temporal-upsampling-dlss-fsr
  gpu-driven-rendering → nanite-virtual-geometry
  bindless-resources → virtual-texturing
```

---

## Phase 1 — Engine Foundation (18 tasks)

**Goal**: DX12 engine boots, scene loads, streams JPEG to VSCode, IPC works, crash safety.
**Milestone**: "Lights on" — Month 1
**Exit criteria**: `UzEngine.exe --editor` streams viewport; .uzscene loads; crash reporter active.

| # | Task ID | Depends On | Notes |
|---|---------|-----------|-------|
| 1 | phase0-rhi-design | — | Design doc: DX12/Vulkan/Metal vtable API |
| 2 | phase0-render-graph-design | phase0-rhi-design | Design doc: DAG, barriers, transient alloc |
| 3 | phase0-platform-abstraction-layer | — | Design doc: IPlatform (FS, threads, timers) |
| 4 | phase0-debug-infrastructure-design | — | Design doc: logging, Tracy hooks, crash |
| 5 | phase0-shared-state-design | — | Design doc: GameState store API |
| 6 | phase1-scene-system | — | Node tree, .uzscene parser, IPC scene.* |
| 7 | phase1-debug-profiling | — | Tracy, ImGui overlay, IPC engine.status |
| 8 | phase1-crash-reporting | — | SEH, minidump, JSON crash log |
| 9 | phase1-shader-hot-reload | — | DXC integration, file watcher, VSCode diag |
| 10 | phase1-vscode-syntax-highlighting | — | TextMate grammar for .uzscene/.uzmat/.uzlua |
| 11 | phase1-render-graph | phase0-render-graph-design | RgPass DAG, auto barriers, transient alloc |
| 12 | phase1-console-commands | phase1-scene-system | ~ key overlay, IPC console.execute |
| 13 | phase1-editor-panels | phase1-scene-system | Scene Tree + Inspector Webview panels |
| 14 | phase1-play-mode | phase1-scene-system, phase1-editor-panels | --editor / --play process separation |
| 15 | phase1-asset-browser | phase1-editor-panels | File browser, .import.json sidecar display |
| 16 | phase1-asset-drag-drop | phase1-asset-browser | Drag files into Asset Browser |
| 17 | phase1-undo-redo | phase1-editor-panels | EditorCommand + UndoStack(100) |
| 18 | phase1-editor-gizmos | phase1-editor-panels, phase1-scene-system | Translate/Rotate/Scale DX12 overlay pass |

**Parallel streams within Phase 1**:
- Stream A (independent): items 1–5 (design docs), 6 (scene), 7 (debug), 8 (crash), 9 (shaders), 10 (syntax)
- Stream B (after scene-system): items 12–18 (editor features)
- Stream C (after render-graph-design): item 11 (render graph impl)

---

## Phase 2 — Editor Core & Dev Infrastructure (20 tasks)

**Goal**: Full VSCode editor workflow, Lua scripting, entity templates, dev tooling.
**Milestone**: "First playable" — Month 2
**Exit criteria**: Lua script on node, hot-reload, undo/redo full, multi-select, prefab editor.

| # | Task ID | Depends On | Notes |
|---|---------|-----------|-------|
| 1 | phase1-multi-select | phase1-editor-gizmos | Rubber-band, bulk transform, bulk delete |
| 2 | phase1-lua-scripting | phase1-scene-system | LuaJIT VM, ctx.* bindings, sandbox |
| 3 | phase1-script-execution-safety | phase1-lua-scripting | Watchdog + 5M instruction budget |
| 4 | phase1-lua-coroutines | phase1-lua-scripting | Coroutine async/yield support |
| 5 | phase1-lua-module-system | phase1-lua-scripting | require() path resolution |
| 6 | phase1-timer-system | phase1-lua-scripting | ctx:schedule_after/every/cancel_timer |
| 7 | phase1-shared-state | phase1-lua-scripting | GameState key-value store + Lua proxy |
| 8 | phase1-entity-templates | phase1-editor-panels, phase1-undo-redo | Prefab-lite instantiation from Asset Browser |
| 9 | phase2-prefab-edit-mode | phase1-editor-panels, phase1-undo-redo | Isolated viewport, breadcrumb, override panel |
| 10 | phase0-job-system-design | — | Design doc: enkiTS JobDispatcher API |
| 11 | phase0-async-streaming-design | — | Design doc: async IO, streaming arena |
| 12 | phase0-data-driven-framework | — | Design doc: INI schema, .uz* file format |
| 13 | phase0-audio-abstraction-layer | — | Design doc: IAudioEngine + NullAudioEngine |
| 14 | phase1-ecs-system-scheduler | phase1-scene-system | Parallel ECS system registration + ordering |
| 15 | phase1-ecs-spatial-query | phase1-scene-system, phase1-ecs-system-scheduler | BVH spatial queries for ECS |
| 16 | phase2-cvar-developer-config-system | phase1-console-commands | Runtime CVar<T>, CVarRegistry, .uzproj |
| 17 | phase2-frame-pacing-vsync-modes | phase1-render-graph | FLIP_DISCARD, VSync modes, FRL |
| 18 | phase2-simd-math-optimization | — | SIMD vec3/mat4/quat fast paths (SSE/AVX2) |
| 19 | phase2-memory-profiler | phase1-debug-profiling | Arena high-water marks, fragmentation panel |
| 20 | phase2-profiler-instrumentation-api | phase1-debug-profiling, phase2-memory-profiler | UZ_PROFILE_SCOPE, Tracy zones, frame markers |

**Parallel streams**: Design docs (10–13) run immediately. Lua stream (2–9) runs after scene-system. Infrastructure (14–20) runs independently.

---

## Phase 3 — Asset Pipeline (18 tasks)

**Goal**: Import real 3D assets, textures, audio. Content pipeline ready for art teams.
**Milestone**: "Asset import working" — Month 3–4
**Exit criteria**: glTF drag-drop imports mesh+materials; textures auto-BC7; .uzpak archives work; audio loads.

| # | Task ID | Depends On | Notes |
|---|---------|-----------|-------|
| 1 | phase2-pbr-renderer | phase1-render-graph | Deferred G-Buffer, GGX BRDF, tone mapping |
| 2 | phase2-bindless-resources | phase2-pbr-renderer | Bindless descriptor heap — P0 root sig change |
| 3 | phase2-ibl-pipeline | phase2-pbr-renderer | BRDF LUT, irradiance, radiance cubemaps |
| 4 | phase2-shader-library | phase2-pbr-renderer | shaders/include/*.hlsli shared library |
| 5 | phase2-shader-variants | phase2-pbr-renderer, phase2-shader-library | Permutation keys, PSO cache, DXC -D flags |
| 6 | phase2-texture-tools | phase2-pbr-renderer | ispc_texcomp BC7/BC5/BC6H, mips, DDS |
| 7 | phase2-texture-srgb-autodetect | phase2-texture-tools | Filename pattern → sRGB, alpha detection |
| 8 | phase2-texture-bulk-optimizer | phase2-texture-tools, phase2-texture-srgb-autodetect | Batch folder import panel in VSCode |
| 9 | phase2-gltf-import | phase2-pbr-renderer | fastgltf + meshoptimizer LOD |
| 10 | phase2-fbx-import | phase2-gltf-import | ufbx, skeleton/anim metadata |
| 11 | phase2-auto-lod-pipeline | phase2-gltf-import, phase2-fbx-import | Auto 4-level LOD at import time |
| 12 | phase2-asset-pak | phase2-texture-tools, phase2-gltf-import | .uzpak LZ4+CRC, VirtualFilesystem |
| 13 | phase2-source-asset-db | phase2-gltf-import, phase2-fbx-import, phase2-texture-tools | SQLite mtime-based incremental rebuild |
| 14 | phase2-asset-hot-reload-textures-meshes | phase2-source-asset-db | FileWatcher → hot-swap GPU resource |
| 15 | phase2-gpu-instancing | phase2-gltf-import, phase2-pbr-renderer | BatchBuilder, StructuredBuffer<float4x4> |
| 16 | phase2-ssao | phase2-pbr-renderer | HBAO+, bilateral blur, depth reconstruction |
| 17 | phase2-audio-system | phase0-audio-abstraction-layer, phase1-scene-system | XAudio2, 64-voice pool, 4-bus, X3DAudio |
| 18 | phase2-auto-exposure-color-grading | phase2-pbr-renderer | HDR luminance histogram, LUT color grading |

**Parallel streams**: PBR renderer (1) gates rendering tasks. Audio system (17) fully independent. Source asset DB (13) unlocks after importers.

---

## Phase 4 — Rendering Quality (20 tasks)

**Goal**: AAA-quality PBR rendering. Shadows, post-process, occlusion culling, UI system.
**Milestone**: "Render demo quality" — Month 5–6
**Exit criteria**: CSM shadows, bloom/tone-map/SSAO, HZB culling, async compute, UI overlays working.

| # | Task ID | Depends On | Notes |
|---|---------|-----------|-------|
| 1 | phase2-shadow-maps | phase2-pbr-renderer | CSM 4 cascades, atlas 4096×2048 |
| 2 | phase2-point-spot-shadows | phase2-shadow-maps | Dual-paraboloid point, perspective spot |
| 3 | phase2-render-postprocess | phase1-render-graph, phase2-pbr-renderer | Bloom, ACES, vignette, LUT RgPasses |
| 4 | phase2-deferred-decals | phase2-pbr-renderer, phase1-render-graph | DBuffer decal RgPass, CDecal component |
| 5 | phase2-occlusion-culling-hzb | phase2-pbr-renderer | Hi-Z pyramid, GPU visibility query |
| 6 | phase2-render-graph-async-compute | phase1-render-graph | Async queue overlap, barrier scheduling |
| 7 | phase2-ui-system | phase2-pbr-renderer, phase2-shader-variants | UIPass, HTML+CSS layout, SDF fonts |
| 8 | phase2-sdf-font-atlas | phase2-ui-system | msdf-atlas-gen, MSDF HLSL, CText |
| 9 | phase2-ui-animation | phase2-ui-system | CSS transition + keyframe animation |
| 10 | phase2-ui-hot-reload-stable | phase2-ui-system | File watch → UI rebuild without restart |
| 11 | phase2-ui-reference-resolution | phase2-ui-system | Canonical resolution scaler, DPR support |
| 12 | phase2-ui-theme-system | phase2-ui-system | CSS custom property themes, runtime swap |
| 13 | phase2-ui-profiler-devtools | phase2-ui-system, phase2-profiler-instrumentation-api | Layout/paint/composite timing panel |
| 14 | phase2-ai-native-features | phase2-pbr-renderer, phase2-shader-library | MCP: scene_generate, shader_assist, script_generate |
| 15 | phase2-script-intellisense-lsp | phase1-lua-scripting | LuaLS + ctx.d.lua type definitions |
| 16 | phase2-script-linting-static-analysis | phase1-lua-scripting | luacheck, .luacheckrc, CI integration |
| 17 | phase1-lua-dap-server | phase1-lua-scripting | DAP server, breakpoints, variable inspect |
| 18 | phase2-data-tables | phase1-scene-system, phase1-lua-scripting | .uztable CSV/JSON, VSCode grid editor |
| 19 | phase2-physics-multithreading | phase0-job-system-design | Jolt parallel island dispatch + UzJobSystem |
| 20 | phase2-audio-event-system | phase2-audio-system, phase0-data-driven-framework | .uzevent files, AudioEventRegistry |

**Parallel streams**: Shadow maps (1–2) and post-process (3–4) run independently. UI system (7–13) is fully independent. Script tooling (15–17) fully independent.

---

## Phase 5 — Physics, Audio & Gameplay Core (18 tasks)

**Goal**: Jolt Physics, complete audio ecosystem, event bus, input, camera, save system.
**Milestone**: "Playable prototype" — Month 7–8
**Exit criteria**: Character walks with physics, audio plays with spatial, save/load works, input rebindable.

| # | Task ID | Depends On | Notes |
|---|---------|-----------|-------|
| 1 | phase3-event-bus | phase1-lua-scripting | EventBus<T> + Lua string-dispatch |
| 2 | phase3-tags-groups | phase1-scene-system | CTags bitset<64>, find_by_tag queries |
| 3 | phase3-physics-system | phase1-scene-system, phase3-event-bus | Jolt, CRigidBody, CCollider, CharacterVirtual |
| 4 | phase2-physics-materials | phase3-physics-system, phase0-data-driven-framework | .uzphysmat, surface_type → audio/VFX routing |
| 5 | phase2-audio-dsp-chains | phase2-audio-system | Per-bus EQ, reverb, limiter DSP effects |
| 6 | phase2-audio-bus-dynamic-graph | phase2-audio-system, phase2-audio-dsp-chains | Runtime bus graph DAG rewiring |
| 7 | phase2-audio-profiler-metering | phase2-audio-system, phase2-profiler-instrumentation-api | Per-bus VU meters, steal rate graph |
| 8 | phase3-save-system | phase1-scene-system, phase1-lua-scripting | msgpack slots 0-9 + autosave |
| 9 | phase3-localization | phase1-scene-system, phase1-lua-scripting | CSV translations, ctx.locale:t() |
| 10 | phase3-seeded-rng | — | XoshirO256, ctx.rng, deterministic replays |
| 11 | phase3-time-dilation | phase1-timer-system | Global/local time scale |
| 12 | phase3-input-system | phase1-lua-scripting, phase3-event-bus | InputAction, MappingContext, XInput, rebind |
| 13 | phase3-camera-system | phase3-input-system | SpringArm, orbit, first-person, .uzcam |
| 14 | phase3-game-settings-framework | phase3-save-system | Runtime settings, apply callbacks |
| 15 | phase3-game-mode-state-machine | phase3-event-bus, phase1-shared-state | FSM: MainMenu/Gameplay/Pause/Loading |
| 16 | phase2-gameplay-ability-system | phase3-event-bus, phase1-timer-system, phase1-shared-state | AbilitySpec, cooldown, cost, activation |
| 17 | phase2-behavior-trees | phase1-ecs-system-scheduler, phase0-data-driven-framework, phase1-lua-scripting | .uzbt, Sequence/Selector/Parallel, Blackboard |
| 18 | phase3-day-night-cycle | phase1-timer-system, phase3-event-bus | Keyframe light curves, sky param animation |

**Parallel streams**: Event bus (1) is a gate. After event-bus: physics (3), audio advanced (5–7), gameplay (15–17) all run in parallel.

---

## Phase 6 — Characters & Animation (20 tasks)

**Goal**: Skeletal mesh, full animation state machine, ragdoll, AAA animation suite.
**Milestone**: "Animated character" — Month 9–10
**Exit criteria**: GLTF character with skeleton runs walk/run state machine, ragdoll on death, foot IK.

| # | Task ID | Depends On | Notes |
|---|---------|-----------|-------|
| 1 | phase3-skeletal-mesh | phase2-gltf-import | Bone hierarchy, GPU skinning compute |
| 2 | phase3-animation-system | phase3-skeletal-mesh | AnimClip, StateMachine, BlendTree, IK |
| 3 | phase3-ragdoll | phase3-skeletal-mesh, phase3-physics-system | JPH::Ragdoll, blend-in/out, impulse |
| 4 | phase3-motion-warping | phase3-animation-system | Root motion trajectory warp |
| 5 | phase3-aim-offset | phase3-animation-system | 2D additive grid pitch/yaw |
| 6 | phase3-procedural-foot-placement | phase3-animation-system, phase3-physics-system | IK foot placement, terrain adaptation |
| 7 | phase3-sync-markers-inertialization | phase3-animation-system | Sync markers + inertialization blend |
| 8 | phase3-distance-matching | phase3-animation-system | Speed-driven playrate |
| 9 | phase3-animation-compression | phase3-animation-system | ACL 8:1 compression |
| 10 | phase3-animation-lod | phase3-animation-system | Bone count LOD by distance |
| 11 | phase3-jiggle-bones-secondary-anim | phase3-animation-system | Spring secondary bone sim |
| 12 | phase3-mirror-animation | phase3-animation-system | Left/right anim flip |
| 13 | phase3-stride-orientation-warping | phase3-animation-system | Strafe direction blend |
| 14 | phase3-modular-skeleton-merge | phase3-skeletal-mesh | CMergedSkeleton for MMO equipment |
| 15 | phase3-audio-animation-sync | phase3-animation-system, phase2-audio-system | AnimNotify → audio |
| 16 | phase3-particle-system | phase1-render-graph, phase2-pbr-renderer | GPU particles, .uzfx |
| 17 | phase4-vfx-graph-system | phase3-particle-system | .uzfx v2 JSON, CustomHLSL modules |
| 18 | phase3-compound-colliders | phase3-physics-system | Compound JPH shapes |
| 19 | phase4-convex-decomposition-vhacd | phase3-physics-system | V-HACD auto hull |
| 20 | phase3-physics-batch-queries | phase3-physics-system | Batch raycast/overlap API |

**Count**: 20

---

## Phase 7 — Advanced Rendering (20 tasks)

**Goal**: TAA, SSR, volumetrics, sky, clustered lighting, GI, subsurface. Cinematic quality.
**Milestone**: "AAA visual quality" — Month 11–13
**Exit criteria**: TAA stable, SSR working, volumetric fog visible, sky atmosphere, area lights.

| # | Task ID | Depends On | Notes |
|---|---------|-----------|-------|
| 1 | phase3-taa | phase2-pbr-renderer, phase1-render-graph, phase2-shader-library | Halton jitter, history blend, velocity buffer |
| 2 | phase3-ssr | phase2-pbr-renderer, phase3-taa | Hi-Z trace, TAA reprojection, IBL fallback |
| 3 | phase3-virtual-shadow-maps | phase2-shadow-maps, phase1-render-graph | VSM page table, 16K atlas, stable shadows |
| 4 | phase3-volumetric-fog-froxel | phase2-pbr-renderer, phase1-render-graph | Froxel scatter/transmittance, 160×90×64 |
| 5 | phase3-sky-atmosphere-bruneton | phase2-pbr-renderer | Precomputed Rayleigh/Mie multi-scatter |
| 6 | phase3-subsurface-scattering | phase2-pbr-renderer, phase3-taa | Screen-space SSS, Separable SSS |
| 7 | phase3-motion-blur | phase2-pbr-renderer, phase1-render-graph | Per-object velocity, tile-based blur |
| 8 | phase3-clustered-shading-3d | phase2-pbr-renderer, phase1-render-graph | 3D cluster grid, light list, max 1024 |
| 9 | phase3-area-lights-ltc | phase3-clustered-shading-3d | LTC rect/tube/disk lights, shadow sampling |
| 10 | phase3-global-illumination | phase1-render-graph, phase2-pbr-renderer | Irradiance probe grid, DDGI update |
| 11 | phase4-temporal-upsampling-dlss-fsr | phase3-taa | DLSS 3 / FSR 3 / XeSS SDK integration |
| 12 | phase3-material-merge-atlas | phase2-gltf-import, phase2-texture-tools | Guillotine atlas packer, UV remap, 1 draw call |
| 13 | phase3-audio-hrtf-binaural | phase2-audio-system | X3DAudio HRTF, headphone spatialization |
| 14 | phase3-audio-reverb-zones | phase2-audio-system | CReverbZone, .uzreverbzone, XAudio reverb |
| 15 | phase3-audio-lod | phase2-audio-system | Distance-fade, virtual voice, cull radius |
| 16 | phase3-audio-occlusion | phase2-audio-system, phase3-physics-system | Raycast occlusion, low-pass filter |
| 17 | phase3-opus-audio-codec | phase2-audio-system | Opus encode/decode, .uzaudio v2 streaming |
| 18 | phase3-adaptive-music | phase2-audio-system, phase3-event-bus | Horizontal re-sequencing, vertical remixing |
| 19 | phase2-ui-state-machine-nav-stack | phase2-ui-system | ViewStack, modal stack, controller nav |
| 20 | phase2-ui-rich-text | phase2-ui-system | Inline tags, emoji, gradient, [color][size] |

**Parallel streams**: TAA (1) is a gate for SSR (2) and DLSS (11). Audio advanced (13–18) runs independently. Clustered shading (8) gates area lights (9).

---

## Phase 8 — Gameplay Systems (22 tasks)

**Goal**: Dialogue, accessibility, UI completeness, gameplay patterns, physics advanced.
**Milestone**: "Game-complete feature set" — Month 14–15
**Exit criteria**: Dialogue tree works, full UI input, accessibility, vehicle physics, explosion, haptics.

| # | Task ID | Depends On | Notes |
|---|---------|-----------|-------|
| 1 | phase3-dialogue-system | phase3-event-bus, phase2-behavior-trees, phase1-lua-scripting | .uzdialogue, NPC dialogue trees |
| 2 | phase3-vehicle-physics | phase3-physics-system | JPH::VehicleConstraint, suspension |
| 3 | phase3-explosion-radial-force | phase3-physics-system, phase3-event-bus | Radial impulse, damage sphere |
| 4 | phase3-physics-regions-gravity-zones | phase3-physics-system | Volume gravity override |
| 5 | phase3-physics-debug-profiler | phase3-physics-system, phase2-profiler-instrumentation-api | Jolt debug draw, islands, sleep |
| 6 | phase3-accessibility-framework | phase2-ui-system | ARIA, screen reader, color-blind |
| 7 | phase3-controller-haptics | phase3-input-system | XInput rumble, Lua API |
| 8 | phase3-ui-drag-drop | phase2-ui-system | Drag source/target, payload |
| 9 | phase2-ui-text-input-fields | phase2-ui-system | <uz-input>, clipboard, password |
| 10 | phase2-ui-testing-framework | phase2-ui-system | Lua UI tests, screenshot regression |
| 11 | phase3-ui-minimap-widget | phase2-ui-system, phase1-render-graph | Render-to-texture minimap |
| 12 | phase3-ui-world-space-billboards | phase2-ui-system, phase1-render-graph | World-space UI on entities |
| 13 | phase3-ui-accessibility | phase2-ui-system, phase3-accessibility-framework | Focus, keyboard nav, aria |
| 14 | phase3-font-fallback-cjk | phase2-ui-system | CJK/Arabic/Hebrew fallback |
| 15 | phase3-companion-mount-pattern | phase3-physics-system, phase3-event-bus | No-collision companion/mount |
| 16 | phase3-proximity-lod-swap | phase3-physics-system, phase2-gpu-instancing | MMO proximity LOD state machine |
| 17 | phase3-ktx2-basis-universal | phase2-texture-tools | KTX2/ETC2/ASTC supercompressed textures |
| 18 | phase3-normal-map-baking | phase2-texture-tools | High-poly → low-poly normal bake |
| 19 | phase3-auto-uv-unwrap | phase2-gltf-import | xatlas auto UV unwrap at import |
| 20 | phase4-wind-system-unified | phase3-physics-system, phase3-particle-system | Wind field, foliage, cloth, particles |
| 21 | phase4-impostor-billboards | phase2-gpu-instancing, phase2-texture-tools | 8-angle render-to-texture impostors |
| 22 | phase3-world-streaming | phase1-scene-system, phase3-physics-system | Async chunk load/unload by distance |

**Count**: 22

---

## Phase 9 — World Systems (20 tasks)

**Goal**: Terrain, NavMesh, WorldGen, VFX AI, plugin system, DXR, virtual texturing.
**Milestone**: "Open world capable" — Month 16–18
**Exit criteria**: Procedural terrain with biomes, NPC pathfinding, VFX AI generator, plugin loads.

| # | Task ID | Depends On | Notes |
|---|---------|-----------|-------|
| 1 | phase4-terrain-system | phase3-physics-system, phase2-gpu-instancing | .uzterrain, 4-LOD, HeightFieldShape |
| 2 | phase4-navmesh | phase4-terrain-system, phase3-physics-system | Recast+Detour, bake, RVO2 crowd |
| 3 | phase4-worldgen-graph-compiler | phase4-terrain-system | .uzworldgen JSON, CPU threaded nodes |
| 4 | phase4-worldgen-mmorpg-nodes | phase4-worldgen-graph-compiler | Plateau/Terrace/Cliff/Ramp/Arena nodes |
| 5 | phase4-worldgen-ai-generator | phase4-worldgen-graph-compiler | world_generate + world_refine MCP tools |
| 6 | phase4-worldgen-node-editor | phase4-worldgen-graph-compiler | VSCode reactflow WorldGen editor |
| 7 | phase4-vfx-ai-generator | phase4-vfx-graph-system | vfx_generate MCP + Nano Banana sprites |
| 8 | phase4-vfx-node-editor | phase4-vfx-graph-system | VSCode reactflow VFX editor |
| 9 | phase4-plugin-system | phase1-scene-system, phase4-build-pipeline | .uzplugin TOML, stable C ABI, SEH |
| 10 | phase4-build-pipeline | phase2-asset-pak, phase2-texture-tools | Cook+Stage+Package, NSIS installer |
| 11 | phase4-virtual-texturing | phase2-bindless-resources | Virtual page table, feedback buffer |
| 12 | phase4-gpu-driven-rendering | phase2-bindless-resources, phase2-gpu-instancing | ExecuteIndirect, GPU cull, indirect draw |
| 13 | phase4-cloth-simulation | phase3-physics-system, phase3-skeletal-mesh | PBD cloth, bone attachment |
| 14 | phase4-rope-chain-simulation | phase3-physics-system | Verlet chain, wind interaction |
| 15 | phase4-buoyancy-system | phase3-physics-system | Voxel submersion, fluid density |
| 16 | phase4-soft-body-physics | phase3-physics-system | Jolt SoftBody, mesh deform |
| 17 | phase4-destructible-meshes | phase3-physics-system, phase2-gltf-import | Voronoi fracture, shard RigidBodies |
| 18 | phase4-visual-scripting | phase1-lua-scripting, phase1-editor-panels | Node graph → Lua codegen |
| 19 | phase3-global-illumination | phase1-render-graph, phase2-pbr-renderer | (if not done in Phase 7) DDGI probe grid |
| 20 | phase4-dxr-ray-tracing-rhi | phase0-rhi-design, phase2-pbr-renderer | DXR BLAS/TLAS, ray pipeline |

**Count**: 20

---

## Phase 10 — AAA & Stable MVP (18 tasks)

**Goal**: Nanite, DLSS/FSR, performance hardening, full tooling. Stable, shippable MVP.
**Milestone**: "Stable MVP" — Month 19–22
**Exit criteria**: Nanite works on hero assets, DLSS/FSR optional, all tools complete, CI budget enforcer passing.

| # | Task ID | Depends On | Notes |
|---|---------|-----------|-------|
| 1 | phase4-nanite-virtual-geometry | phase4-gpu-driven-rendering | Meshlet BVH, software rast, vis buffer |
| 2 | phase4-temporal-upsampling-dlss-fsr | phase3-taa | DLSS 3 / FSR 3 / XeSS SDK integration |
| 3 | phase2-ui-rich-text | phase2-ui-system | (if not done in Phase 7) inline tags, emoji |
| 4 | tools-content-audit | phase2-source-asset-db, phase2-asset-pak | Dead assets, VRAM budget, duplicates |
| 5 | tools-shader-debugger | phase1-shader-hot-reload | PIX/RenderDoc frame capture from VSCode |
| 6 | tools-project-packager | phase4-build-pipeline | VSCode GUI for build profiles |
| 7 | tools-draw-call-analyzer | phase2-pbr-renderer | DX12 command list interception + grouping |
| 8 | tools-scene-linter | phase1-scene-system | Static .uzscene analysis, broken refs |
| 9 | tools-asset-validator | phase2-gltf-import, phase2-texture-tools | Pre-import quality gates |
| 10 | tools-gpu-memory-viz | phase2-memory-profiler | Live VRAM allocation panel |
| 11 | tools-uzmesh-inspector | phase2-gltf-import | .uzmesh binary inspector |
| 12 | tools-spline-editor | phase1-scene-system, phase1-editor-gizmos | Bezier spline viewport editor |
| 13 | tools-performance-budget-enforcer | phase2-profiler-instrumentation-api | CI benchmark, regression fail |
| 14 | phase4-virtual-texturing | phase2-bindless-resources | Virtual texture page table |
| 15 | phase4-dxr-ray-tracing-rhi | phase0-rhi-design, phase2-pbr-renderer | DXR ray tracing pipeline |
| 16 | phase2-texture-bulk-optimizer | phase2-texture-tools, phase2-texture-srgb-autodetect | Batch folder import panel in VSCode |
| 17 | phase3-vehicle-physics | phase3-physics-system | JPH::VehicleConstraint, suspension |
| 18 | phase3-sky-atmosphere-bruneton | phase2-pbr-renderer | Precomputed Rayleigh/Mie multi-scatter |

**Count**: 18

---

## Tools Track (Parallel, unlock after Phase 2)

The Tools track runs **in parallel** to the main phases after Phase 2 completes (which provides the debug profiling infrastructure and source asset DB). Tools tasks do not gate any main-phase tasks.

| Task ID | Earliest Unlock Phase | Depends On |
|---------|----------------------|-----------|
| tools-content-audit | Phase 3 (after asset-pak) | phase2-source-asset-db, phase2-asset-pak |
| tools-asset-validator | Phase 3 (after importers) | phase2-gltf-import, phase2-texture-tools |
| tools-uzmesh-inspector | Phase 3 (after gltf-import) | phase2-gltf-import, phase2-auto-lod-pipeline |
| tools-scene-linter | Phase 2 (after scene-system) | phase1-scene-system |
| tools-shader-debugger | Phase 1 (after shader-hot-reload) | phase1-shader-hot-reload |
| tools-draw-call-analyzer | Phase 4 (after pbr-renderer) | phase2-pbr-renderer, phase1-render-graph |
| tools-gpu-memory-viz | Phase 2 (after memory-profiler) | phase2-memory-profiler |
| tools-spline-editor | Phase 1 (after editor-gizmos) | phase1-scene-system, phase1-editor-gizmos |
| tools-project-packager | Phase 9 (after build-pipeline) | phase4-build-pipeline |
| tools-performance-budget-enforcer | Phase 2 (after profiler-api) | phase2-profiler-instrumentation-api |

---

## Complete Migration Table

| New Phase | Task ID (unchanged folder name) |
|-----------|--------------------------------|
| **Phase 1** | phase0-rhi-design, phase0-render-graph-design, phase0-platform-abstraction-layer, phase0-debug-infrastructure-design, phase0-shared-state-design, phase1-scene-system, phase1-debug-profiling, phase1-crash-reporting, phase1-shader-hot-reload, phase1-vscode-syntax-highlighting, phase1-render-graph, phase1-console-commands, phase1-editor-panels, phase1-play-mode, phase1-asset-browser, phase1-asset-drag-drop, phase1-undo-redo, phase1-editor-gizmos |
| **Phase 2** | phase1-multi-select, phase1-lua-scripting, phase1-script-execution-safety, phase1-lua-coroutines, phase1-lua-module-system, phase1-timer-system, phase1-shared-state, phase1-entity-templates, phase2-prefab-edit-mode, phase0-job-system-design, phase0-async-streaming-design, phase0-data-driven-framework, phase0-audio-abstraction-layer, phase1-ecs-system-scheduler, phase1-ecs-spatial-query, phase2-cvar-developer-config-system, phase2-frame-pacing-vsync-modes, phase2-simd-math-optimization, phase2-memory-profiler, phase2-profiler-instrumentation-api |
| **Phase 3** | phase2-pbr-renderer, phase2-bindless-resources, phase2-ibl-pipeline, phase2-shader-library, phase2-shader-variants, phase2-texture-tools, phase2-texture-srgb-autodetect, phase2-texture-bulk-optimizer, phase2-gltf-import, phase2-fbx-import, phase2-auto-lod-pipeline, phase2-asset-pak, phase2-source-asset-db, phase2-asset-hot-reload-textures-meshes, phase2-gpu-instancing, phase2-ssao, phase2-audio-system, phase2-auto-exposure-color-grading |
| **Phase 4** | phase2-shadow-maps, phase2-point-spot-shadows, phase2-render-postprocess, phase2-deferred-decals, phase2-occlusion-culling-hzb, phase2-render-graph-async-compute, phase2-ui-system, phase2-sdf-font-atlas, phase2-ui-animation, phase2-ui-hot-reload-stable, phase2-ui-reference-resolution, phase2-ui-theme-system, phase2-ui-profiler-devtools, phase2-ai-native-features, phase2-script-intellisense-lsp, phase2-script-linting-static-analysis, phase1-lua-dap-server, phase2-data-tables, phase2-physics-multithreading, phase2-audio-event-system |
| **Phase 5** | phase3-event-bus, phase3-tags-groups, phase3-physics-system, phase2-physics-materials, phase2-audio-dsp-chains, phase2-audio-bus-dynamic-graph, phase2-audio-profiler-metering, phase3-save-system, phase3-localization, phase3-seeded-rng, phase3-time-dilation, phase3-input-system, phase3-camera-system, phase3-game-settings-framework, phase3-game-mode-state-machine, phase2-gameplay-ability-system, phase2-behavior-trees, phase3-day-night-cycle |
| **Phase 6** | phase3-skeletal-mesh, phase3-animation-system, phase3-ragdoll, phase3-motion-warping, phase3-aim-offset, phase3-procedural-foot-placement, phase3-sync-markers-inertialization, phase3-distance-matching, phase3-animation-compression, phase3-animation-lod, phase3-jiggle-bones-secondary-anim, phase3-mirror-animation, phase3-stride-orientation-warping, phase3-modular-skeleton-merge, phase3-audio-animation-sync, phase3-particle-system, phase4-vfx-graph-system, phase3-compound-colliders, phase4-convex-decomposition-vhacd, phase3-physics-batch-queries |
| **Phase 7** | phase3-taa, phase3-ssr, phase3-virtual-shadow-maps, phase3-volumetric-fog-froxel, phase3-sky-atmosphere-bruneton, phase3-subsurface-scattering, phase3-motion-blur, phase3-clustered-shading-3d, phase3-area-lights-ltc, phase3-global-illumination, phase4-temporal-upsampling-dlss-fsr, phase3-material-merge-atlas, phase3-audio-hrtf-binaural, phase3-audio-reverb-zones, phase3-audio-lod, phase3-audio-occlusion, phase3-opus-audio-codec, phase3-adaptive-music, phase2-ui-state-machine-nav-stack, phase2-ui-rich-text |
| **Phase 8** | phase3-dialogue-system, phase3-vehicle-physics, phase3-explosion-radial-force, phase3-physics-regions-gravity-zones, phase3-physics-debug-profiler, phase3-accessibility-framework, phase3-controller-haptics, phase3-ui-drag-drop, phase2-ui-text-input-fields, phase2-ui-testing-framework, phase3-ui-minimap-widget, phase3-ui-world-space-billboards, phase3-ui-accessibility, phase3-font-fallback-cjk, phase3-companion-mount-pattern, phase3-proximity-lod-swap, phase3-ktx2-basis-universal, phase3-normal-map-baking, phase3-auto-uv-unwrap, phase4-wind-system-unified, phase4-impostor-billboards, phase3-world-streaming |
| **Phase 9** | phase4-terrain-system, phase4-navmesh, phase4-worldgen-graph-compiler, phase4-worldgen-mmorpg-nodes, phase4-worldgen-ai-generator, phase4-worldgen-node-editor, phase4-vfx-ai-generator, phase4-vfx-node-editor, phase4-plugin-system, phase4-build-pipeline, phase4-virtual-texturing, phase4-gpu-driven-rendering, phase4-cloth-simulation, phase4-rope-chain-simulation, phase4-buoyancy-system, phase4-soft-body-physics, phase4-destructible-meshes, phase4-visual-scripting, phase3-global-illumination, phase4-dxr-ray-tracing-rhi |
| **Phase 10** | phase4-nanite-virtual-geometry, phase4-temporal-upsampling-dlss-fsr, tools-content-audit, tools-shader-debugger, tools-project-packager, tools-draw-call-analyzer, tools-scene-linter, tools-asset-validator, tools-gpu-memory-viz, tools-uzmesh-inspector, tools-spline-editor, tools-performance-budget-enforcer |

---

## Phase Summary

| Phase | Name | Tasks | Goal | Month |
|-------|------|-------|------|-------|
| 1 | Engine Foundation | 18 | DX12 boots, streams to VSCode, crash safe | 1 |
| 2 | Editor Core | 20 | Full editor workflow, Lua scripting | 2 |
| 3 | Asset Pipeline | 18 | Import glTF/FBX/textures/audio, .uzpak | 3–4 |
| 4 | Rendering Quality | 20 | PBR shadows, post-process, UI system | 5–6 |
| 5 | Physics & Gameplay Core | 18 | Jolt physics, audio ecosystem, input, save | 7–8 |
| 6 | Characters & Animation | 20 | Skeletal mesh, animation AAA suite, VFX | 9–10 |
| 7 | Advanced Rendering | 20 | TAA, SSR, volumetrics, sky, GI, area lights | 11–13 |
| 8 | Gameplay Systems | 22 | Dialogue, vehicles, accessibility, UI complete | 14–15 |
| 9 | World Systems | 20 | Terrain, NavMesh, WorldGen, plugins, build | 16–18 |
| 10 | AAA & Stable MVP | 18 | Nanite, DLSS/FSR, all tools, CI hardening | 19–22 |
| Tools | Developer Tools | 10 | Parallel track, unlock after Phase 2 | Any |
| **Total** | | **188** | | **22 months** |
