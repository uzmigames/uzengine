# UzEngine 0.2 — Roadmap Analysis Report

> **Status**: PENDING APPROVAL — Do not update roadmap.md until approved.
> **Date**: 2026-03-12
> **Sources**: 8 parallel agent analyses (UE5, Godot, Unity architects + RHI, ECS, Editor, Asset Pipeline, Gameplay engineers + Codebase explorer)

---

## 1. Current State (What Actually Exists)

Before planning forward, here's what's **implemented and working** (661+ tests, 31K LOC):

| System | Status | Evidence |
|--------|--------|----------|
| Core (logging, assertions, paths, config, memory) | **DONE** | 50+ tests |
| Math (Vec2-4, Mat4, Quat, Transform, SIMD) | **DONE** | 70 tests |
| Containers (RingBuffer, FreeList, HandleMap) | **DONE** | 28 tests |
| RHI DX12 SM6.6 (device, swapchain, descriptors, fences, D3D12MA, DRED) | **DONE** | 33 tests, 6069 FPS |
| Shader Pipeline (DXC+FXC, VFS, cache, hot-reload, reflection, permutations) | **DONE** | 48 tests |
| Asset Foundation (VFS, cache, importer registry, sidecar, PAK format) | **DONE** | 42 tests |
| Basic Renderer (camera, mesh builder, constant buffers, upload context) | **PARTIAL** | Scaffolding |
| Demos (clear screen, triangle, textured quad, mesh test, camera test) | **WORKING** | 5 demos |
| Platform (Win32 window, keyboard/mouse input) | **DONE** | 6 tests |

**NOT implemented**: ECS, Scene, Physics, Animation, Audio, Editor, Scripting, or any gameplay system.

---

## 2. Critical Problems in Current Roadmap

All 8 agents converged on these issues:

### Problem 1: Application Lifecycle doesn't exist — every demo repeats 200+ lines of boilerplate
- **UE5 agent**: "FEngineLoop is the FIRST thing that runs. It IS the engine."
- **Impact**: Every demo duplicates init/loop/shutdown. Bugs multiply. No subsystem ordering.
- **Fix**: Build lifecycle framework NEXT.

### Problem 2: Scene/ECS comes AFTER full renderer — inverted dependency
- **UE5 agent**: "UWorld exists independently of the renderer. The renderer depends on components, not the reverse."
- **Godot agent**: "Build a thin RenderServer API at 1.3.0, then scene system can start immediately."
- **Impact**: All renderer demos use throwaway hardcoded draw calls that get rewritten when ECS arrives.
- **Fix**: Build ECS core in parallel with renderer. Scene populates what renderer draws.

### Problem 3: Job System planned for "Phase 1.5" — too late
- **UE5 agent**: "FTaskGraphInterface is initialized before RHI, before window creation."
- **Unity agent**: "Move to 1.2.5. Visibility culling is embarrassingly parallel."
- **Impact**: Shader compilation single-threaded, asset loading blocks main thread, no parallel culling.
- **Fix**: Job system as early foundation, not late addition.

### Problem 4: Render Graph placement is debated
- **Unity agent**: "Build RenderGraph BEFORE deferred. Never write manual barriers."
- **UE5 agent**: "RenderGraph is an optimization layer. UE4 shipped 4.0-4.21 without it."
- **RHI agent**: "Build after 1.3.0 (triangle) but before 1.3.1 (deferred)."
- **Consensus**: Render Graph between triangle-to-screen and deferred pipeline.

### Problem 5: Editor and MCP come last — wastes the engine's differentiator
- **Godot agent**: "Every month spent on advanced rendering before MCP exists is a month where AI cannot help build content."
- **Editor agent**: "Start incrementally during Scene System, not after all of Phase 1."
- **Fix**: Editor scaffold + basic panels alongside scene system. MCP scene tools at 1.4, not 3.0.

### Problem 6: Property System is Tier 3 but gates everything
- **ECS agent**: "Property System is a HARD GATE. Scene loading, inspector, hot-reload, prefabs ALL depend on it."
- **Fix**: Property System MVP immediately after Component Model.

### Problem 7: Asset importers (mesh/texture) planned for 1.5.1 — too late
- **Asset Pipeline agent**: "TextureImporter v1 and MeshImporter v1 must exist BEFORE 1.3.0 concludes."
- **Fix**: Minimal importers (stb_image + fastgltf) before renderer milestone.

### Problem 8: Advanced rendering (DLSS/VSM/fog) blocks scene system
- **All agents agree**: Defer DLSS, FSR, XeSS, virtual shadow maps, volumetric fog, sky/atmosphere to polish phase.
- **Godot agent**: "None of these affect the editor or AI pipeline. They are polish, not foundation."

