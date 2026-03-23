# UzEngine 0.2 — Dependency Acyclic Graph (DAG)

> **Repository**: https://github.com/uzmigames/uzengine
> Milestone dependency graph. Arrows mean "depends on". Read top-to-bottom.
> **Last updated**: 2026-03-22
> Phases 1-3 are complete (✅). Phase 4 is in progress. Lua scripting (6.3) was implemented early.

---

## Visual DAG

```
                         ┌──────────────────┐
                         │  1.0 Bootstrap ✅ │
                         └────────┬─────────┘
                                  │
                         ┌────────▼─────────┐
                         │  1.1 Math &    ✅ │
                         │  Containers      │
                         └────────┬─────────┘
                                  │
                         ┌────────▼─────────┐
                         │  1.2 RHI       ✅ │
                         │  DX12+DX11+Vk+Mtl│
                         └────────┬─────────┘
                                  │
                         ┌────────▼─────────┐
                         │  1.3 Shader    ✅ │
                         │  Pipeline        │
                         │  DXC+FXC+Slang   │
                         └────────┬─────────┘
                                  │
                         ┌────────▼─────────┐
                         │  2.0 App       ✅ │
                         │  Lifecycle       │
                         └────────┬─────────┘
                                  │
                         ┌────────▼─────────┐
                         │  2.1 Job System✅ │
                         └────────┬─────────┘
                                  │
                         ┌────────▼─────────┐
                         │  2.2 Resource  ✅ │
                         │  System          │
                         └────────┬─────────┘
                                  │
   ┌──────────────────────────────┴──────────────────────────────┐
   │  Phase 3 — Two parallel tracks (BOTH COMPLETE ✅)           │
   │                                                              │
   │  Track A (Rendering) ✅          Track B (ECS/Scene) ✅      │
   │  ┌─────────────────┐            ┌─────────────────┐         │
   │  │ 3.0 Triangle  ✅ │            │ 3.0b World+Ent✅│         │
   │  └────────┬────────┘            └────────┬────────┘         │
   │           │                              │                  │
   │  ┌────────▼────────┐            ┌────────▼────────┐         │
   │  │ 3.1 Render    ✅ │            │ 3.1b Components✅│         │
   │  │     Graph       │            └────────┬────────┘         │
   │  └────────┬────────┘                     │                  │
   │           │                     ┌────────▼────────┐         │
   │  ┌────────▼────────┐            │ 3.2b Property ✅│         │
   │  │ 3.2 Forward   ✅ │            │     System      │         │
   │  │     PBR         │            └────────┬────────┘         │
   │  └────────┬────────┘                     │                  │
   │           │                     ┌────────▼────────┐         │
   │  ┌────────▼────────┐            │ 3.3b Tick     ✅│         │
   │  │ 3.3 Deferred  ✅ │            │     System      │         │
   │  └────────┬────────┘            └────────┬────────┘         │
   │           │                              │                  │
   │  ┌────────▼────────┐            ┌────────▼────────┐         │
   │  │ 3.4 Shadows   ✅ │            │ 3.4b Scene    ✅│         │
   │  │ CSM+SDF+Cache   │            │     Loader      │         │
   │  └────────┬────────┘            └────────┬────────┘         │
   │           │                              │                  │
   │  ┌────────▼────────┐                     │                  │
   │  │ 3.5 Post-Proc ✅ │                     │                  │
   │  │ GTAO+SSR+TAA+   │                     │                  │
   │  │ Bloom+Exposure   │                     │                  │
   │  └────────┬────────┘                     │                  │
   │           │                              │                  │
   └───────────┴──────────────────────────────┘                  │
               │                              │
               └──────────────┬───────────────┘
                              │ MERGE
                     ┌────────▼────────┐
                     │ 3.5b Rendering✅│
                     │ Bridge (MERGE)  │
                     └────────┬────────┘
                              │
              ┌───────────────┴────────────────────┐
              │                                    │
     ┌────────▼─────────┐               ┌──────────▼────────┐
     │  Phase 4 —        │               │  Phase 5 —        │
     │  Playable Scene   │               │  Editor           │
     │  🔨 IN PROGRESS   │               │  (overlaps Ph 4)  │
     │                   │               │                   │
     │  4.0 Input     ✅ │               │  5.0 Ext+IPC      │
     │     │             │               │     │             │
     │  4.1 Camera    ✅ │               │  5.1 Viewport     │
     │     │             │               │     │             │
     │  4.2 Physics      │               │  5.2 Hier+Insp    │
     │     │             │               │     │             │
     │  4.3 Animation    │               │  5.3 Asset Brow.  │
     │     │             │               │     │             │
     │  4.4 Audio        │               │  5.4 Gizmos       │
     │     │             │               │     │             │
     │  4.5 Prefabs      │               │  5.5 Node-Graph   │
     └────────┬──────────┘               │     │             │
              │                          │  5.6 Material Ed. │
              │                          │     │             │
              │                          │  5.7+ Advanced    │
              │                          └──────────┬────────┘
              │                                     │
              └─────────────────┬───────────────────┘
                                │
                ┌───────────────┼───────────────┐
                │               │               │
     ┌──────────▼────┐ ┌────────▼──────┐ ┌──────▼──────────┐
     │  Phase 6 —    │ │  Phase 7 —    │ │  Phase 8 —      │
     │  Adv Gameplay │ │  Content &    │ │  MCP & AI       │
     │               │ │  Polish       │ │                 │
     │  6.0 Nav      │ │  (after 3+6)  │ │  (after Ph 5)   │
     │  6.1 Beh Tree │ │               │ │                 │
     │  6.2 C++ HR   │ │  7.0 Cooking  │ │  8.0 MCP Server │
     │  6.3 Lua   ✅ │ │  7.1 Adv Post │ │  8.1 AI Tools   │
     │  6.4 UI       │ │  7.2 Stream.  │ │  8.2 AI Content │
     │  6.5 Save     │ │  7.3 Terrain  │ │     Pipeline    │
     └──────────┬────┘ │  7.4 VFX      │ │  (VFX AI Tool)  │
                │      │  7.5 Adv Rend │ └──────┬──────────┘
                │      └────────┬──────┘        │
                │               │               │
                └───────────────┼───────────────┘
                                │
                       ┌────────▼─────────┐
                       │  Phase 9 —        │
                       │  Shipping         │
                       │                   │
                       │  9.0 Platform SDK │
                       │  9.1 Networking   │
                       │  9.2 Optimization │
                       │  9.3 Release      │
                       └───────────────────┘
```

