# UzEngine AAA Feature Gap Analysis — Version 2

> **Date**: 2026-03-02
> **Scope**: Complete analysis across 9 domains by 9 specialist agents
> **Previous version**: MASTER-GAP-ANALYSIS.md (137 gaps, 7 domains)
> **This version**: ~350+ gaps across 9 domains — all new P0/P1/P2 items catalogued

---

## Executive Summary

The original gap analysis identified 137 gaps. This V2 analysis adds ~220 new gaps discovered by specialist agents covering all 9 engine domains. The most critical findings:

| Priority | Count | Action Required |
|----------|-------|----------------|
| **P0 (Blockers)** | 28 new | Design before Phase 2 begins |
| **P1 (Critical)** | 120 new | Must ship for AAA quality |
| **P2 (Important)** | 72 new | Required for feature parity |
| **Total new** | ~220 | Added to Rulebook as new tasks |

### Top 5 Critical Discoveries

1. **Job System** (P0): Entire engine is 2-threaded. Physics, animation, culling, audio all block each other. Cannot retrofit later — must be designed now.
2. **Script Safety** (P0): `while true do end` freezes engine. Users will hit this on Day 1.
3. **Bindless Resources** (P0): Root signature locked after PSO compilation. Adding bindless later = full renderer rewrite.
4. **IBL Pipeline** (P0): PBR shaders reference `SampleEnvironmentIrradiance()` with no data. Metallic materials are black.
5. **IAudioEngine Abstraction** (P0): XAudio2 hard-coded prevents unit testing, CI, and middleware integration.

---

## Domain 1: Rendering (40 new gaps)

### P0 Rendering Blockers

| ID | Gap | Spec | Task |
|----|-----|------|------|
| R-P0-1 | **Bindless Resources** — fixed descriptor slots block GPU-driven rendering | [bindless-resources.md](../specs/rendering/bindless-resources.md) | `phase2-bindless-resources` |
| R-P0-2 | **IBL Pipeline** — PBR ambient lighting missing, metallic surfaces black | [ibl-pipeline.md](../specs/rendering/ibl-pipeline.md) | `phase2-ibl-pipeline` |
| R-P0-3 | **HZB Occlusion Culling** — no occlusion system, invisible objects waste GPU | TBD | `phase2-occlusion-culling-hzb` |
| R-P0-4 | **Render Graph Async Compute** — async queue unused, no overlap | TBD | `phase2-render-graph-async-compute` |
| R-P0-5 | **Auto-Exposure + Color Grading** — HDR output has no tone mapping | TBD | `phase2-auto-exposure-color-grading` |

### P1 Rendering: Visual Quality

| ID | Gap | Task |
|----|-----|------|
| R-P1-1 | Volumetric fog (froxel-based) | `phase3-volumetric-fog-froxel` |
| R-P1-2 | Sky/atmosphere (Bruneton model) | `phase3-sky-atmosphere-bruneton` |
| R-P1-3 | Subsurface scattering (skin, wax) | `phase3-subsurface-scattering` |
| R-P1-4 | 3D clustered shading (point light scale) | `phase3-clustered-shading-3d` |
| R-P1-5 | Motion blur (per-object velocity buffer) | `phase3-motion-blur` |
| R-P1-6 | Area lights (LTC approximation) | `phase3-area-lights-ltc` |
| R-P1-7 | Screen-space global illumination | TBD |
| R-P1-8 | Virtual shadow maps (cascaded + cached) | `phase3-virtual-shadow-maps` |
| R-P1-9 | Depth of field (Bokeh) | TBD |
| R-P1-10 | Lens flares and bloom variants | TBD |

### P2 Rendering: Advanced Features

| ID | Gap | Task |
|----|-----|------|
| R-P2-1 | DXR Ray Tracing (reflections, AO, shadows) | `phase4-dxr-ray-tracing-rhi` |
| R-P2-2 | GPU-driven rendering (indirect draw) | `phase4-gpu-driven-rendering` |
| R-P2-3 | Temporal upsampling (DLSS/FSR/XeSS) | `phase4-temporal-upsampling-dlss-fsr` |
| R-P2-4 | Nanite-like virtual geometry | `phase4-nanite-virtual-geometry` |
| R-P2-5 | Virtual texturing / megatextures | `phase4-virtual-texturing` |
| R-P2-6 | Parallax occlusion mapping | TBD |
| R-P2-7 | Clear coat material layer | TBD |
| R-P2-8 | Anisotropic specular (hair, brushed metal) | TBD |
| R-P2-9 | Voxel cone tracing GI | TBD |
| R-P2-10 | Deferred decals | `phase2-deferred-decals` |