---

## 3. Consensus Across All Agents

### What they ALL agree on:

1. **Application Lifecycle → Job System → VFS/Assets → ECS** must come before advanced rendering
2. **Property System is early foundation**, not Tier 3
3. **Render Graph goes between triangle and deferred** (not before, not much after)
4. **Editor development should be incremental**, matching runtime milestones
5. **Advanced rendering (DLSS, VSM, volumetrics) = Phase 4 polish**
6. **Input is the first gameplay system** (everything needs it for testing)
7. **Lua scripting comes late** (C++ behaviors suffice; Lua needs stable APIs to bind)
8. **Materials don't depend on Render Graph** (inverted dependency in current docs)

### Where they disagree:

| Topic | UE5 View | Godot View | Unity View | Resolution |
|-------|----------|------------|------------|------------|
| ECS complexity | Full sparse-set from start | Simple pointer-based first | ECS for hot paths only | **Start simple, profile, optimize** |
| Render Graph timing | After passes exist | Thin command API first | Before deferred (no compat mode) | **After triangle, before deferred** |
| Editor start | After runtime foundation | Immediately (editor-first) | After runtime is stable | **Incrementally alongside scene** |
| Forward PBR step | Yes, before deferred | Yes, minimal | Ship simple first | **Yes — forward PBR validates full pipeline** |

---

## 4. Proposed New Roadmap

### Phase 1: Foundation (DONE)

| # | Milestone | Status |
|---|-----------|--------|
| 1.0 | Bootstrap & Project Setup | ✅ DONE |
| 1.1 | Math & Containers | ✅ DONE |
| 1.2 | RHI (DX12 SM6.6) | ✅ DONE |

### Phase 2: Runtime Framework (NEW — currently missing)

| # | Milestone | Duration | Justification |
|---|-----------|----------|---------------|
| 2.0 | **Application Lifecycle** | ~1 week | Init/tick/shutdown framework. Eliminates 200+ lines boilerplate per demo. Every subsystem registers. |
| 2.1 | **Job System** (basic) | ~1 week | Work-stealing, ParallelFor, Counter/Fence. Unblocks parallel shader compile, async asset load, culling. |
| 2.2 | **Resource System** (VFS + loaders) | ~1 week | Minimal mesh importer (fastgltf → .uzmesh), texture loader (stb_image → RGBA8). Load assets by path. |

### Phase 3: First Pixels (Rendering + ECS in parallel)

**Track A — Rendering:**

| # | Milestone | Duration | Notes |
|---|-----------|----------|-------|
| 3.0 | Triangle to Screen | ~1 week | Vertex/index buffers, camera, textured mesh. Manual command lists. |
| 3.1 | Render Graph | ~1.5 weeks | DAG, auto barriers, transient aliasing. Bridge milestone. |
| 3.2 | Forward PBR | ~1 week | Single light, Cook-Torrance. Validates full material→mesh→light pipeline. |
| 3.3 | Deferred Pipeline | ~2 weeks | GBuffer (albedo, normal, metalRough, depth) + lighting pass. Built ON render graph. |
| 3.4 | Shadows (CSM) | ~1 week | 4-cascade directional shadow maps. |
| 3.5 | Basic Post-Processing | ~1 week | Tonemap (ACES) + Bloom + FXAA only. Rest deferred. |

**Track B — ECS/Scene (parallel with Track A from 3.0):**

| # | Milestone | Duration | Notes |
|---|-----------|----------|-------|
| 3.0b | World + Entity | ~1 week | Generational handles, sparse sets, entity CRUD. No rendering dependency. |
| 3.1b | Component Model | ~1 week | Component/ActorComponent/SceneComponent, hierarchy, lifecycle. |
| 3.2b | Property System (MVP) | ~1 week | ClassRegistry, UZ_PROPERTY, JSON serialize/deserialize. **Gates everything downstream.** |
| 3.3b | Behaviors + Tick | ~1 week | Behavior base, UZ_REGISTER_BEHAVIOR, TickGroup, OnTick. |
| 3.4b | Scene Loader | ~1 week | .uzscene JSON → World. Headless testable. |
| 3.5b | Rendering Bridge | ~1 week | PrimitiveComponent → RenderProxy → RenderWorld. Merge Track A + B. |

**Merge point**: After 3.5 + 3.5b, we have a **loaded .uzscene rendering with deferred PBR**.

### Phase 4: Playable Scene