---

## Dependency Table

| Milestone | Depends On | Blocks | Status |
|-----------|-----------|--------|--------|
| **1.0** Bootstrap | — | 1.1 | ✅ |
| **1.1** Math & Containers | 1.0 | 1.2 | ✅ |
| **1.2** RHI (DX12 + DX11 + Vulkan + Metal) | 1.1 | 1.3 | ✅ |
| **1.3** Shader Pipeline | 1.2 | 2.0 | ✅ |
| **2.0** Application Lifecycle | 1.3 | 2.1 | ✅ |
| **2.1** Job System | 2.0 | 2.2 | ✅ |
| **2.2** Resource System | 2.1 | 3.0, 3.0b | ✅ |
| **3.0** Triangle | 2.2 | 3.1 | ✅ |
| **3.1** Render Graph | 3.0 | 3.2 | ✅ |
| **3.2** Forward PBR | 3.1 | 3.3 | ✅ |
| **3.3** Deferred | 3.2 | 3.4 | ✅ |
| **3.4** Shadows (CSM + SDF + Cache) | 3.3 | 3.5 | ✅ |
| **3.5** Post-Process (GTAO, SSR, TAA, Bloom, Exposure) | 3.4 | 3.5b | ✅ |
| **3.0b** World + Entity (EnTT ECS) | 2.2 | 3.1b | ✅ |
| **3.1b** Components | 3.0b | 3.2b | ✅ |
| **3.2b** Property System | 3.1b | 3.3b | ✅ |
| **3.3b** Tick System | 3.2b | 3.4b | ✅ |
| **3.4b** Scene Loader | 3.3b | 3.5b | ✅ |
| **3.5b** Rendering Bridge (MERGE) | 3.5 + 3.4b | 4.0, 5.0 | ✅ |
| **4.0** Input | 3.5b | 4.1, 5.0 | ✅ |
| **4.1** Camera | 4.0 | 4.2 | ✅ |
| **4.2** Physics (Jolt) | 4.1 | 4.3 | Planned |
| **4.3** Animation | 4.2 | 4.4 | Planned |
| **4.4** Audio | 4.3 | 4.5 | Planned |
| **4.5** Prefabs | 4.4 | 6.0 | Planned |
| **5.0** Extension + IPC | 4.0 | 5.1 | Planned |
| **5.1** Viewport | 5.0 | 5.2 | Planned |
| **5.2** Hierarchy + Inspector | 5.1 | 5.3 | Planned |
| **5.3** Asset Browser | 5.2 | 5.4 | Planned |
| **5.4** Gizmos | 5.3 | 5.5 | Planned |
| **5.5** Node-Graph | 5.4 | 5.6 | Planned |
| **5.6** Material Editor | 5.5 | 5.7+, 8.0 | Planned |
| **6.0** Navigation | 4.5 | 6.1 | Planned |
| **6.1** Behavior Trees | 6.0 | 6.2 | Planned |
| **6.2** C++ Hot-Reload | 6.1 | 6.3 | Planned |
| **6.3** Lua Scripting | — (done early) | 6.4 | ✅ |
| **6.4** UI | 6.3 | 6.5 | Planned |
| **6.5** Save | 6.4 | 7.0, 9.x | Planned |
| **7.0** Asset Cooking | 3.5b + 6.5 | 7.1 | Planned |
| **7.1** Advanced Post-Process | 7.0 | 7.2 | Planned |
| **7.2** Streaming | 7.1 | 7.3 | Planned |
| **7.3** Terrain | 7.2 | 7.4 | Planned |
| **7.4** VFX | 7.3 | 7.5 | Planned |
| **7.5** Advanced Rendering | 7.4 | 9.x | Planned |
| **8.0** MCP Server | 5.6 | 8.1 | Planned |
| **8.1** AI Tools | 8.0 | 8.2 | Planned |
| **8.2** AI Content Pipeline (VFX AI Tool) | 8.1 | 9.x | Planned |
| **9.0** Platform SDKs | 7.5 + 8.2 | 9.1 | Planned |
| **9.1** Networking | 9.0 | 9.2 | Planned |
| **9.2** Optimization | 9.1 | 9.3 | Planned |
| **9.3** Release | 9.2 | — | Planned |