---

## Domain 2: Engine Core (18 new gaps)

### P0 Engine Core Blockers

| ID | Gap | Spec | Task |
|----|-----|------|------|
| EC-P0-1 | **Job System / Task Graph** — 2-thread engine, all systems serialize | [job-system.md](../specs/engine/job-system.md) | `phase0-job-system-design` |
| EC-P0-2 | **Platform Abstraction Layer** — Windows-only, no Linux/macOS CI | TBD | `phase0-platform-abstraction-layer` |
| EC-P0-3 | **Frame Pacing / VSync** — no flip model, no variable refresh | TBD | `phase2-frame-pacing-vsync-modes` |

### P1 Engine Core

| ID | Gap | Task |
|----|-----|------|
| EC-P1-1 | SIMD math library (Vec3/Mat4 SSE/AVX) | `phase2-simd-math-optimization` |
| EC-P1-2 | Memory profiler (per-arena allocation tracking) | `phase2-memory-profiler` |
| EC-P1-3 | CVars developer config system | `phase2-cvar-developer-config-system` |
| EC-P1-4 | Profiler instrumentation API | `phase2-profiler-instrumentation-api` |
| EC-P1-5 | Asset hot-reload (textures, meshes at runtime) | `phase2-asset-hot-reload-textures-meshes` |
| EC-P1-6 | Async compute queue utilization | `phase2-render-graph-async-compute` |
| EC-P1-7 | ECS spatial queries (sphere/AABB overlap) | `phase1-ecs-spatial-query` |
| EC-P1-8 | ECS system scheduler (parallel system groups) | `phase1-ecs-system-scheduler` |
| EC-P1-9 | Entity templates (prefab-lite) | `phase1-entity-templates` |

### P2 Engine Core

| ID | Gap | Task |
|----|-----|------|
| EC-P2-1 | World streaming (large open world) | `phase3-world-streaming` |
| EC-P2-2 | Shared state (multi-process IPC) | `phase1-shared-state` |
| EC-P2-3 | Data-driven framework (JSON/TOML config nodes) | `phase0-data-driven-framework` |
| EC-P2-4 | Async streaming design | `phase0-async-streaming-design` |

---

## Domain 3: Physics (13 new gaps)

### P0 Physics Blockers

| ID | Gap | Task |
|----|-----|------|
| PH-P0-1 | **Physics Multithreading** — Jolt job system bridge not configured | `phase2-physics-multithreading` |

### P1 Physics

| ID | Gap | Task |
|----|-----|------|
| PH-P1-1 | Batch physics queries (parallel raycast) | `phase3-physics-batch-queries` |
| PH-P1-2 | Compound colliders (multi-shape bodies) | `phase3-compound-colliders` |
| PH-P1-3 | Physics regions (gravity zones, wind volumes) | `phase3-physics-regions-gravity-zones` |
| PH-P1-4 | Explosion / radial force impulse | `phase3-explosion-radial-force` |
| PH-P1-5 | Physics debug profiler (Jolt stats overlay) | `phase3-physics-debug-profiler` |
| PH-P1-6 | Convex decomposition (VHACD integration) | `phase4-convex-decomposition-vhacd` |

### P2 Physics

| ID | Gap | Task |
|----|-----|------|
| PH-P2-1 | Rope/chain simulation | `phase4-rope-chain-simulation` |
| PH-P2-2 | Soft body physics | `phase4-soft-body-physics` |
| PH-P2-3 | Buoyancy system | `phase4-buoyancy-system` |
| PH-P2-4 | Cloth simulation | `phase4-cloth-simulation` |
| PH-P2-5 | Destructible meshes (Voronoi fracture) | `phase4-destructible-meshes` |
| PH-P2-6 | Vehicle physics (wheel suspension) | `phase3-vehicle-physics` |