| # | Milestone | Duration | Notes |
|---|-----------|----------|-------|
| 4.0 | Input System | ~1 week | Enhanced Input, action mapping, contexts. First gameplay system. |
| 4.1 | Camera System | ~1 week | SpringArm, third-person, orbit. Needs input. |
| 4.2 | Physics (Jolt) | ~2 weeks | Rigid bodies, queries, character controller, triggers. |
| 4.3 | Animation (core) | ~2 weeks | Skeleton, GPU skinning, state machines, blend spaces. |
| 4.4 | Audio (core) | ~1 week | miniaudio, 3D spatial, bus mixing. |
| 4.5 | Prefabs + Hot-Reload | ~1 week | .uzprefab, asset hot-reload via FileWatcher. |

**Demo gate**: Third-person character in a lit scene with physics, animation, and audio.

### Phase 5: Editor (Incremental — starts during Phase 4)

| # | Milestone | When | Notes |
|---|-----------|------|-------|
| 5.0 | Extension Scaffold + IPC | Parallel with 4.0 | package.json, N-API bridge, message format. Pure TS + C bridge. |
| 5.1 | Console Panel | With 5.0 | Log streaming via IPC callbacks. Zero runtime deps. |
| 5.2 | Viewport Panel | After 3.5 merge | HWND + swapchain + camera orbit in VSCode. |
| 5.3 | Hierarchy + Inspector (basic) | After 3.5b | Tree view, Bool/Int/Float/String/Vec3 properties. |
| 5.4 | Asset Browser | After 5.3 | File listing, drag-drop. Thumbnails later. |
| 5.5 | Gizmos + Selection | After 5.3 | Translate/rotate/scale handles, click-to-select. |
| 5.6 | Undo/Redo | After 5.5 | ICommand stack with transactions. |
| 5.7 | Node-Graph Framework | After 5.6 | Shared canvas (BaseGraphEditor). Pure TS, no runtime deps. |
| 5.8 | Material Editor | After 4.3+ shader work | First specialized node-graph editor. |
| 5.9 | Animation Controller | After 4.3 | State machine editor. |
| 5.10 | Advanced Editors | After respective runtime systems | BT, VFX, PCG, Sequencer, Control Rig. |

### Phase 6: Advanced Gameplay

| # | Milestone | Notes |
|---|-----------|-------|
| 6.0 | Navigation (Recast/Detour) | NavMesh baking + pathfinding |
| 6.1 | Behavior Trees | Sequence/Selector/Blackboard |
| 6.2 | C++ Game DLL + Hot-Reload | Separate DLL, serialize → reload → deserialize |
| 6.3 | Lua Scripting | LuaJIT, sandbox, bindings to all C++ APIs |
| 6.4 | UI Runtime | HTML/CSS renderer, data binding |
| 6.5 | Save System | msgpack, slots, autosave |

### Phase 7: Content & Polish

| # | Milestone | Notes |
|---|-----------|-------|
| 7.0 | Asset Cooking (full) | BC7/BC5/BC6H compression, LOD generation, .uzpak packager |
| 7.1 | Advanced Post-Processing | SSAO, SSR, TAA, DOF, motion blur (as micro-milestones) |
| 7.2 | World Streaming | Grid partition, async load/unload |
| 7.3 | Terrain + Foliage | Heightmap, clipmap LOD, GPU instancing |
| 7.4 | VFX/Particles | GPU compute, emitter modules |
| 7.5 | Advanced Rendering | DLSS/FSR/XeSS, virtual shadow maps, volumetric fog, sky/atmosphere |

### Phase 8: MCP & AI

| # | Milestone | Notes |
|---|-----------|-------|
| 8.0 | MCP Server + Scene Tools | create/modify/delete entities, screenshot |
| 8.1 | AI Content Pipeline | Texture/model/audio generation via LLM |
| 8.2 | End-to-end AI workflows | Prompt → complete VFX/character/level |

### Phase 9: Shipping

| # | Milestone | Notes |
|---|-----------|-------|
| 9.0 | Platform Services | Steam, upscaling fallback chain |
| 9.1 | Networking | Replication, RPCs, dedicated server |
| 9.2 | Optimization | GPU-driven rendering, Nanite-style, hardware RT |
| 9.3 | Release | CI/CD, marketplace, docs, templates |

---

## 5. Key Architecture Decisions (from analysis)

### Decision 1: Materials DON'T depend on Render Graph
**Current docs say**: Material System depends on Render Graph.
**All agents say**: Inverted. Materials produce shaders + parameter blocks. Render Graph consumes them.
**Action**: Fix dependency in docs/engine/12-rendering/05-material-system.md.

### Decision 2: Replace switch-on-RenderProxyType with sort-key rendering
**Unity agent**: "64-bit sort key encoding (material | mesh | distance). Linear iteration, no branching."
**Action**: Update docs/engine/12-rendering/01-render-world.md.