---

## Critical Path (longest dependency chain)

```
1.0 → 1.1 → 1.2 → 1.3 → 2.0 → 2.1 → 2.2
  → 3.0 → 3.1 → 3.2 → 3.3 → 3.4 → 3.5
  → 3.5b (MERGE with Track B)
  → 4.0 → 4.1 → 4.2 → 4.3 → 4.4 → 4.5
  → 6.0 → 6.1 → 6.2 → 6.3 → 6.4 → 6.5
  → 7.0 → 7.1 → 7.2 → 7.3 → 7.4 → 7.5
  → 9.0 → 9.1 → 9.2 → 9.3
```

**Critical path length**: 30 milestones (Bootstrap to Release)
**Current position**: 4.2 Physics (next unblocked item on critical path)

> **Note**: Lua scripting (6.3) was implemented early, out of sequence, breaking the Phase 6 dependency chain. The remaining critical path runs through Physics → Animation → Audio → Prefabs → Navigation → Behavior Trees → C++ Hot-Reload → UI → Save.

---

## Parallelization Opportunities

| Window | Can Run In Parallel |
|--------|-------------------|
| ✅ After 1.3 (done) | Phase 2 ran linearly — all complete |
| ✅ After 2.2 (done) | Track A (3.0→3.5) + Track B (3.0b→3.4b) ran in parallel — both complete |
| **NOW** — After 3.5b (done) | **Phase 4** (4.2 Physics next) + **Phase 5** (editor) can start simultaneously |
| After 4.5 (Prefabs done) | **Phase 6** (minus Lua) + **Phase 7** can start independently |
| After Phase 5 complete | **Phase 8** (MCP & AI) begins |
| After 7.5 + 8.2 | **Phase 9** (shipping) begins |