---

## Domain 4: Audio (19 new gaps)

### P0 Audio Blockers

| ID | Gap | Spec | Task |
|----|-----|------|------|
| AU-P0-1 | **IAudioEngine Abstraction** — XAudio2 hard-coded, no testing path | [audio-abstraction-layer.md](../specs/audio/audio-abstraction-layer.md) | `phase0-audio-abstraction-layer` |
| AU-P0-2 | **Audio Profiler/Metering** — no DSP CPU usage, voice count stats | TBD | `phase2-audio-profiler-metering` |
| AU-P0-3 | **Dynamic Bus Graph** — static 4-bus layout, no runtime reconfiguration | TBD | `phase2-audio-bus-dynamic-graph` |

### P1 Audio

| ID | Gap | Task |
|----|-----|------|
| AU-P1-1 | HRTF binaural audio (spatial headphone) | `phase3-audio-hrtf-binaural` |
| AU-P1-2 | Reverb zones (DSP chain per zone) | `phase3-audio-reverb-zones` |
| AU-P1-3 | Audio LOD (distance-based quality reduction) | `phase3-audio-lod` |
| AU-P1-4 | Animation sync (AudioNotify events) | `phase3-audio-animation-sync` |
| AU-P1-5 | Adaptive/dynamic music system | `phase3-adaptive-music` |
| AU-P1-6 | Audio event system (decoupled trigger → sound) | `phase2-audio-event-system` |
| AU-P1-7 | Audio DSP chains (EQ, compression per bus) | `phase2-audio-dsp-chains` |
| AU-P1-8 | Audio occlusion (geometry-based muffling) | `phase3-audio-occlusion` |

### P2 Audio

| ID | Gap | Task |
|----|-----|------|
| AU-P2-1 | Opus codec support (.uzaudio streaming) | `phase3-opus-audio-codec` |
| AU-P2-2 | Wwise middleware integration | TBD |
| AU-P2-3 | FMOD middleware integration | TBD |

---

## Domain 5: Animation (20 new gaps)

### P0 Animation Blockers

| ID | Gap | Task |
|----|-----|------|
| AN-P0-1 | **Motion Warping** — no root motion trajectory warping for environmental fit | `phase3-motion-warping` |
| AN-P0-2 | **Aim Offset** — no additive layer for weapon/head aiming | `phase3-aim-offset` |
| AN-P0-3 | **Procedural Foot Placement** — feet clip through irregular terrain | `phase3-procedural-foot-placement` |

### P1 Animation

| ID | Gap | Task |
|----|-----|------|
| AN-P1-1 | Sync markers + inertialization blending | `phase3-sync-markers-inertialization` |
| AN-P1-2 | Animation compression (ACL library) | `phase3-animation-compression` |
| AN-P1-3 | Jiggle bones / secondary animation | `phase3-jiggle-bones-secondary-anim` |
| AN-P1-4 | Animation LOD (bone reduction at distance) | `phase3-animation-lod` |
| AN-P1-5 | Distance matching (footstep sync) | `phase3-distance-matching` |
| AN-P1-6 | Stride/orientation warping | `phase3-stride-orientation-warping` |
| AN-P1-7 | Mirror animation (flip poses) | `phase3-mirror-animation` |
| AN-P1-8 | Motion Matching (database-driven anim selection) | TBD |

### P2 Animation

| ID | Gap | Task |
|----|-----|------|
| AN-P2-1 | Full-body IK (FABRIK multi-chain) | TBD |
| AN-P2-2 | Two-bone IK (arms, legs) | TBD |
| AN-P2-3 | Look-at controller | TBD |
| AN-P2-4 | Additive layer blending | TBD |
| AN-P2-5 | Animation state machine sub-states | TBD |
| AN-P2-6 | Retargeting (remap skeleton A→B) | TBD |

---

## Domain 6: Gameplay Systems (22 new gaps)

### P0 Gameplay Blockers

| ID | Gap | Task |
|----|-----|------|
| GP-P0-1 | **Accessibility Framework** — no visual/audio/input accessibility options | `phase3-accessibility-framework` |

### P1 Gameplay

