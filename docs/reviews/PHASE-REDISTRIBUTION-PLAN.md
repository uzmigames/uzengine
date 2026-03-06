# UzEngine — 10-Phase Task Redistribution Plan

**Date**: 2026-03-03 (folders renamed 2026-03-03)
**Status**: Approved
**Scope**: 188 Rulebook tasks across 10 phases + Tools track
**Previous structure**: Phase 0–4 (5 phases, severely overloaded Phase 2/3)
**This structure**: Phase 1–10 + Tools parallel track

> **Note**: Task folder IDs now match the new phase numbers (e.g. `phase3-pbr-renderer`).
> The rename was applied on 2026-03-03 — all `.rulebook/tasks/` folders use the new `phase1`–`phase10` prefix.

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
**Exit criteria**: `render.exe --editor` streams viewport; .uzscene loads; crash reporter active.

| # | Task ID | Depends On | Notes |
|---|---------|-----------|-------|
| 1 | phase1-rhi-design | — | Design doc: DX12/Vulkan/Metal vtable API |
| 2 | phase1-render-graph-design | phase1-rhi-design | Design doc: DAG, barriers, transient alloc |
| 3 | phase1-platform-abstraction-layer | — | Design doc: IPlatform (FS, threads, timers) |
| 4 | phase1-debug-infrastructure-design | — | Design doc: logging, Tracy hooks, crash |
| 5 | phase1-shared-state-design | — | Design doc: GameState store API |
| 6 | phase1-scene-system | — | Node tree, .uzscene parser, IPC scene.* |
| 7 | phase1-debug-profiling | — | Tracy, ImGui overlay, IPC engine.status |
| 8 | phase1-crash-reporting | — | SEH, minidump, JSON crash log |
| 9 | phase1-shader-hot-reload | — | DXC integration, file watcher, VSCode diag |
| 10 | phase1-vscode-syntax-highlighting | — | TextMate grammar for .uzscene/.uzmat/.uzlua |
| 11 | phase1-render-graph | phase1-render-graph-design | RgPass DAG, auto barriers, transient alloc |
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
| 1 | phase2-multi-select | phase1-editor-gizmos | Rubber-band, bulk transform, bulk delete |
| 2 | phase2-lua-scripting | phase1-scene-system | LuaJIT VM, ctx.* bindings, sandbox |
| 3 | phase2-script-execution-safety | phase2-lua-scripting | Watchdog + 5M instruction budget |
| 4 | phase2-lua-coroutines | phase2-lua-scripting | Coroutine async/yield support |
| 5 | phase2-lua-module-system | phase2-lua-scripting | require() path resolution |
| 6 | phase2-timer-system | phase2-lua-scripting | ctx:schedule_after/every/cancel_timer |
| 7 | phase2-shared-state | phase2-lua-scripting | GameState key-value store + Lua proxy |
| 8 | phase2-entity-templates | phase1-editor-panels, phase1-undo-redo | Prefab-lite instantiation from Asset Browser |
| 9 | phase2-prefab-edit-mode | phase1-editor-panels, phase1-undo-redo | Isolated viewport, breadcrumb, override panel |
| 10 | phase2-job-system-design | — | Design doc: enkiTS JobDispatcher API |
| 11 | phase2-async-streaming-design | — | Design doc: async IO, streaming arena |
| 12 | phase2-data-driven-framework | — | Design doc: INI schema, .uz* file format |
| 13 | phase2-audio-abstraction-layer | — | Design doc: IAudioEngine + NullAudioEngine |
| 14 | phase2-ecs-system-scheduler | phase1-scene-system | Parallel ECS system registration + ordering |
| 15 | phase2-ecs-spatial-query | phase1-scene-system, phase2-ecs-system-scheduler | BVH spatial queries for ECS |
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
| 1 | phase3-pbr-renderer | phase1-render-graph | Deferred G-Buffer, GGX BRDF, tone mapping |
| 2 | phase3-bindless-resources | phase3-pbr-renderer | Bindless descriptor heap — P0 root sig change |
| 3 | phase3-ibl-pipeline | phase3-pbr-renderer | BRDF LUT, irradiance, radiance cubemaps |
| 4 | phase3-shader-library | phase3-pbr-renderer | shaders/include/*.hlsli shared library |
| 5 | phase3-shader-variants | phase3-pbr-renderer, phase3-shader-library | Permutation keys, PSO cache, DXC -D flags |
| 6 | phase3-texture-tools | phase3-pbr-renderer | ispc_texcomp BC7/BC5/BC6H, mips, DDS |
| 7 | phase3-texture-srgb-autodetect | phase3-texture-tools | Filename pattern → sRGB, alpha detection |
| 8 | phase3-texture-bulk-optimizer | phase3-texture-tools, phase3-texture-srgb-autodetect | Batch folder import panel in VSCode |
| 9 | phase3-gltf-import | phase3-pbr-renderer | fastgltf + meshoptimizer LOD |
| 10 | phase3-fbx-import | phase3-gltf-import | ufbx, skeleton/anim metadata |
| 11 | phase3-auto-lod-pipeline | phase3-gltf-import, phase3-fbx-import | Auto 4-level LOD at import time |
| 12 | phase3-asset-pak | phase3-texture-tools, phase3-gltf-import | .uzpak LZ4+CRC, VirtualFilesystem |
| 13 | phase3-source-asset-db | phase3-gltf-import, phase3-fbx-import, phase3-texture-tools | SQLite mtime-based incremental rebuild |
| 14 | phase3-asset-hot-reload-textures-meshes | phase3-source-asset-db | FileWatcher → hot-swap GPU resource |
| 15 | phase3-gpu-instancing | phase3-gltf-import, phase3-pbr-renderer | BatchBuilder, StructuredBuffer<float4x4> |
| 16 | phase3-ssao | phase3-pbr-renderer | HBAO+, bilateral blur, depth reconstruction |
| 17 | phase3-audio-system | phase2-audio-abstraction-layer, phase1-scene-system | XAudio2, 64-voice pool, 4-bus, X3DAudio |
| 18 | phase3-auto-exposure-color-grading | phase3-pbr-renderer | HDR luminance histogram, LUT color grading |

**Parallel streams**: PBR renderer (1) gates rendering tasks. Audio system (17) fully independent. Source asset DB (13) unlocks after importers.

---

## Phase 4 — Rendering Quality (20 tasks)

**Goal**: AAA-quality PBR rendering. Shadows, post-process, occlusion culling, UI system.
**Milestone**: "Render demo quality" — Month 5–6
**Exit criteria**: CSM shadows, bloom/tone-map/SSAO, HZB culling, async compute, UI overlays working.

| # | Task ID | Depends On | Notes |
|---|---------|-----------|-------|
| 1 | phase4-shadow-maps | phase3-pbr-renderer | CSM 4 cascades, atlas 4096×2048 |
| 2 | phase4-point-spot-shadows | phase4-shadow-maps | Dual-paraboloid point, perspective spot |
| 3 | phase4-render-postprocess | phase1-render-graph, phase3-pbr-renderer | Bloom, ACES, vignette, LUT RgPasses |
| 4 | phase4-deferred-decals | phase3-pbr-renderer, phase1-render-graph | DBuffer decal RgPass, CDecal component |
| 5 | phase4-occlusion-culling-hzb | phase3-pbr-renderer | Hi-Z pyramid, GPU visibility query |
| 6 | phase4-render-graph-async-compute | phase1-render-graph | Async queue overlap, barrier scheduling |
| 7 | phase4-ui-system | phase3-pbr-renderer, phase3-shader-variants | UIPass, HTML+CSS layout, SDF fonts |
| 8 | phase4-sdf-font-atlas | phase4-ui-system | msdf-atlas-gen, MSDF HLSL, CText |
| 9 | phase4-ui-animation | phase4-ui-system | CSS transition + keyframe animation |
| 10 | phase4-ui-hot-reload-stable | phase4-ui-system | File watch → UI rebuild without restart |
| 11 | phase4-ui-reference-resolution | phase4-ui-system | Canonical resolution scaler, DPR support |
| 12 | phase4-ui-theme-system | phase4-ui-system | CSS custom property themes, runtime swap |
| 13 | phase4-ui-profiler-devtools | phase4-ui-system, phase2-profiler-instrumentation-api | Layout/paint/composite timing panel |
| 14 | phase4-ai-native-features | phase3-pbr-renderer, phase3-shader-library | MCP: scene_generate, shader_assist, script_generate |
| 15 | phase4-script-intellisense-lsp | phase2-lua-scripting | LuaLS + ctx.d.lua type definitions |
| 16 | phase4-script-linting-static-analysis | phase2-lua-scripting | luacheck, .luacheckrc, CI integration |
| 17 | phase4-lua-dap-server | phase2-lua-scripting | DAP server, breakpoints, variable inspect |
| 18 | phase4-data-tables | phase1-scene-system, phase2-lua-scripting | .uztable CSV/JSON, VSCode grid editor |
| 19 | phase4-physics-multithreading | phase2-job-system-design | Jolt parallel island dispatch + UzJobSystem |
| 20 | phase4-audio-event-system | phase3-audio-system, phase2-data-driven-framework | .uzevent files, AudioEventRegistry |

**Parallel streams**: Shadow maps (1–2) and post-process (3–4) run independently. UI system (7–13) is fully independent. Script tooling (15–17) fully independent.

---

## Phase 5 — Physics, Audio & Gameplay Core (18 tasks)

**Goal**: Jolt Physics, complete audio ecosystem, event bus, input, camera, save system.
**Milestone**: "Playable prototype" — Month 7–8
**Exit criteria**: Character walks with physics, audio plays with spatial, save/load works, input rebindable.

| # | Task ID | Depends On | Notes |
|---|---------|-----------|-------|
| 1 | phase5-event-bus | phase2-lua-scripting | EventBus<T> + Lua string-dispatch |
| 2 | phase5-tags-groups | phase1-scene-system | CTags bitset<64>, find_by_tag queries |
| 3 | phase5-physics-system | phase1-scene-system, phase5-event-bus | Jolt, CRigidBody, CCollider, CharacterVirtual |
| 4 | phase5-physics-materials | phase5-physics-system, phase2-data-driven-framework | .uzphysmat, surface_type → audio/VFX routing |
| 5 | phase5-audio-dsp-chains | phase3-audio-system | Per-bus EQ, reverb, limiter DSP effects |
| 6 | phase5-audio-bus-dynamic-graph | phase3-audio-system, phase5-audio-dsp-chains | Runtime bus graph DAG rewiring |
| 7 | phase5-audio-profiler-metering | phase3-audio-system, phase2-profiler-instrumentation-api | Per-bus VU meters, steal rate graph |
| 8 | phase5-save-system | phase1-scene-system, phase2-lua-scripting | msgpack slots 0-9 + autosave |
| 9 | phase5-localization | phase1-scene-system, phase2-lua-scripting | CSV translations, ctx.locale:t() |
| 10 | phase5-seeded-rng | — | XoshirO256, ctx.rng, deterministic replays |
| 11 | phase5-time-dilation | phase2-timer-system | Global/local time scale |
| 12 | phase5-input-system | phase2-lua-scripting, phase5-event-bus | InputAction, MappingContext, XInput, rebind |
| 13 | phase5-camera-system | phase5-input-system | SpringArm, orbit, first-person, .uzcam |
| 14 | phase5-game-settings-framework | phase5-save-system | Runtime settings, apply callbacks |
| 15 | phase5-game-mode-state-machine | phase5-event-bus, phase2-shared-state | FSM: MainMenu/Gameplay/Pause/Loading |
| 16 | phase5-gameplay-ability-system | phase5-event-bus, phase2-timer-system, phase2-shared-state | AbilitySpec, cooldown, cost, activation |
| 17 | phase5-behavior-trees | phase2-ecs-system-scheduler, phase2-data-driven-framework, phase2-lua-scripting | .uzbt, Sequence/Selector/Parallel, Blackboard |
| 18 | phase5-day-night-cycle | phase2-timer-system, phase5-event-bus | Keyframe light curves, sky param animation |

**Parallel streams**: Event bus (1) is a gate. After event-bus: physics (3), audio advanced (5–7), gameplay (15–17) all run in parallel.

---

## Phase 6 — Characters & Animation (20 tasks)

**Goal**: Skeletal mesh, full animation state machine, ragdoll, AAA animation suite.
**Milestone**: "Animated character" — Month 9–10
**Exit criteria**: GLTF character with skeleton runs walk/run state machine, ragdoll on death, foot IK.

| # | Task ID | Depends On | Notes |
|---|---------|-----------|-------|
| 1 | phase6-skeletal-mesh | phase3-gltf-import | Bone hierarchy, GPU skinning compute |
| 2 | phase6-animation-system | phase6-skeletal-mesh | AnimClip, StateMachine, BlendTree, IK |
| 3 | phase6-ragdoll | phase6-skeletal-mesh, phase5-physics-system | JPH::Ragdoll, blend-in/out, impulse |
| 4 | phase6-motion-warping | phase6-animation-system | Root motion trajectory warp |
| 5 | phase6-aim-offset | phase6-animation-system | 2D additive grid pitch/yaw |
| 6 | phase6-procedural-foot-placement | phase6-animation-system, phase5-physics-system | IK foot placement, terrain adaptation |
| 7 | phase6-sync-markers-inertialization | phase6-animation-system | Sync markers + inertialization blend |
| 8 | phase6-distance-matching | phase6-animation-system | Speed-driven playrate |
| 9 | phase6-animation-compression | phase6-animation-system | ACL 8:1 compression |
| 10 | phase6-animation-lod | phase6-animation-system | Bone count LOD by distance |
| 11 | phase6-jiggle-bones-secondary-anim | phase6-animation-system | Spring secondary bone sim |
| 12 | phase6-mirror-animation | phase6-animation-system | Left/right anim flip |
| 13 | phase6-stride-orientation-warping | phase6-animation-system | Strafe direction blend |
| 14 | phase6-modular-skeleton-merge | phase6-skeletal-mesh | CMergedSkeleton for MMO equipment |
| 15 | phase6-audio-animation-sync | phase6-animation-system, phase3-audio-system | AnimNotify → audio |
| 16 | phase6-particle-system | phase1-render-graph, phase3-pbr-renderer | GPU particles, .uzfx |
| 17 | phase6-vfx-graph-system | phase6-particle-system | .uzfx v2 JSON, CustomHLSL modules |
| 18 | phase6-compound-colliders | phase5-physics-system | Compound JPH shapes |
| 19 | phase6-convex-decomposition-vhacd | phase5-physics-system | V-HACD auto hull |
| 20 | phase6-physics-batch-queries | phase5-physics-system | Batch raycast/overlap API |

**Count**: 20

---

## Phase 7 — Advanced Rendering (20 tasks)

**Goal**: TAA, SSR, volumetrics, sky, clustered lighting, GI, subsurface. Cinematic quality.
**Milestone**: "AAA visual quality" — Month 11–13
**Exit criteria**: TAA stable, SSR working, volumetric fog visible, sky atmosphere, area lights.

| # | Task ID | Depends On | Notes |
|---|---------|-----------|-------|
| 1 | phase7-taa | phase3-pbr-renderer, phase1-render-graph, phase3-shader-library | Halton jitter, history blend, velocity buffer |
| 2 | phase7-ssr | phase3-pbr-renderer, phase7-taa | Hi-Z trace, TAA reprojection, IBL fallback |
| 3 | phase7-virtual-shadow-maps | phase4-shadow-maps, phase1-render-graph | VSM page table, 16K atlas, stable shadows |
| 4 | phase7-volumetric-fog-froxel | phase3-pbr-renderer, phase1-render-graph | Froxel scatter/transmittance, 160×90×64 |
| 5 | phase7-sky-atmosphere-bruneton | phase3-pbr-renderer | Precomputed Rayleigh/Mie multi-scatter |
| 6 | phase7-subsurface-scattering | phase3-pbr-renderer, phase7-taa | Screen-space SSS, Separable SSS |
| 7 | phase7-motion-blur | phase3-pbr-renderer, phase1-render-graph | Per-object velocity, tile-based blur |
| 8 | phase7-clustered-shading-3d | phase3-pbr-renderer, phase1-render-graph | 3D cluster grid, light list, max 1024 |
| 9 | phase7-area-lights-ltc | phase7-clustered-shading-3d | LTC rect/tube/disk lights, shadow sampling |
| 10 | phase7-global-illumination | phase1-render-graph, phase3-pbr-renderer | Irradiance probe grid, DDGI update |
| 11 | phase7-temporal-upsampling-dlss-fsr | phase7-taa | DLSS 3 / FSR 3 / XeSS SDK integration |
| 12 | phase7-material-merge-atlas | phase3-gltf-import, phase3-texture-tools | Guillotine atlas packer, UV remap, 1 draw call |
| 13 | phase7-audio-hrtf-binaural | phase3-audio-system | X3DAudio HRTF, headphone spatialization |
| 14 | phase7-audio-reverb-zones | phase3-audio-system | CReverbZone, .uzreverbzone, XAudio reverb |
| 15 | phase7-audio-lod | phase3-audio-system | Distance-fade, virtual voice, cull radius |
| 16 | phase7-audio-occlusion | phase3-audio-system, phase5-physics-system | Raycast occlusion, low-pass filter |
| 17 | phase7-opus-audio-codec | phase3-audio-system | Opus encode/decode, .uzaudio v2 streaming |
| 18 | phase7-adaptive-music | phase3-audio-system, phase5-event-bus | Horizontal re-sequencing, vertical remixing |
| 19 | phase7-ui-state-machine-nav-stack | phase4-ui-system | ViewStack, modal stack, controller nav |
| 20 | phase7-ui-rich-text | phase4-ui-system | Inline tags, emoji, gradient, [color][size] |

**Parallel streams**: TAA (1) is a gate for SSR (2) and DLSS (11). Audio advanced (13–18) runs independently. Clustered shading (8) gates area lights (9).

---

## Phase 8 — Gameplay Systems (22 tasks)

**Goal**: Dialogue, accessibility, UI completeness, gameplay patterns, physics advanced.
**Milestone**: "Game-complete feature set" — Month 14–15
**Exit criteria**: Dialogue tree works, full UI input, accessibility, vehicle physics, explosion, haptics.

| # | Task ID | Depends On | Notes |
|---|---------|-----------|-------|
| 1 | phase8-dialogue-system | phase5-event-bus, phase5-behavior-trees, phase2-lua-scripting | .uzdialogue, NPC dialogue trees |
| 2 | phase8-vehicle-physics | phase5-physics-system | JPH::VehicleConstraint, suspension |
| 3 | phase8-explosion-radial-force | phase5-physics-system, phase5-event-bus | Radial impulse, damage sphere |
| 4 | phase8-physics-regions-gravity-zones | phase5-physics-system | Volume gravity override |
| 5 | phase8-physics-debug-profiler | phase5-physics-system, phase2-profiler-instrumentation-api | Jolt debug draw, islands, sleep |
| 6 | phase8-accessibility-framework | phase4-ui-system | ARIA, screen reader, color-blind |
| 7 | phase8-controller-haptics | phase5-input-system | XInput rumble, Lua API |
| 8 | phase8-ui-drag-drop | phase4-ui-system | Drag source/target, payload |
| 9 | phase8-ui-text-input-fields | phase4-ui-system | <uz-input>, clipboard, password |
| 10 | phase8-ui-testing-framework | phase4-ui-system | Lua UI tests, screenshot regression |
| 11 | phase8-ui-minimap-widget | phase4-ui-system, phase1-render-graph | Render-to-texture minimap |
| 12 | phase8-ui-world-space-billboards | phase4-ui-system, phase1-render-graph | World-space UI on entities |
| 13 | phase8-ui-accessibility | phase4-ui-system, phase8-accessibility-framework | Focus, keyboard nav, aria |
| 14 | phase8-font-fallback-cjk | phase4-ui-system | CJK/Arabic/Hebrew fallback |
| 15 | phase8-companion-mount-pattern | phase5-physics-system, phase5-event-bus | No-collision companion/mount |
| 16 | phase8-proximity-lod-swap | phase5-physics-system, phase3-gpu-instancing | MMO proximity LOD state machine |
| 17 | phase8-ktx2-basis-universal | phase3-texture-tools | KTX2/ETC2/ASTC supercompressed textures |
| 18 | phase8-normal-map-baking | phase3-texture-tools | High-poly → low-poly normal bake |
| 19 | phase8-auto-uv-unwrap | phase3-gltf-import | xatlas auto UV unwrap at import |
| 20 | phase8-wind-system-unified | phase5-physics-system, phase6-particle-system | Wind field, foliage, cloth, particles |
| 21 | phase8-impostor-billboards | phase3-gpu-instancing, phase3-texture-tools | 8-angle render-to-texture impostors |
| 22 | phase8-world-streaming | phase1-scene-system, phase5-physics-system | Async chunk load/unload by distance |

**Count**: 22

---

## Phase 9 — World Systems (20 tasks)

**Goal**: Terrain, NavMesh, WorldGen, VFX AI, plugin system, DXR, virtual texturing.
**Milestone**: "Open world capable" — Month 16–18
**Exit criteria**: Procedural terrain with biomes, NPC pathfinding, VFX AI generator, plugin loads.

| # | Task ID | Depends On | Notes |
|---|---------|-----------|-------|
| 1 | phase9-terrain-system | phase5-physics-system, phase3-gpu-instancing | .uzterrain, 4-LOD, HeightFieldShape |
| 2 | phase9-navmesh | phase9-terrain-system, phase5-physics-system | Recast+Detour, bake, RVO2 crowd |
| 3 | phase9-worldgen-graph-compiler | phase9-terrain-system | .uzworldgen JSON, CPU threaded nodes |
| 4 | phase9-worldgen-mmorpg-nodes | phase9-worldgen-graph-compiler | Plateau/Terrace/Cliff/Ramp/Arena nodes |
| 5 | phase9-worldgen-ai-generator | phase9-worldgen-graph-compiler | world_generate + world_refine MCP tools |
| 6 | phase9-worldgen-node-editor | phase9-worldgen-graph-compiler | VSCode reactflow WorldGen editor |
| 7 | phase9-vfx-ai-generator | phase6-vfx-graph-system | vfx_generate MCP + Nano Banana sprites |
| 8 | phase9-vfx-node-editor | phase6-vfx-graph-system | VSCode reactflow VFX editor |
| 9 | phase9-plugin-system | phase1-scene-system, phase9-build-pipeline | .uzplugin TOML, stable C ABI, SEH |
| 10 | phase9-build-pipeline | phase3-asset-pak, phase3-texture-tools | Cook+Stage+Package, NSIS installer |
| 11 | phase9-virtual-texturing | phase3-bindless-resources | Virtual page table, feedback buffer |
| 12 | phase9-gpu-driven-rendering | phase3-bindless-resources, phase3-gpu-instancing | ExecuteIndirect, GPU cull, indirect draw |
| 13 | phase9-cloth-simulation | phase5-physics-system, phase6-skeletal-mesh | PBD cloth, bone attachment |
| 14 | phase9-rope-chain-simulation | phase5-physics-system | Verlet chain, wind interaction |
| 15 | phase9-buoyancy-system | phase5-physics-system | Voxel submersion, fluid density |
| 16 | phase9-soft-body-physics | phase5-physics-system | Jolt SoftBody, mesh deform |
| 17 | phase9-destructible-meshes | phase5-physics-system, phase3-gltf-import | Voronoi fracture, shard RigidBodies |
| 18 | phase9-visual-scripting | phase2-lua-scripting, phase1-editor-panels | Node graph → Lua codegen |
| 19 | phase9-dxr-ray-tracing-rhi | phase1-rhi-design, phase3-pbr-renderer | DXR BLAS/TLAS, ray pipeline |

**Count**: 19

---

## Phase 10 — AAA & Stable MVP (18 tasks)

**Goal**: Nanite, DLSS/FSR, performance hardening, full tooling. Stable, shippable MVP.
**Milestone**: "Stable MVP" — Month 19–22
**Exit criteria**: Nanite works on hero assets, DLSS/FSR optional, all tools complete, CI budget enforcer passing.

| # | Task ID | Depends On | Notes |
|---|---------|-----------|-------|
| 1 | phase10-nanite-virtual-geometry | phase9-gpu-driven-rendering | Meshlet BVH, software rast, vis buffer |
| 2 | tools-content-audit | phase3-source-asset-db, phase3-asset-pak | Dead assets, VRAM budget, duplicates |
| 3 | tools-shader-debugger | phase1-shader-hot-reload | PIX/RenderDoc frame capture from VSCode |
| 4 | tools-project-packager | phase9-build-pipeline | VSCode GUI for build profiles |
| 5 | tools-draw-call-analyzer | phase3-pbr-renderer | DX12 command list interception + grouping |
| 6 | tools-scene-linter | phase1-scene-system | Static .uzscene analysis, broken refs |
| 7 | tools-asset-validator | phase3-gltf-import, phase3-texture-tools | Pre-import quality gates |
| 8 | tools-gpu-memory-viz | phase2-memory-profiler | Live VRAM allocation panel |
| 9 | tools-uzmesh-inspector | phase3-gltf-import | .uzmesh binary inspector |
| 10 | tools-spline-editor | phase1-scene-system, phase1-editor-gizmos | Bezier spline viewport editor |
| 11 | tools-performance-budget-enforcer | phase2-profiler-instrumentation-api | CI benchmark, regression fail |

**Count**: 18

---

## Tools Track (Parallel, unlock after Phase 2)

The Tools track runs **in parallel** to the main phases after Phase 2 completes (which provides the debug profiling infrastructure and source asset DB). Tools tasks do not gate any main-phase tasks.

| Task ID | Earliest Unlock Phase | Depends On |
|---------|----------------------|-----------|
| tools-content-audit | Phase 3 (after asset-pak) | phase3-source-asset-db, phase3-asset-pak |
| tools-asset-validator | Phase 3 (after importers) | phase3-gltf-import, phase3-texture-tools |
| tools-uzmesh-inspector | Phase 3 (after gltf-import) | phase3-gltf-import, phase3-auto-lod-pipeline |
| tools-scene-linter | Phase 1 (after scene-system) | phase1-scene-system |
| tools-shader-debugger | Phase 1 (after shader-hot-reload) | phase1-shader-hot-reload |
| tools-draw-call-analyzer | Phase 3 (after pbr-renderer) | phase3-pbr-renderer, phase1-render-graph |
| tools-gpu-memory-viz | Phase 2 (after memory-profiler) | phase2-memory-profiler |
| tools-spline-editor | Phase 1 (after editor-gizmos) | phase1-scene-system, phase1-editor-gizmos |
| tools-project-packager | Phase 9 (after build-pipeline) | phase9-build-pipeline |
| tools-performance-budget-enforcer | Phase 2 (after profiler-api) | phase2-profiler-instrumentation-api |

---

## Complete Migration Table

| New Phase | Task ID (unchanged folder name) |
|-----------|--------------------------------|
| **Phase 1** | phase1-rhi-design, phase1-render-graph-design, phase1-platform-abstraction-layer, phase1-debug-infrastructure-design, phase1-shared-state-design, phase1-scene-system, phase1-debug-profiling, phase1-crash-reporting, phase1-shader-hot-reload, phase1-vscode-syntax-highlighting, phase1-render-graph, phase1-console-commands, phase1-editor-panels, phase1-play-mode, phase1-asset-browser, phase1-asset-drag-drop, phase1-undo-redo, phase1-editor-gizmos |
| **Phase 2** | phase2-multi-select, phase2-lua-scripting, phase2-script-execution-safety, phase2-lua-coroutines, phase2-lua-module-system, phase2-timer-system, phase2-shared-state, phase2-entity-templates, phase2-prefab-edit-mode, phase2-job-system-design, phase2-async-streaming-design, phase2-data-driven-framework, phase2-audio-abstraction-layer, phase2-ecs-system-scheduler, phase2-ecs-spatial-query, phase2-cvar-developer-config-system, phase2-frame-pacing-vsync-modes, phase2-simd-math-optimization, phase2-memory-profiler, phase2-profiler-instrumentation-api |
| **Phase 3** | phase3-pbr-renderer, phase3-bindless-resources, phase3-ibl-pipeline, phase3-shader-library, phase3-shader-variants, phase3-texture-tools, phase3-texture-srgb-autodetect, phase3-texture-bulk-optimizer, phase3-gltf-import, phase3-fbx-import, phase3-auto-lod-pipeline, phase3-asset-pak, phase3-source-asset-db, phase3-asset-hot-reload-textures-meshes, phase3-gpu-instancing, phase3-ssao, phase3-audio-system, phase3-auto-exposure-color-grading |
| **Phase 4** | phase4-shadow-maps, phase4-point-spot-shadows, phase4-render-postprocess, phase4-deferred-decals, phase4-occlusion-culling-hzb, phase4-render-graph-async-compute, phase4-ui-system, phase4-sdf-font-atlas, phase4-ui-animation, phase4-ui-hot-reload-stable, phase4-ui-reference-resolution, phase4-ui-theme-system, phase4-ui-profiler-devtools, phase4-ai-native-features, phase4-script-intellisense-lsp, phase4-script-linting-static-analysis, phase4-lua-dap-server, phase4-data-tables, phase4-physics-multithreading, phase4-audio-event-system, phase4-ui-gamepad-navigation, phase4-ui-widget-library |
| **Phase 5** | phase5-event-bus, phase5-tags-groups, phase5-physics-system, phase5-physics-materials, phase5-audio-dsp-chains, phase5-audio-bus-dynamic-graph, phase5-audio-profiler-metering, phase5-save-system, phase5-localization, phase5-seeded-rng, phase5-time-dilation, phase5-input-system, phase5-camera-system, phase5-game-settings-framework, phase5-game-mode-state-machine, phase5-gameplay-ability-system, phase5-behavior-trees, phase5-day-night-cycle |
| **Phase 6** | phase6-skeletal-mesh, phase6-animation-system, phase6-ragdoll, phase6-motion-warping, phase6-aim-offset, phase6-procedural-foot-placement, phase6-sync-markers-inertialization, phase6-distance-matching, phase6-animation-compression, phase6-animation-lod, phase6-jiggle-bones-secondary-anim, phase6-mirror-animation, phase6-stride-orientation-warping, phase6-modular-skeleton-merge, phase6-audio-animation-sync, phase6-particle-system, phase6-vfx-graph-system, phase6-compound-colliders, phase6-convex-decomposition-vhacd, phase6-physics-batch-queries |
| **Phase 7** | phase7-taa, phase7-ssr, phase7-virtual-shadow-maps, phase7-volumetric-fog-froxel, phase7-sky-atmosphere-bruneton, phase7-subsurface-scattering, phase7-motion-blur, phase7-clustered-shading-3d, phase7-area-lights-ltc, phase7-global-illumination, phase7-temporal-upsampling-dlss-fsr, phase7-material-merge-atlas, phase7-audio-hrtf-binaural, phase7-audio-reverb-zones, phase7-audio-lod, phase7-audio-occlusion, phase7-opus-audio-codec, phase7-adaptive-music, phase7-ui-state-machine-nav-stack, phase7-ui-rich-text |
| **Phase 8** | phase8-dialogue-system, phase8-vehicle-physics, phase8-explosion-radial-force, phase8-physics-regions-gravity-zones, phase8-physics-debug-profiler, phase8-accessibility-framework, phase8-controller-haptics, phase8-ui-drag-drop, phase8-ui-text-input-fields, phase8-ui-testing-framework, phase8-ui-minimap-widget, phase8-ui-world-space-billboards, phase8-ui-accessibility, phase8-font-fallback-cjk, phase8-companion-mount-pattern, phase8-proximity-lod-swap, phase8-ktx2-basis-universal, phase8-normal-map-baking, phase8-auto-uv-unwrap, phase8-wind-system-unified, phase8-impostor-billboards, phase8-world-streaming |
| **Phase 9** | phase9-terrain-system, phase9-navmesh, phase9-worldgen-graph-compiler, phase9-worldgen-mmorpg-nodes, phase9-worldgen-ai-generator, phase9-worldgen-node-editor, phase9-vfx-ai-generator, phase9-vfx-node-editor, phase9-plugin-system, phase9-build-pipeline, phase9-virtual-texturing, phase9-gpu-driven-rendering, phase9-cloth-simulation, phase9-rope-chain-simulation, phase9-buoyancy-system, phase9-soft-body-physics, phase9-destructible-meshes, phase9-visual-scripting, phase9-dxr-ray-tracing-rhi |
| **Phase 10** | phase10-nanite-virtual-geometry, tools-content-audit, tools-shader-debugger, tools-project-packager, tools-draw-call-analyzer, tools-scene-linter, tools-asset-validator, tools-gpu-memory-viz, tools-uzmesh-inspector, tools-spline-editor, tools-performance-budget-enforcer |

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