---

## Validated Nodes

```
✅ Phase 1 — Foundation (COMPLETE)
   ✅ 1.0 — Bootstrap
      ✅ Logging (UZ_LOG categories + spdlog backend)
      ✅ Assertions (UZ_CHECK, UZ_ENSURE, UZ_VERIFY)
      ✅ Profiler (Tracy integration, UZ_PROFILE_SCOPE)
      ✅ Memory (arena allocators, engine allocator wrappers)
      ✅ Window (Win32 HWND creation)
      ✅ Config (JSON-based project config)
      ✅ Test harness (doctest, 95%+ coverage gate)

   ✅ 1.1 — Math & Containers
      ✅ Vec2, Vec3, Vec4 (SIMD via DirectXMath)
      ✅ Mat4 (column-major, XMFLOAT4X4)
      ✅ Quat (slerp/lerp, axis-angle, FromAxisAngle)
      ✅ Transform (compose, inverse, hierarchy)
      ✅ AABB (bounds)
      ✅ HandleMap (generational handles)
      ✅ Arena allocators

   ✅ 1.2 — RHI (4 backends)
      ✅ DX12 SM6.6 (primary) — device, swapchain, command lists, fences, D3D12MA, DRED, bindless
      ✅ DX11 — full feature parity with DX12 for SM5.0 path
      ✅ Vulkan 1.3 — swapchain, fences, GPU profiler
      ✅ Metal — command lists, fences, GPU profiler (macOS)
      ✅ PSO cache (disk-persistent), async compute telemetry
      ✅ GPU breadcrumbs, descriptor allocator, frame pacer
      ✅ 6,069 FPS clear screen on RTX 4090

   ✅ 1.3 — Shader Pipeline
      ✅ DXC compiler (SM6.6+, SPIR-V capable)
      ✅ FXC compiler (SM5.0 fallback, #ifdef UZ_BINDLESS dual-path)
      ✅ Slang compiler integration
      ✅ Shader model negotiation (NegotiateShaderModel, UZ_FORCE_SM5)
      ✅ VFS (file loading in dev, precompiled archive in ship)
      ✅ xxHash64 content-hash cache (source + includes + defines + target)
      ✅ Hot-reload (FileWatcher + reverse-dependency graph + dirty flag)
      ✅ Shader reflection (parameter extraction)
      ✅ Permutation system (define-based variants)
      ✅ Asset foundation (.uzshader format, .cache/ directory)

✅ Phase 2 — Runtime Framework (COMPLETE)
   ✅ 2.0 — Application Lifecycle
      ✅ Engine loop (fixed + variable timestep)
      ✅ Subsystem registry (ordered init/shutdown)
      ✅ Module registry
      ✅ Time manager
      ✅ Project loader (.uzproj)

   ✅ 2.1 — Job System
      ✅ Job system (thread pool)
      ✅ Job counters + fences

   ✅ 2.2 — Resource System
      ✅ Asset registry + importers
      ✅ .uzshader, .uzmesh, .uztexture, .uzsdf formats
      ✅ Import sidecars (.import.json)
      ✅ Asset manifest + asset cache

✅ Phase 3 Track A — Rendering (COMPLETE)
   ✅ 3.0 — Triangle (clear_screen + triangle demos)
   ✅ 3.1 — Render Graph (DAG-based pass scheduling)
   ✅ 3.2 — Forward PBR (GGX BRDF, Burley diffuse, pbr_test demo)
   ✅ 3.3 — Deferred Rendering (GBuffer + deferred lighting, deferred_test demo)
   ✅ 3.4 — Shadows
      ✅ Cascaded Shadow Maps (4 cascades, 2048x2048 atlas)
      ✅ Shadow cache (static object caching)
      ✅ SDF shadows (distance field shadow atlas)
      ✅ Local light shadow atlas (point/spot)
      ✅ Poisson-disk 16-tap PCF
   ✅ 3.5 — Post-Processing
      ✅ GTAO (ambient occlusion + denoise + temporal + upsample)
      ✅ SSR (screen-space reflections — trace + temporal + resolve)
      ✅ TAA (temporal anti-aliasing, Halton jitter)
      ✅ TSR (temporal super resolution)
      ✅ Bloom (downsample + threshold + upsample + composite)
      ✅ Auto Exposure (histogram + average)
      ✅ Tonemap (ACES + film controls)
      ✅ Hi-Z Pyramid (hierarchical depth)
      ✅ Velocity pass (motion vectors)
      ✅ Lens flare, FXAA, CAS sharpen, color grading LUT
      ✅ Depth of Field, motion blur
      ✅ SSGI (screen-space GI — trace + temporal + spatial denoise + upsample)

   ✅ Atmosphere & Sky (bonus — beyond original plan)
      ✅ Sky atmosphere (Bruneton model, transmittance LUT, multi-scatter LUT)
      ✅ Aerial perspective volume
      ✅ Height fog (exponential)
      ✅ Volumetric clouds (ray march + temporal + shape/detail noise + weather)
      ✅ Sky cubemap capture (compute + pixel shader paths)
      ✅ God rays

   ✅ IBL & Lighting (bonus — beyond original plan)
      ✅ IBL pipeline (BRDF LUT, irradiance, prefiltered specular)
      ✅ Reflection probes (ECS component + IBL bake + shader blending)
      ✅ Instance batching (GPU instancing)
      ✅ GPU culling (compute-driven)
      ✅ Depth prepass

✅ Phase 3 Track B — ECS/Scene (COMPLETE)
   ✅ 3.0b — World + Entity (EnTT ECS, World, WorldSubsystem)
   ✅ 3.1b — Components
      ✅ CameraComponent, SceneComponent, PrimitiveComponent
      ✅ LightComponent (directional, point, spot)
      ✅ StaticMeshComponent
      ✅ SkyAtmosphereComponent, SkyLightComponent
      ✅ HeightFogComponent, VolumetricCloudsComponent
      ✅ ReflectionProbeComponent
   ✅ 3.2b — Property System (ClassInfo, ClassRegistry, PropertySerializer)
   ✅ 3.3b — Tick System (TickableComponent, TickSystem, TickSubsystem)
   ✅ 3.4b — Scene Loader (.uzscene format)

✅ 3.5b — Rendering Bridge (MERGE) — Track A + Track B integrated

🔨 Phase 4 — Playable Scene (IN PROGRESS)
   ✅ 4.0 — Input (Lua input API — keyboard/mouse polling)
   ✅ 4.1 — Camera (camera system + Lua camera API)
      4.2 — Physics (Jolt) — NEXT
      4.3 — Animation
      4.4 — Audio
      4.5 — Prefabs

✅ Phase 6.3 — Lua Scripting (DONE EARLY)
   ✅ LuaJIT 2.1 VM with sandbox (no os.execute, io.open, load, require)
   ✅ Script system (ScriptComponent, per-node on_start/on_update/on_stop)
   ✅ Lua APIs: time, input, math, quat, camera, entity, scene, light, environment, renderer, window, VFS

   14 demos: clear_screen, triangle, textured_quad, mesh_test, pbr_test,
             deferred_test, ibl_test, instancing_test, postprocess_test,
             fbx_test, cornell_box, camera_test, + common lib
```

---

## Codebase Stats (2026-03-22)

| Metric | Count |
|--------|-------|
| C++ source files (.cpp) | 213 |
| C++ header files (.h) | 274 |
| HLSL shaders (.hlsl) | 77 |
| Shader includes (.hlsli) | 19 |
| RHI backends | 4 (DX12, DX11, Vulkan, Metal) |
| Demos | 14 |
| Material system | Full graph-based (expressions, baker, GPU baker, shader cache) |

---

*Reference: [engine/README.md](engine/README.md) | [roadmap.md](roadmap.md) | [pdr.md](pdr.md) | [docs/engine/](engine/)*