| ID | Gap | Task |
|----|-----|------|
| GP-P1-1 | Controller haptics (XInput rumble API) | `phase3-controller-haptics` |
| GP-P1-2 | Game mode / match state machine | `phase3-game-mode-state-machine` |
| GP-P1-3 | Day/night cycle (time-of-day system) | `phase3-day-night-cycle` |
| GP-P1-4 | Time dilation (slow-motion/bullet time) | `phase3-time-dilation` |
| GP-P1-5 | Seeded RNG (deterministic replay) | `phase3-seeded-rng` |
| GP-P1-6 | Game settings framework (persist quality/key prefs) | `phase3-game-settings-framework` |
| GP-P1-7 | Dialogue system | `phase3-dialogue-system` |
| GP-P1-8 | Behavior trees | `phase2-behavior-trees` |
| GP-P1-9 | Data tables (CSV-driven config) | `phase2-data-tables` |

### P2 Gameplay

| ID | Gap | Task |
|----|-----|------|
| GP-P2-1 | Quest/objective tracking system | TBD |
| GP-P2-2 | Inventory / item system | TBD |
| GP-P2-3 | Split-screen rendering | TBD |
| GP-P2-4 | Global illumination (dynamic GI) | `phase3-global-illumination` |
| GP-P2-5 | Visual scripting | `phase4-visual-scripting` |

---

## Domain 7: Asset Pipeline (24 new gaps)

### P1 Asset Pipeline

| ID | Gap | Task |
|----|-----|------|
| AP-P1-1 | KTX2 / Basis Universal universal texture compression | `phase3-ktx2-basis-universal` |
| AP-P1-2 | Normal map baking (high-poly → low-poly) | `phase3-normal-map-baking` |
| AP-P1-3 | Auto UV unwrap (xatlas) | `phase3-auto-uv-unwrap` |
| AP-P1-4 | Opus/AAC audio codec support | `phase3-opus-audio-codec` |
| AP-P1-5 | SDF font atlas generation | `phase2-sdf-font-atlas` |
| AP-P1-6 | Watch-and-import (file watcher) | `phase2-asset-hot-reload-textures-meshes` |
| AP-P1-7 | Import settings inheritance | TBD |
| AP-P1-8 | EXR HDR texture pipeline | TBD |

### P2 Asset Pipeline

| ID | Gap | Task |
|----|-----|------|
| AP-P2-1 | Material graph node editor | TBD |
| AP-P2-2 | Lightmap baking (CPU path tracer) | TBD |
| AP-P2-3 | Asset bundles / DLC packs | TBD |
| AP-P2-4 | Streaming LOD meshes | TBD |

---

## Domain 8: Scripting (24 new gaps)

### P0 Scripting Blockers

| ID | Gap | Spec | Task |
|----|-----|------|------|
| SC-P0-1 | **Script Execution Safety** — infinite loops freeze engine | [script-execution-safety.md](../specs/scripting/script-execution-safety.md) | `phase1-script-execution-safety` |

### P1 Scripting

| ID | Gap | Task |
|----|-----|------|
| SC-P1-1 | Script intellisense / LSP | `phase2-script-intellisense-lsp` |
| SC-P1-2 | Static analysis / linting | `phase2-script-linting-static-analysis` |
| SC-P1-3 | Lua coroutine support | `phase1-lua-coroutines` |
| SC-P1-4 | Lua module system (require paths) | `phase1-lua-module-system` |
| SC-P1-5 | Lua DAP debugger server | `phase1-lua-dap-server` |
| SC-P1-6 | ECS component access from Lua | TBD |
| SC-P1-7 | Sandbox escape detection | Covered in script-execution-safety.md |
| SC-P1-8 | VSCode syntax highlighting | `phase1-vscode-syntax-highlighting` |

### P2 Scripting

| ID | Gap | Task |
|----|-----|------|
| SC-P2-1 | Script REPL (live console evaluation) | TBD |
| SC-P2-2 | Remote profiling (Lua flame graph) | TBD |
| SC-P2-3 | TypeScript-like annotations for Lua | TBD |

---

## Domain 9: UI System (35 new gaps)

### P0 UI Blockers

