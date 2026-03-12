# UzEngine 0.2 — Dependency Acyclic Graph (DAG)

> **Repository**: https://github.com/uzmigames/uzengine
> Milestone dependency graph. Arrows mean "depends on". Read top-to-bottom.
> Phase 1 is fully completed (✅). Phase 2 starts next.

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
                         │  (DX12 SM6.6)    │
                         │  6,069 FPS       │
                         └────────┬─────────┘
                                  │
                         ┌────────▼─────────┐
                         │  1.3 Shader    ✅ │
                         │  Pipeline        │
                         │  DXC+FXC,90 tests│
                         └────────┬─────────┘
                                  │
              ┌───────────────────┼───────────────────┐
              │                   │                   │
     ┌────────▼─────────┐┌────────▼─────────┐┌────────▼─────────┐
     │  2.0 Application  ││                  ││                  │
     │  Lifecycle        ││                  ││                  │
     └────────┬──────────┘│                  ││                  │
              │           │  (sequential)    ││                  │
     ┌────────▼─────────┐ │                  ││                  │
     │  2.1 Job System   │ │                  ││                  │
     └────────┬──────────┘ └──────────────────┘└──────────────────┘
              │
     ┌────────▼─────────┐
     │  2.2 Resource     │
     │  System           │
     └────────┬──────────┘
              │
   ┌──────────┴───────────────────────┐
   │  Phase 3 — Two parallel tracks   │
   │                                   │
   │  Track A (Rendering)              │   Track B (ECS/Scene)
   │  ┌─────────────────┐              │   ┌─────────────────┐
   │  │ 3.0 Triangle    │              │   │ 3.0b World+Ent  │
   │  └────────┬────────┘              │   └────────┬────────┘
   │           │                       │            │
   │  ┌────────▼────────┐              │   ┌────────▼────────┐
   │  │ 3.1 Render Graph│              │   │ 3.1b Components │
   │  └────────┬────────┘              │   └────────┬────────┘
   │           │                       │            │
   │  ┌────────▼────────┐              │   ┌────────▼────────┐
   │  │ 3.2 Forward PBR │              │   │ 3.2b Property   │
   │  └────────┬────────┘              │   │     System      │
   │           │                       │   └────────┬────────┘
   │  ┌────────▼────────┐              │            │
   │  │ 3.3 Deferred    │              │   ┌────────▼────────┐
   │  └────────┬────────┘              │   │ 3.3b Behaviors  │
   │           │                       │   └────────┬────────┘
   │  ┌────────▼────────┐              │            │
   │  │ 3.4 Shadows     │              │   ┌────────▼────────┐
   │  └────────┬────────┘              │   │ 3.4b Scene      │
   │           │                       │   │     Loader      │
   │  ┌────────▼────────┐              │   └────────┬────────┘
   │  │ 3.5 Post-Process│              │            │
   │  └────────┬────────┘              │            │
   │           │                       │            │
   └───────────┴───────────────────────┴────────────┘
               │                                    │
               └──────────────┬─────────────────────┘
                              │ MERGE
                     ┌────────▼────────┐
                     │ 3.5b Rendering  │
                     │ Bridge (MERGE)  │
                     └────────┬────────┘
                              │
              ┌───────────────┴────────────────────┐
              │                                    │
     ┌────────▼─────────┐               ┌──────────▼────────┐
     │  Phase 4 —        │               │  Phase 5 —        │
     │  Playable Scene   │               │  Editor           │
     │  (linear)         │               │  (overlaps Ph 4)  │
     │                   │               │                   │
     │  4.0 Input        │               │  5.0 Ext+IPC      │
     │     │             │               │     │             │
     │  4.1 Camera       │               │  5.1 Viewport     │
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
     │  6.3 Lua      │ │  7.1 Adv Post │ │  8.1 AI Tools   │
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