### Decision 3: Property System split — Runtime vs Editor metadata
**Unity agent**: "Never put [HideInInspector] on runtime fields."
**Action**: Split PropertyInfo into RuntimePropertyInfo + EditorPropertyMeta.

### Decision 4: Debug data race detection for Job System
**Unity agent**: "ReadAccess<T>/WriteAccess<T> wrappers catch 90% of threading bugs at schedule time."
**Action**: Include in job system design.

### Decision 5: Render Graph — no compatibility mode
**Unity agent**: "Never build a compat path. Graph-only from 3.1 onward."
**Action**: After render graph milestone, ALL passes must go through graph.

### Decision 6: Start MCP earlier (Phase 5 not Phase 8)
**Godot agent**: "MCP scene tools should exist from the moment scenes exist."
**Compromise**: MCP basic tools (create/modify/delete entity, screenshot) as part of Phase 5 editor, not a separate late phase.

---

## 6. Dependency Diagram (Simplified)

```
Phase 1 (DONE): Bootstrap → Math → RHI

Phase 2 (NEW):  Lifecycle → Job System → Resource Loaders
                     │
Phase 3:         ┌───┴──────────────────┐
              Track A (Render)    Track B (ECS)
              Triangle            World + Entity
              Render Graph        Components
              Forward PBR         Property System
              Deferred            Behaviors + Tick
              Shadows             Scene Loader
              Post-Process        ──────┐
                     │                  │
                     └────── MERGE ─────┘
                             │
Phase 4:              Input → Camera → Physics → Animation → Audio
                             │
Phase 5:              Editor (incremental, parallel with Phase 4)
                             │
Phase 6:              Nav → BT → Scripting → UI → Save
                             │
Phase 7:              Asset Cooking → Advanced Rendering → Streaming → VFX
                             │
Phase 8-9:            MCP → Platform Services → Networking → Shipping
```

---

## 7. Time Estimate (Rough)

| Phase | Duration | Running Total |
|-------|----------|---------------|
| Phase 2 (Framework) | ~3 weeks | 3 weeks |
| Phase 3 (Render + ECS) | ~8 weeks (parallel) | 11 weeks |
| Phase 4 (Playable) | ~7 weeks | 18 weeks |
| Phase 5 (Editor MVE) | ~6 weeks (overlaps Phase 4) | ~20 weeks |
| Phase 6 (Advanced Gameplay) | ~8 weeks | ~28 weeks |
| Phase 7 (Content & Polish) | ~10 weeks | ~38 weeks |
| Phase 8-9 (MCP + Shipping) | ~12 weeks | ~50 weeks |

**First loaded scene rendering**: ~11 weeks
**First playable (third-person demo)**: ~18 weeks
**Minimum viable editor**: ~20 weeks
**Full AAA feature set**: ~50 weeks (~1 year)

---

## 8. What Changed vs Current Roadmap

| Change | Old | New | Why |
|--------|-----|-----|-----|
| Application Lifecycle | Phase 2.0 | Phase 2.0 (next) | Eliminates boilerplate, orders subsystems |
| Job System | Phase 1.5 | Phase 2.1 (early) | Parallel culling, shader compile, asset load |
| ECS/Scene | After full renderer | Parallel with renderer | Scene populates what renderer draws |
| Property System | Tier 3 | Phase 3.2b (early) | Gates scene loading, inspector, everything |
| Render Graph | Various | Between triangle and deferred | No manual barriers in renderer code |
| Forward PBR step | Missing | Phase 3.2 | Validates full pipeline before deferred |
| Asset importers | Phase 1.5.1 (late) | Phase 2.2 (early) | Renderer needs meshes and textures |
| Editor | Phase 2 (after all runtime) | Phase 5 (incremental, during gameplay) | Usable 12+ milestones earlier |
| DLSS/VSM/fog | Phase 1.3.3 (early) | Phase 7.5 (polish) | Not foundation, not needed for editor/AI |
| MCP | Phase 3 (last) | Phase 5 (with editor) | Engine's differentiator available sooner |
| Post-processing | 10 effects at once | Tonemap+Bloom+FXAA now, rest incremental | No need to batch |
| Material→RenderGraph dep | Material depends on RG | RG depends on materials | Fix inverted dependency |

---

## APPROVAL REQUIRED

Review this report. When approved, I will:

1. Rewrite `docs/roadmap.md` with the new phase structure
2. Fix dependency errors in docs/engine/ (material system, render world)
3. Update `docs/README.md` to reference new roadmap
4. Commit with full changelog

**Approve, request changes, or ask questions.**