| ID | Gap | Task |
|----|-----|------|
| UI-P0-1 | **UI Profiler / DevTools** — no performance overlay for HTML/CSS UI | `phase2-ui-profiler-devtools` |
| UI-P0-2 | **Glitch-free Hot-reload** — iframe swap causes visible flash | `phase2-ui-hot-reload-stable` |
| UI-P0-3 | **UI Testing Framework** — no automated UI regression tests | `phase2-ui-testing-framework` |
| UI-P0-4 | **Reference Resolution Scaling** — UI breaks on non-1080p screens | `phase2-ui-reference-resolution` |

### P1 UI

| ID | Gap | Task |
|----|-----|------|
| UI-P1-1 | Rich text (inline bold, color, icons) | `phase2-ui-rich-text` |
| UI-P1-2 | Text input fields (keyboard capture) | `phase2-ui-text-input-fields` |
| UI-P1-3 | UI state machine / nav stack | `phase2-ui-state-machine-nav-stack` |
| UI-P1-4 | UI theme system (CSS variables) | `phase2-ui-theme-system` |
| UI-P1-5 | UI drag-and-drop | `phase3-ui-drag-drop` |
| UI-P1-6 | World-space billboard UI | `phase3-ui-world-space-billboards` |
| UI-P1-7 | Minimap widget | `phase3-ui-minimap-widget` |
| UI-P1-8 | UI Accessibility (screen reader, high contrast) | `phase3-ui-accessibility` |
| UI-P1-9 | Font fallback (CJK/emoji) | `phase3-font-fallback-cjk` |
| UI-P1-10 | Gamepad navigation | `phase1-ui-gamepad-navigation` |
| UI-P1-11 | Widget library (button, slider, checkbox) | `phase1-ui-widget-library` |

### P2 UI

| ID | Gap | Task |
|----|-----|------|
| UI-P2-1 | CSS overflow / scroll | TBD |
| UI-P2-2 | CSS grid layout | TBD |
| UI-P2-3 | Canvas 2D drawing API | TBD |
| UI-P2-4 | Video playback widget | TBD |

---

## Prioritized Action Plan

### Phase 0 (Design — Before Any Implementation)
These must be **designed and specced** before Phase 1/2 implementation begins:

1. `phase0-job-system-design` — Job system architecture (enkiTS)
2. `phase0-audio-abstraction-layer` — IAudioEngine interface + NullAudioEngine
3. `phase0-platform-abstraction-layer` — PAL for filesystem, threading, timers
4. `phase0-render-graph-design` — Render graph DAG + barrier scheduling
5. `phase0-rhi-design` — RHI vtable for DX12/Vulkan/Metal

### Phase 1 Safety Items (Before Lua Ships to Users)
1. `phase1-script-execution-safety` — Watchdog + instruction budget + memory cap

### Phase 2 Foundation Items (Before PBR Renderer Ships)
1. `phase2-bindless-resources` — Root signature redesign
2. `phase2-ibl-pipeline` — Ambient lighting for PBR
3. `phase2-render-graph-async-compute` — GPU parallelism
4. `phase2-occlusion-culling-hzb` — Visibility culling
5. `phase2-physics-multithreading` — Jolt job system bridge

---

## Gap Count Summary

| Domain | V1 Gaps | V2 New Gaps | Total |
|--------|---------|------------|-------|
| Rendering | 45 | 40 | 85 |
| Engine Core | 20 | 18 | 38 |
| Physics | 18 | 13 | 31 |
| Audio | 12 | 19 | 31 |
| Animation | 15 | 20 | 35 |
| Gameplay | 10 | 22 | 32 |
| Asset Pipeline | 12 | 24 | 36 |
| Scripting | 0 | 24 | 24 |
| UI | 5 | 35 | 40 |
| **Total** | **137** | **215** | **352** |

---

## Rulebook Tasks Created (This Analysis)

All new Rulebook tasks created from this analysis are listed in [ROADMAP.md](../ROADMAP.md) under their respective phases.

New tasks by phase:
- **Phase 0**: 5 design tasks
- **Phase 1**: 8 tasks
- **Phase 2**: 30 tasks
- **Phase 3**: 35 tasks
- **Phase 4**: 10 tasks
- **Total new**: ~88 tasks

---

*Previous analysis: [MASTER-GAP-ANALYSIS.md](MASTER-GAP-ANALYSIS.md)*