| Milestone | Depends On | Blocks |
|-----------|-----------|--------|
| **1.0** Bootstrap | — | 1.1 |
| **1.1** Math & Containers | 1.0 | 1.2 |
| **1.2** RHI (DX12 SM6.6) | 1.1 | 1.3 |
| **1.3** Shader Pipeline | 1.2 | 2.0 |
| **2.0** Application Lifecycle | 1.3 | 2.1 |
| **2.1** Job System | 2.0 | 2.2 |
| **2.2** Resource System | 2.1 | 3.0, 3.0b |
| **3.0** Triangle | 2.2 | 3.1 |
| **3.1** Render Graph | 3.0 | 3.2 |
| **3.2** Forward PBR | 3.1 | 3.3 |
| **3.3** Deferred | 3.2 | 3.4 |
| **3.4** Shadows | 3.3 | 3.5 |
| **3.5** Post-Process | 3.4 | 3.5b |
| **3.0b** World + Entity | 2.2 | 3.1b |
| **3.1b** Components | 3.0b | 3.2b |
| **3.2b** Property System | 3.1b | 3.3b |
| **3.3b** Behaviors | 3.2b | 3.4b |
| **3.4b** Scene Loader | 3.3b | 3.5b |
| **3.5b** Rendering Bridge (MERGE) | 3.5 + 3.4b | 4.0, 4.0+ (Phase 5 start) |
| **4.0** Input | 3.5b | 4.1, 5.0 |
| **4.1** Camera | 4.0 | 4.2 |
| **4.2** Physics (Jolt) | 4.1 | 4.3 |
| **4.3** Animation | 4.2 | 4.4 |
| **4.4** Audio | 4.3 | 4.5 |
| **4.5** Prefabs | 4.4 | 6.0 |
| **5.0** Extension + IPC | 4.0 | 5.1 |
| **5.1** Viewport | 5.0 | 5.2 |
| **5.2** Hierarchy + Inspector | 5.1 | 5.3 |
| **5.3** Asset Browser | 5.2 | 5.4 |
| **5.4** Gizmos | 5.3 | 5.5 |
| **5.5** Node-Graph | 5.4 | 5.6 |
| **5.6** Material Editor | 5.5 | 5.7+, 8.0 |
| **6.0** Navigation | 4.5 | 6.1 |
| **6.1** Behavior Trees | 6.0 | 6.2 |
| **6.2** C++ Hot-Reload | 6.1 | 6.3 |
| **6.3** Lua | 6.2 | 6.4 |
| **6.4** UI | 6.3 | 6.5 |
| **6.5** Save | 6.4 | 7.0, 9.x |
| **7.0** Asset Cooking | 3.5b + 6.5 | 7.1 |
| **7.1** Advanced Post-Process | 7.0 | 7.2 |
| **7.2** Streaming | 7.1 | 7.3 |
| **7.3** Terrain | 7.2 | 7.4 |
| **7.4** VFX | 7.3 | 7.5 |
| **7.5** Advanced Rendering | 7.4 | 9.x |
| **8.0** MCP Server | 5.6 | 8.1 |
| **8.1** AI Tools | 8.0 | 8.2 |
| **8.2** AI Content Pipeline (VFX AI Tool) | 8.1 | 9.x |
| **9.0** Platform SDKs | 7.5 + 8.2 | 9.1 |
| **9.1** Networking | 9.0 | 9.2 |
| **9.2** Optimization | 9.1 | 9.3 |
| **9.3** Release | 9.2 | — |

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

> **Note**: Track B (ECS/Scene: 3.0b → 3.4b) runs in parallel with Track A and must complete before 3.5b merge. Track B is 6 milestones and Track A is 6 milestones — they are equal length and form the longest Phase 3 chain together.

---

## Parallelization Opportunities

| Window | Can Run In Parallel |
|--------|-------------------|
| After 1.3 (Shader Pipeline done) | Phase 2 begins (linear, so no parallelism within Phase 2) |
| After 2.2 (Resource System done) | **Track A** (3.0→3.5) + **Track B** (3.0b→3.4b) run fully in parallel |
| After 3.5b (Phase 3 merged) | **Phase 4** (gameplay, linear) + **Phase 5** (editor, starts at 4.0) — both begin simultaneously |
| After 4.5 (Prefabs done) | **Phase 6** (gameplay) + **Phase 7** (content, needs 3.5b+6.5) can start independently |
| After Phase 5 complete | **Phase 8** (MCP & AI) begins |
| After 7.5 + 8.2 | **Phase 9** (shipping) begins |

---

## Validated Nodes (Phase 1 — COMPLETE ✅)

```
✅ 1.0 — Bootstrap
   ✅ Logging (UZ_LOG categories + spdlog backend)
   ✅ Assertions (UZ_CHECK, UZ_ENSURE)
   ✅ Profiler (Tracy integration, UZ_PROFILE_SCOPE)
   ✅ Memory (arena allocators, engine allocator wrappers)
   ✅ Window (Win32 HWND creation)
   ✅ Config (JSON-based project config)
   ✅ Test harness (Catch2, 95%+ coverage gate)

✅ 1.1 — Math & Containers
   ✅ Vec2, Vec3, Vec4 (SIMD via DirectXMath)
   ✅ Mat4 (column-major, XMFLOAT4X4)
   ✅ Quat (slerp/lerp, axis-angle, FromAxisAngle)
   ✅ Transform (compose, inverse, hierarchy)
   ✅ AABB (bounds)
   ✅ HandleMap (generational handles)
   ✅ Arena allocators

✅ 1.2 — RHI (DX12 SM6.6)
   ✅ Device creation + adapter enumeration
   ✅ SwapChain (triple buffered, tearing)
   ✅ Command lists + fences
   ✅ D3D12MA v3.1.0 (memory allocator)
   ✅ DRED (device removed extended data)
   ✅ Bindless SM6.6 (ResourceDescriptorHeap)
   ✅ 6,069 FPS clear screen on RTX 4090
   ✅ 183 tests passing

✅ 1.3 — Shader Pipeline
   ✅ DXC compiler (SM6.6+, SPIR-V capable)
   ✅ FXC compiler (SM5.0 fallback, #ifdef UZ_BINDLESS dual-path)
   ✅ Shader model negotiation (NegotiateShaderModel, UZ_FORCE_SM5)
   ✅ VFS (file loading in dev, precompiled archive in ship)
   ✅ xxHash64 content-hash cache (source + includes + defines + target)
   ✅ Hot-reload (FileWatcher + reverse-dependency graph + dirty flag)
   ✅ Shader reflection (parameter extraction)
   ✅ Permutation system (define-based variants)
   ✅ Asset foundation (.uzshader format, .cache/ directory)
   ✅ 90 tests passing, 5 demos
```

---

*Reference: [ARCHITECTURE.md](ARCHITECTURE.md) | [ROADMAP.md](ROADMAP.md) | [PDR.md](PDR.md) | [docs/engine/](engine/)*
