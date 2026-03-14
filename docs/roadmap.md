# UzEngine 0.2 — Roadmap

> **Repository**: https://github.com/uzmigames/uzengine

> Complete development roadmap from zero to AAA-ready engine.
> Each milestone has clear deliverables and acceptance criteria.
> Detailed design docs: [engine/README.md](engine/README.md) (183 files, 32 sections).
> Analysis report: [roadmap-analysis-report.md](research/roadmap-analysis-report.md).

---

## Overview

```
Phase 1: Foundation (DONE) ─────────────────────────────────────────►
  1.0 Bootstrap ► 1.1 Math ► 1.2 RHI ► 1.3 Shader Pipeline

Phase 2: Runtime Framework ─────────────────────────────────────────►
  2.0 App Lifecycle ► 2.1 Job System ► 2.2 Resource Loaders

Phase 3: First Pixels (parallel tracks) ────────────────────────────►
  Track A: 3.0 Triangle ► 3.1 Render Graph ► 3.2 Forward PBR ► 3.3 Deferred ► 3.4 Shadows ► 3.5 Post-Process
  Track B: 3.0b World ► 3.1b Components ► 3.2b Property System ► 3.3b Behaviors ► 3.4b Scene ► 3.5b Render Bridge
  ──────── MERGE: Loaded scene with deferred PBR ────────

Phase 4: Playable Scene ───────────────────────────────────────────►
  4.0 Input ► 4.1 Camera ► 4.2 Physics ► 4.3 Animation ► 4.4 Audio ► 4.5 Prefabs

Phase 5: Editor (incremental, overlaps Phase 4) ───────────────────►
  5.0 Extension + IPC ► 5.1 Viewport ► 5.2 Hierarchy + Inspector ► 5.3 Asset Browser
  5.4 Gizmos ► 5.5 Node-Graph Framework ► 5.6 Material Editor ► 5.7+ Advanced Editors

Phase 6: Advanced Gameplay ─────────────────────────────────────────►
  6.0 Navigation ► 6.1 Behavior Trees ► 6.2 C++ Hot-Reload ► 6.3 Lua ► 6.4 UI ► 6.5 Save

Phase 7: Content & Polish ─────────────────────────────────────────►
  7.0 Asset Cooking ► 7.1 Advanced Post-Process ► 7.2 Streaming ► 7.3 Terrain ► 7.4 VFX ► 7.5 Advanced Rendering

Phase 8: MCP & AI ─────────────────────────────────────────────────►
  8.0 MCP Server ► 8.1 AI Tools ► 8.2 AI Content Pipeline

Phase 9: Shipping ─────────────────────────────────────────────────►
  9.0 Platform SDKs ► 9.1 Networking ► 9.2 Optimization ► 9.3 Release
```

---

## Phase 1: Foundation ✅ COMPLETED

### 1.0 — Bootstrap & Project Setup ✅

**Goal**: Compilable project with build system, logging, and a window.

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Build system | Zig build (primary) + CMake fallback. MSVC/Clang support. | `zig build` and `cmake --build` produce executable |
| Logging | `UZ_LOG(category, level, fmt, ...)` with console + file backends | Logs with category/level/timestamp |
| Assertions | `UZ_CHECK()`, `UZ_ENSURE()`, `UZ_VERIFY()`, `UZ_CHECKF()` | Fatal = crash + stack trace, ensure = log + continue |
| CPU profiler | `UZ_PROFILE_SCOPE(name)` RAII timer | Profile data queryable |
| Memory | `Own<T>`, `Ref<T>`, `RefMut<T>` ownership types | Double-borrow asserts in debug |
| Platform window | Win32 window + message loop | Window opens, resizable, closeable |
| Config | `.uzproject` JSON, cascading resolution | CLI > project > engine defaults |
| Test harness | doctest integration | `zig build test` passes |

---

### 1.1 — Math & Containers ✅

**Goal**: Complete math library and core data structures.

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Math | Vec2/3/4, Mat4, Quat, Transform over DirectXMath SIMD | 70 tests pass |
| Color | LinearColor, sRGB, conversions | Round-trip correct |
| AABB / Frustum | Bounding box, frustum planes, intersections | Culling test passes |
| Containers | RingBuffer, FreeList, HandleMap (generational) | 28 tests pass |
| Allocators | Linear, pool, frame allocator | Leak detection in debug |
| Random | PCG-64 PRNG | Distribution tests pass |

---

### 1.2 — RHI (DX12 SM6.6) ✅

**Goal**: Abstract GPU interface with DX12 backend. **6,069 FPS** clear-screen on RTX 4090.

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| RHI interface | IRHIDevice, IRHICommandList, IRHIBuffer, IRHITexture, IRHIPipeline | All pure virtual |
| DX12 backend | Device, command lists, fences, D3D12MA, DRED, breadcrumbs | 33 tests pass |
| Descriptors | Bindless heap (1M CBV_SRV_UAV, SM6.6 ResourceDescriptorHeap) | Allocate/free, bindless access works |
| Swapchain | Triple-buffered FLIP_DISCARD, tearing support | No tearing |
| GPU profiler | Triple-buffered timestamp queries | Timings readable |

---

### 1.3 — Shader Pipeline ✅

**Goal**: Full shader compilation, caching, and hot-reload pipeline.

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| DXC compiler | HLSL → DXIL (SM6.0+) | Shaders compile |
| FXC fallback | HLSL → DXBC (SM5.0) | Dual-compiler support |
| VFS | Loose files (dev) + PAK archive (ship) | Both paths work |
| Cache | Content-hash (xxHash64), source + includes + defines | Unchanged shaders skip recompile |
| Reflection | SPIRV-Reflect for auto root signatures | No manual binding needed |
| Hot-reload | FileWatcher + dependency graph + lazy recompile | Edit shader → viewport updates |
| Permutations | `#ifdef UZ_BINDLESS` for SM6.6/SM5.0 paths | Auto variant selection |
| Asset foundation | Importer registry, cache, sidecar (.uzimport), manifest | 42 asset tests pass |

**Status**: 48 shader tests + 42 asset tests passing. 5 working demos.

---

## Phase 2: Runtime Framework

> **Why this phase exists**: Every demo currently repeats 200+ lines of init/loop/shutdown boilerplate.
> Application lifecycle, job system, and resource loading are the load-bearing walls every downstream system depends on.
> Reference: [UE5 analysis](.claude/memory/analysis_unreal_architect.md) — "FEngineLoop is the FIRST thing that runs."

### 2.0 — Application Lifecycle

**Goal**: Centralized init/tick/shutdown framework. No more boilerplate in demos.
**Docs**: [09-application-lifecycle/](engine/09-application-lifecycle/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| EngineLoop | 3-phase init (PreInit → Init → Start), main loop, shutdown | All demos refactored to use EngineLoop |
| Subsystem registry | `RegisterSubsystem<T>()` with dependency ordering | Subsystems init in correct order |
| Tick architecture | Fixed timestep + variable rendering, delta time | Deterministic physics tick at any display rate |
| Module system | `UZ_REGISTER_MODULE()` for static registration | Modules self-register at startup |
| CLI args | `--headless`, `--screenshot`, `--config` parsed by EngineLoop | Args override config |
| Graceful shutdown | Reverse-order subsystem teardown, GPU fence wait | Zero leaks, zero crashes on exit |

**Dependencies**: Phase 1 (all done)

---

### 2.1 — Job System

**Goal**: Work-stealing thread pool for parallel shader compile, asset loading, culling.
**Docs**: [07-platform/04-job-system.md](engine/07-platform/04-job-system.md)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Thread pool | Work-stealing queues, N-1 worker threads | Saturates all cores |
| ParallelFor | `ParallelFor(count, batch_size, lambda)` | Processes array in parallel |
| Counter / Fence | Atomic counter for job completion, CPU wait | `counter.Wait()` blocks until done |
| Debug safety | `ReadAccess<T>` / `WriteAccess<T>` wrappers in debug | Data race detected at schedule time |
| Integration | Shader compile uses job system | Multi-threaded shader compile works |

**Dependencies**: 2.0 (lifecycle for subsystem registration)

---

### 2.2 — Resource System (VFS + Loaders)

**Goal**: Load meshes and textures by path. Minimal importers for rendering.
**Docs**: [08-resource-system/](engine/08-resource-system/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| MeshImporter v1 | fastgltf → .uzmesh (vertex cache optimization, no LOD) | .glb imports to .uzmesh |
| TextureImporter v1 | stb_image → .uztexture (RGBA8, box-filter mips, no BC7) | .png/.jpg imports |
| .uzmesh runtime loader | Read header, upload VB+IB to GPU via RHI | Mesh renders from .uzmesh file |
| .uztexture runtime loader | Read header, upload RGBA8 to D3D12 texture, bindless SRV | Texture samples correctly |
| uzcook registration | Register MeshImporter + TextureImporter in uzcook | `uzcook assets/` processes meshes and textures |
| Format stabilization | Lock .uzmesh and .uztexture binary format (version field) | Format documented, magic number agreed |

**Dependencies**: 2.1 (async import via job system)

---

## Phase 3: First Pixels

> **Two parallel tracks**: Rendering (Track A) and ECS/Scene (Track B) develop simultaneously.
> Track B has NO rendering dependency — World, Entity, Components are pure simulation.
> Tracks merge at 3.5b when PrimitiveComponent bridges ECS to the renderer.

### Track A — Rendering

#### 3.0 — Triangle to Screen

**Goal**: First real geometry rendered via the engine pipeline.
**Docs**: [12-rendering/](engine/12-rendering/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Vertex/Index buffers | Upload mesh data via upload context | Colored triangle renders |
| Constant buffers | PerFrame (ViewProj, time), PerObject (Model, material) | Object transforms in 3D |
| Texture sampling | Load .uztexture → bindless SRV → sample in PS | Textured quad renders |
| Camera | Perspective/ortho, view matrix, WASD + mouse look | Free-fly camera works |
| Mesh rendering | Load .uzmesh → draw with material | Textured 3D mesh on screen |

**Dependencies**: 2.2 (resource loaders)

---

#### 3.1 — Render Graph

**Goal**: DAG-based render pass management with automatic barriers. No manual barriers after this point.
**Docs**: [12-rendering/04-render-graph.md](engine/12-rendering/04-render-graph.md)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| RgBuilder | Declare passes, textures, buffers in a frame DAG | DAG compiles without cycles |
| RgPass | Read/Write resource declarations, execution callback | Passes execute in topo order |
| Auto barriers | Resource state tracking, barrier insertion at pass boundaries | Zero D3D12 validation errors |
| Transient aliasing | Lifetime analysis, memory reuse for same-frame resources | VRAM usage reduced vs no aliasing |
| Pass culling | Unused outputs → pass removed from execution | Disabled bloom → bloom pass culled |
| Graph-only rule | ALL rendering after this milestone goes through graph | No manual `ResourceBarrier` calls in renderer |

**Dependencies**: 3.0 (existing passes to wrap)

---

#### 3.2 — Forward PBR

**Goal**: Lit geometry with a single light. Validates full material → mesh → draw → light pipeline before deferred complexity.

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| SimplePBRMaterial | Albedo + normal + metallic/roughness textures + scalar params | Material binds to PSO |
| Cook-Torrance BRDF | GGX NDF, Smith G, Schlick Fresnel in PS | Metallic sphere looks physically correct |
| Directional light | Single light, forward pass | Scene lit with shadows (placeholder) |
| Sort-key rendering | 64-bit key (material, mesh, distance), linear draw | No switch-on-type in draw loop |

**Dependencies**: 3.1 (render graph manages the forward pass)

---

#### 3.3 — Deferred Pipeline

**Goal**: Multi-light deferred shading via G-Buffer.
**Docs**: [12-rendering/06-lighting.md](engine/12-rendering/06-lighting.md)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| G-Buffer | Albedo (RGBA8), Normal (R10G10B10A2), MetalRough (RGBA8), Depth (D32) | G-Buffer textures visualizable individually |
| Geometry pass | Render all opaque → G-Buffer via render graph | Multiple objects write correct data |
| Lighting pass | Fullscreen quad, reconstruct position from depth | Directional + point lights illuminate scene |
| PBR shading | Cook-Torrance in lighting pass | Metallic/roughness produces correct visuals |
| Forward transparent | Alpha-blended objects after deferred resolve | Transparent objects render correctly |

**Dependencies**: 3.2 (material system, BRDF shaders)

---

#### 3.4 — Shadow Maps

**Goal**: Directional light cascaded shadow maps.

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| CSM (4 cascades) | Cascade selection, depth-only pass, shadow atlas | Shadows render at all distances |
| PCF filtering | 3x3 or 5x5 percentage-closer filtering | Soft shadow edges |
| Cascade blending | Smooth transition between cascades | No visible cascade seams |
| Shadow render graph pass | Depth-only pass registered in render graph | Auto barriers for shadow atlas |

**Dependencies**: 3.3 (deferred lighting pass consumes shadow maps)

---

#### 3.5 — Basic Post-Processing

**Goal**: Minimum post-processing for visual quality. Additional effects deferred to Phase 7.

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Tonemap | ACES filmic, HDR → LDR | HDR scene maps correctly |
| Bloom | Downsample mip chain + Gaussian blur + composite | Bright areas glow, no artifacts |
| FXAA | Fast approximate AA (non-temporal) | Reduced aliasing vs raw output |
| Post-process stack | Ordered pipeline via render graph | Effects chain correct order |

**Dependencies**: 3.3 (deferred output as input), 3.1 (render graph)

---

### Track B — ECS / Scene (parallel with Track A)

#### 3.0b — World + Entity

**Goal**: Entity creation, generational handles, sparse-set storage. No rendering dependency.
**Docs**: [10-entity-component/](engine/10-entity-component/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| EnTT integration | Entity CRUD, component add/remove/query | 100K entities create/destroy <1ms |
| Generational handles | Use-after-destroy detected | Stale handle access asserts |
| World | Simulation container, entity ownership, subsystem host | World creates/destroys cleanly |

**Dependencies**: 2.0 (lifecycle), 1.1 (containers)

---

#### 3.1b — Component Model

**Goal**: Component hierarchy with lifecycle callbacks.
**Docs**: [10-entity-component/02-component-model.md](engine/10-entity-component/02-component-model.md)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Component base | Register, Initialize, BeginPlay, EndPlay, Uninitialize | Lifecycle fires in correct order |
| ActorComponent | Owning entity ref, enable/disable | Components toggle on/off |
| SceneComponent | Transform (local + world), parent-child hierarchy | Child follows parent |
| Transform dirty flags | Only recalculate world transform when dirty | Static entities skip update |
| WorldSubsystem | Base class, `InitializeDependency<T>`, auto-ordering | Subsystems init in dependency order |
| Events | MulticastDelegate, deferred EventQueue | Events fire, deferred flush works |

**Dependencies**: 3.0b (World + Entity)

---

#### 3.2b — Property System (MVP)

**Goal**: Reflection for serialization, inspector, hot-reload. **Hard gate** for scene loading.
**Docs**: [21-property-system/](engine/21-property-system/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| ClassRegistry | Type registration + lookup by name | `ClassRegistry::Find("StaticMeshComponent")` works |
| UZ_PROPERTY macro | Field declaration with metadata (name, type, offset) | Properties enumerable at runtime |
| PropertyInfo | Offset, type, size, default value | Correct for all field types |
| UZ_COMPONENT macro | Auto-registers component in ClassRegistry | Components self-register |
| JSON serializer | `Serialize(component) → JSON`, `Deserialize(JSON) → component` | Round-trip preserves all fields |
| Runtime/Editor split | RuntimePropertyInfo (serialization) vs EditorPropertyMeta (tooltips, ranges) | Runtime has zero editor deps |

**Dependencies**: 3.1b (Component model to reflect on)

---

#### 3.3b — Behaviors + Tick

**Goal**: Game logic framework. Behaviors tick each frame.
**Docs**: [10-entity-component/](engine/10-entity-component/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Behavior base | `OnTick(float dt)` virtual, attachable to entities | Behavior ticks each frame |
| UZ_REGISTER_BEHAVIOR | Auto-registers behavior factory in ClassRegistry | Behaviors creatable by name |
| TickGroup | PrePhysics, Physics, PostPhysics, Update, PostUpdate | Groups execute in order |
| Frame tick loop | EngineLoop calls tick groups each frame | All behaviors receive OnTick |

**Dependencies**: 3.2b (ClassRegistry for behavior resolution), 2.0 (EngineLoop)

---

#### 3.4b — Scene Loader

**Goal**: Load .uzscene files into World. Headless-testable (no renderer needed).
**Docs**: [11-scene/](engine/11-scene/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| .uzscene format | JSON: entities array, each with name + parent + components | Schema validates |
| SceneLoader | Parse JSON → create entities → deserialize components via PropertySystem | Scene loads without errors |
| Hierarchy setup | Parent-child relationships from scene file | Transform hierarchy correct |
| Headless test | Load scene, tick 100 frames, verify behavior state | Passes without renderer |
| Scene save | World → .uzscene JSON | Save → reload → identical state |

**Dependencies**: 3.3b (behaviors for game logic), 3.2b (property system for deserialization)

---

#### 3.5b — Rendering Bridge (MERGE POINT)

**Goal**: Connect ECS components to the renderer. PrimitiveComponent → RenderProxy → RenderWorld.
**Docs**: [12-rendering/01-render-world.md](engine/12-rendering/01-render-world.md)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| PrimitiveComponent | Base for all renderable components | Components register in RenderWorld |
| StaticMeshComponent | References .uzmesh + material | Mesh renders with assigned material |
| LightComponent | Point, directional, spot. Color, intensity, range. | Lights affect deferred lighting |
| CameraComponent | FOV, near/far, active camera selection | Camera component drives render view |
| RenderProxy | ECS → render-thread data sync (command queue) | Game thread never touches GPU state |
| RenderSubsystem | WorldSubsystem: collect visible → generate draw calls | Scene entities render via deferred pipeline |

**Dependencies**: Track A (3.3+ deferred working) + Track B (3.4b scene loading)

**DEMO GATE**: Load a .uzscene → entities render with deferred PBR + shadows + post-processing.

---

## Phase 4: Playable Scene

> **Goal**: Third-person character demo with input, camera, physics, animation, and audio.

### 4.0 — Input System

**Goal**: Enhanced Input with action mapping, modifiers, and runtime rebinding.
**Docs**: [13-input-system/](engine/13-input-system/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| InputAction | Bool, Axis1D, Axis2D action types | Actions fire on input |
| InputMappingContext | Map hardware input → actions with modifiers/triggers | Context stacking works |
| Modifiers | Negate, dead zone, scale, swizzle | Gamepad stick applies dead zone |
| Triggers | Pressed, released, held (duration), tap, chord | All trigger types work |
| InputComponent | Attach context stack to entity | Per-entity input contexts |
| Runtime rebinding | Rebind key at runtime, persist to .uzinput | Rebinding survives restart |
| Gamepad | XInput + raw HID, vibration | Gamepad sticks + buttons detected |

**Dependencies**: 3.5b (ECS, WorldSubsystem)

---

### 4.1 — Camera System

**Goal**: Third-person camera with SpringArm, collision avoidance, and modes.
**Docs**: [14-camera/](engine/14-camera/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| CameraManager | Active camera selection, blend between cameras | Smooth camera transitions |
| SpringArm | Arm length, pitch/yaw, socket offset | Camera follows target at distance |
| Collision avoidance | Sphere cast along arm, pull camera forward on hit | Camera doesn't clip through walls |
| Camera modes | Free-fly, orbit, third-person, fixed | All modes work |
| Camera shake | Perlin noise shake, amplitude/frequency/decay | Screen shakes on impact |

**Dependencies**: 4.0 (input for camera control)

---

### 4.2 — Physics (Jolt)

**Goal**: Rigid body physics, collision detection, queries, character controller.
**Docs**: [16-physics/](engine/16-physics/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Jolt integration | PhysicsSubsystem, job system bridge | Physics world steps, objects fall |
| RigidBody component | Dynamic, static, kinematic | Box drops onto floor |
| Colliders | Box, sphere, capsule, mesh, convex hull | All shapes collide correctly |
| Collision layers | 16 layers, configurable interaction matrix | Layers filter as configured |
| Raycasts | `PhysicsWorld::Raycast(origin, dir, maxDist)` | Returns correct hit |
| Shape casts | Sphere/box/capsule sweep | Sweep detects obstacles |
| Triggers | Enter/exit/stay events | Trigger volume fires events |
| Character controller | Jolt `CharacterVirtual`, ground detection, step-up | Character walks on terrain, climbs steps |
| Fixed timestep | 120Hz physics with interpolation | Smooth at any display rate |

**Dependencies**: 3.5b (ECS components, SceneComponent transforms)

---

### 4.3 — Animation (Core)

**Goal**: Skeletal mesh, GPU skinning, animation state machines.
**Docs**: [19-animation/](engine/19-animation/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Skeleton | Bone hierarchy, bind pose, inverse bind matrices | .uzskeleton loads |
| SkeletalMeshComponent | Skinned mesh rendering | Mesh deforms with skeleton |
| GPU skinning | Compute shader: bone matrices × vertex weights | Correct deformation |
| Animation clips | .uzanim keyframe interpolation (slerp for rotation) | Animation plays smoothly |
| AnimationPlayer | Play, pause, speed, loop, crossfade | Blend between two anims |
| State machine | States + transitions + conditions | Walk → Run by speed param |
| Blend spaces | 1D (walk/run) and 2D (speed × direction) | Locomotion blends correctly |
| Root motion | Extract root bone translation → CharacterMovement | Character moves via animation |
| Animation events | Notify at keyframe (footstep, sound) | Event fires at correct time |
| MeshImporter v2 | Skinned mesh, bone weights from glTF | Skeletal import works |

**Dependencies**: 3.5b (rendering bridge), 4.2 (root motion → character controller)

---

### 4.4 — Audio (Core)

**Goal**: 3D spatial audio with bus mixing.
**Docs**: [17-audio/](engine/17-audio/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| miniaudio integration | Device init, playback engine | Audio plays through speakers |
| AudioSubsystem | WorldSubsystem, command queue to audio thread | No glitches under load |
| AudioSourceComponent | 3D position, attenuation curves, looping | Sound quieter with distance |
| AudioListenerComponent | Follows active camera | Spatial audio correct relative to camera |
| Bus hierarchy | Main → SFX / Music / Voice / Reverb | Per-group volume works |
| AudioImporter | WAV/OGG/MP3 → .uzaudio (PCM <2MB, streaming >2MB) | All formats play |
| Fire-and-forget | `AudioSubsystem::PlayAt(sound, position, volume)` | One-liner positional audio |

**Dependencies**: 3.5b (SceneComponent for position)

---

### 4.5 — Prefabs + Hot-Reload

**Goal**: Reusable entity templates and live asset reloading.

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| .uzprefab format | JSON: entity template with component defaults | Schema validates |
| Prefab instantiation | Spawn prefab hierarchy into World | Prefab spawns correctly |
| Override merging | Instance overrides on top of prefab defaults | Override field persists |
| Nested prefabs | Prefab references other prefabs | Recursive instantiation works |
| Asset hot-reload | FileWatcher → detect change → reimport → reload in scene | Modify texture → viewport updates |
| Import dependency graph | Texture → material → mesh → scene chain | Change texture → material reimports |

**Dependencies**: 3.4b (scene loader), 2.2 (resource system)

**DEMO GATE**: Third-person character in a lit scene with physics, animation, audio, and prefabs.

---

## Phase 5: Editor (Incremental)

> **Starts during Phase 4** — extension scaffold has zero runtime deps.
> Each panel ships as soon as its runtime dependency is met.
> Reference: [40-editor-framework/](engine/40-editor-framework/) (21 detailed docs).

### 5.0 — Extension + IPC Foundation

**Goal**: VSCode extension scaffold, N-API bridge, IPC message protocol.
**Docs**: [40-editor-framework/01-overview.md](engine/40-editor-framework/01-overview.md), [20-editor-ipc.md](engine/40-editor-framework/20-editor-ipc.md)
**When**: Parallel with Phase 4.0

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Extension scaffold | package.json, activation, commands, lifecycle | Extension activates in VSCode |
| N-API bridge | .node compilation, Electron ABI matching | .node loads in VSCode |
| IPC message format | JSON envelope: id, method, params, response | Format locked and documented |
| IPC dispatch | Function call routing, error propagation, callbacks | Call from TS → C++ → response |
| Console panel | Log streaming via IPC callbacks, level filtering | Engine logs appear in VSCode panel |
| Toolbar skeleton | Play/pause/stop buttons → runtime lifecycle IPC | Buttons control engine state |

**Dependencies**: 2.0 (lifecycle), N-API proven by POC 0.2

---

### 5.1 — Viewport Panel

**Goal**: 3D scene renders inside VSCode with camera controls.
**Docs**: [40-editor-framework/18-viewport-modes.md](engine/40-editor-framework/18-viewport-modes.md)
**When**: After 3.5b (render bridge)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| HWND viewport | Runtime creates child HWND inside VSCode panel | Scene renders in VSCode |
| Camera orbit | Orbit, pan, zoom via mouse | Camera controls feel standard |
| Input forwarding | Mouse/keyboard events from VSCode → runtime | Click in viewport selects entity |
| Viewport modes | Lit, unlit, wireframe, buffer visualization | Toggle between modes |

**Dependencies**: 3.5b (scene renders), 5.0 (IPC bridge)

---

### 5.2 — Hierarchy + Inspector

**Goal**: Browse scene tree, edit entity properties.
**Docs**: [40-editor-framework/05-hierarchy-panel.md](engine/40-editor-framework/05-hierarchy-panel.md), [04-inspector.md](engine/40-editor-framework/04-inspector.md)
**When**: After 3.5b (scene + property system)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Hierarchy panel | Tree view of entities, expand/collapse, single-select | Select entity in tree |
| Selection sync | Hierarchy ↔ Viewport selection linked | Click tree → viewport highlights |
| Inspector panel | Bool, Int, Float, String, Vec3 property widgets | Modify transform → entity moves |
| Component list | Show all components on selected entity | Components enumerable |
| Create/Delete | Right-click → Create Entity / Delete Entity | Entities add/remove from scene |

**Dependencies**: 3.2b (property system), 3.4b (scene loader)

---

### 5.3 — Asset Browser

**Goal**: Browse, preview, and drag-drop assets.
**Docs**: [40-editor-framework/06-asset-browser.md](engine/40-editor-framework/06-asset-browser.md)
**When**: After 5.2

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| File listing | Browse content/ directory tree via IPC | Files and folders display |
| Thumbnails | GPU-rendered thumbnails for meshes/textures | Thumbnails show content |
| Drag-drop | Drag asset from browser → drop in viewport | Entity created with asset |
| Search / Filter | Text search + type filter (mesh, texture, material) | Find assets quickly |

**Dependencies**: 5.2 (hierarchy for entity creation), 3.0 (renderer for thumbnails)

---

### 5.4 — Gizmos + Selection

**Goal**: Transform gizmos for visual scene editing.
**Docs**: [40-editor-framework/02-gizmos.md](engine/40-editor-framework/02-gizmos.md), [07-tool-modes.md](engine/40-editor-framework/07-tool-modes.md)
**When**: After 5.2

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Translate gizmo | 3-axis arrows, plane handles | Drag to move entity |
| Rotate gizmo | 3-axis rings | Drag to rotate entity |
| Scale gizmo | 3-axis boxes | Drag to scale entity |
| Snapping | Grid snap, rotation snap (15°, 45°, 90°) | Snapping works |
| Multi-select | Shift-click, box select | Multiple entities selected |
| Undo/Redo | ICommand stack, Ctrl+Z/Ctrl+Y | Undo reverts, redo restores |

**Dependencies**: 5.1 (viewport), 4.2 (raycasts for click-to-select)

---

### 5.5 — Node-Graph Framework

**Goal**: Shared graph editor canvas reusable by all specialized editors.
**Docs**: [40-editor-framework/09-node-graph-framework.md](engine/40-editor-framework/09-node-graph-framework.md)
**When**: After 5.4 (pure TS, no runtime deps beyond IPC)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Canvas | WebGL pan/zoom, virtual rendering for 1000+ nodes | Smooth interaction |
| Node system | Create/delete/move nodes, pin types, connections | Nodes connect via pins |
| Serialization | Graph → JSON → graph round-trip | Save/load graphs |
| Undo/Redo | Graph-level undo stack | Undo node operations |
| Mini-map | Overview of full graph with viewport indicator | Navigate large graphs |
| Pin validation | Type-checking on connections, error indicators | Invalid connections rejected |

**Dependencies**: 5.0 (IPC for compilation requests)

---

### 5.6 — Material Editor

**Goal**: Node-graph material authoring → HLSL generation.
**Docs**: [40-editor-framework/10-material-editor.md](engine/40-editor-framework/10-material-editor.md)
**When**: After 5.5 (graph framework) + 3.2 (material system)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Material nodes | Texture Sample, Math, Lerp, Fresnel, Time, etc. | Nodes produce correct HLSL |
| PBR output | Albedo, Normal, Metallic, Roughness, Emissive pins | Material renders in deferred |
| Live preview | Sphere/plane preview updates on graph change | Edit node → preview updates |
| HLSL codegen | Graph → HLSL source → DXC compile → PSO | Generated shader compiles |
| Save format | .uzmat JSON graph definition | Save → reload → identical graph |

**Dependencies**: 5.5 (graph framework), 3.2 (forward PBR material system)

---

### 5.7 — Animation Controller Editor

**Docs**: [40-editor-framework/11-animcontroller-editor.md](engine/40-editor-framework/11-animcontroller-editor.md)
**When**: After 4.3 (animation runtime)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| State machine | Visual states + transitions + conditions | Walk → Run transition editable |
| Blend space | 1D/2D blend visualization | Blend weights visualized |
| Live preview | Skeleton preview with current state | Animation plays in editor |

---

### 5.8+ — Advanced Editors (each trails its runtime system)

| Editor | Docs | Unlocked By |
|--------|------|-------------|
| Control Rig | [12-controlrig-editor.md](engine/40-editor-framework/12-controlrig-editor.md) | 4.3 Animation + IK |
| Behavior Tree | [15-behavior-tree-editor.md](engine/40-editor-framework/15-behavior-tree-editor.md) | 6.1 BT runtime |
| VFX Graph | [13-vfx-editor.md](engine/40-editor-framework/13-vfx-editor.md) | 7.4 VFX/Particles |
| PCG Graph | [16-pcg-editor.md](engine/40-editor-framework/16-pcg-editor.md) | 7.3 Terrain |
| Sequencer | [14-sequencer-editor.md](engine/40-editor-framework/14-sequencer-editor.md) | 4.3 Animation + 4.4 Audio |
| Debug Tools | [17-debug-tools.md](engine/40-editor-framework/17-debug-tools.md) | 5.1 Viewport |
| Viewport Modes | [18-viewport-modes.md](engine/40-editor-framework/18-viewport-modes.md) | 3.3 Deferred (buffer viz) |
| Preferences | [19-editor-preferences.md](engine/40-editor-framework/19-editor-preferences.md) | 5.0 Extension |
| Testing | [21-editor-testing.md](engine/40-editor-framework/21-editor-testing.md) | 5.4 Gizmos (full editor) |

---

## Phase 6: Advanced Gameplay

### 6.0 — Navigation (Recast/Detour)

**Docs**: [20-gameplay/](engine/20-gameplay/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Recast integration | NavMesh baking from scene geometry (offline via uzcook) | NavMesh generates for test level |
| Detour pathfinding | A* path query on navmesh | Agent finds path around obstacles |
| NavMeshAgent | Auto-movement along path, obstacle avoidance | Agent navigates to target |
| NavObstacle | Dynamic obstacles carve navmesh | Agent reroutes |
| Off-mesh links | Jump points, ladders | Agent traverses gaps |

**Dependencies**: 4.2 (physics for agent movement), 3.4b (scene geometry)

---

### 6.1 — Behavior Trees

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| BT runtime | Sequence, Selector, Parallel composites | Tree executes correctly |
| Decorators | Cooldown, loop, conditional | Decorators modify child behavior |
| Services | Background polling (check distance, update blackboard) | Services run at interval |
| Blackboard | Key-value store per entity | Set/Get keys works |
| EQS | "Find cover", "find flanking position" queries | Spatial query returns results |
| Perception | Sight (cone), hearing (radius) | AI detects player |

**Dependencies**: 6.0 (navigation for MoveTo), 4.0 (input for player detection)

---

### 6.2 — C++ Game DLL + Hot-Reload

**Docs**: [15-scripting/](engine/15-scripting/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Game DLL | Separate DLL with `UZ_REGISTER_MODULE()` | Custom component in DLL works |
| Hot-reload | Serialize → unload DLL → rebuild → reload → deserialize | State preserved across reload |
| Component registration | Game DLL registers components in ClassRegistry | Components visible to editor |

**Dependencies**: 3.2b (property system for serialization)

---

### 6.3 — Lua Scripting

**Docs**: [15-scripting/](engine/15-scripting/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| LuaJIT VM | Embedded, sandboxed (no os/io/debug) | Script executes in engine |
| Bindings | Vec3, Entity, World, Input, Physics, Audio metatables | `ctx.world:spawn()` works |
| Script component | Attach .lua to entity, on_start/on_update/on_stop | Script ticks each frame |
| Hot-reload | FileWatcher → reload script, preserve state | Edit .lua → behavior changes |
| Error handling | Lua errors → UZ_LOG, no crash | Bad script logs error |

**Dependencies**: 4.0-4.4 (all C++ APIs must be stable to bind)

---

### 6.4 — UI Runtime

**Docs**: [18-ui-system/](engine/18-ui-system/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| HTML/CSS renderer | Lightweight browser layer for game UI | HTML page renders as overlay |
| Data binding | `ui:set_property("--health", hp)` CSS custom properties | UI updates reactively |
| Input routing | Modal vs passthrough, context stack | UI captures input when focused |
| Lua API | `ctx.ui:load("hud.html")`, `ctx.ui:set_property()` | Lua drives UI |
| imgui debug | Engine debug overlay (FPS, entity inspector, profiler) | Debug UI toggleable |

**Dependencies**: 4.0 (input routing), 3.5 (renderer for overlay compositing)

---

### 6.5 — Save System

**Docs**: [20-gameplay/](engine/20-gameplay/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Save format | msgpack binary, slot-based | Save file created |
| Serialize world | ECS snapshot via property system | All entity state captured |
| Load world | Deserialize → restore World state | Load restores exact state |
| Autosave | Timer-based, configurable interval | Autosave fires |
| Save slots | Create/delete/overwrite slots, metadata (timestamp, thumbnail) | Slot management works |

**Dependencies**: 3.2b (property system), 4.2+ (gameplay state to save)

---

## Phase 7: Content & Polish

### 7.0 — Asset Cooking (Full)

**Docs**: [08-resource-system/](engine/08-resource-system/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| TextureImporter v2 | BC7 (albedo), BC5 (normals), BC6H (HDR) via ispc_texcomp | Compressed textures load |
| LOD generation | meshoptimizer simplify → .uzmesh LOD chain | LOD switches at distance |
| Tangent generation | mikktspace for normal mapping | Normal maps work correctly |
| .uzpak packager | Binary archive, 64-byte aligned, zstd optional | Assets load from .uzpak |
| .uzmanifest | JSON manifest for delta patching | Incremental patch works |
| Parallel cook | Job system integration for multi-threaded import | Cook saturates cores |

**Dependencies**: 2.1 (job system), 2.2 (importer framework)

---

### 7.1 — Advanced Post-Processing

**Goal**: Ship each effect as an independent micro-milestone.

| Effect | Dependencies | Acceptance Criteria |
|--------|-------------|-------------------|
| SSAO (GTAO) | 3.5 (render graph) | Occlusion in corners |
| TAA | 3.5 (render graph, velocity buffer) | Stable image, reduced aliasing |
| SSR | 3.3 (deferred, depth + normal) | Reflections on glossy surfaces |
| DOF | 3.5 (render graph) | Bokeh blur based on focus |
| Motion blur | 3.5 (velocity buffer) | Moving objects blur |
| Color grading | 3.5 (LUT texture) | LUT applies correctly |

---

### 7.2 — World Streaming

**Docs**: [11-scene/](engine/11-scene/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| World partition | Grid-based cell system | Cells defined in .uzscene |
| Async streaming | Background cell load/unload via job system | No main-thread stalls |
| Streaming volumes | Manual load/unload triggers | Volume triggers load |
| Cross-cell physics | Contacts at cell boundaries | Objects don't fall through seams |

**Dependencies**: 3.4b (scene), 4.2 (physics), 2.1 (job system for async I/O)

---

### 7.3 — Terrain + Foliage

**Docs**: [11-scene/](engine/11-scene/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Heightmap terrain | Load heightmap, clipmap LOD | Terrain renders with LOD |
| Terrain materials | Multi-layer painting, blend weights | 4+ layers blend |
| Virtual texturing | Tile streaming, GPU feedback | No pop-in |
| Foliage instancing | GPU instanced grass/trees, culling | 1M+ instances at 60fps |
| Terrain collider | Heightfield physics shape | Character walks on terrain |

**Dependencies**: 3.3 (deferred renderer), 4.2 (physics)

---

### 7.4 — VFX / Particles

**Docs**: [25-vfx/](engine/25-vfx/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| GPU particle system | Compute shader spawn/update/render | 1M particles at 60fps |
| Emitter modules | Spawn, velocity, lifetime, size/color curves | Fire/smoke VFX |
| Force modules | Gravity, wind, turbulence, vortex | Particles react to forces |
| Depth collision | Particles bounce off depth buffer | Sparks bounce off surfaces |
| Ribbons | Trail renderer | Sword slash trail |
| .uzvfx loader | JSON system definition | VFX loads and plays |

**Dependencies**: 3.3 (depth buffer for collision), 3.1 (render graph for compute)

---

### 7.5 — Advanced Rendering

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| DLSS integration | NVIDIA DLSS SDK | Upscaled quality ≈ native |
| FSR integration | AMD FSR, any GPU | FSR modes work |
| XeSS integration | Intel XeSS SDK | Auto-fallback chain (DLSS→XeSS→FSR) |
| Virtual shadow maps | Clipmap pages | Stable shadows at distance |
| Volumetric fog | Ray-marched | God rays visible |
| Sky / Atmosphere | Hillaire model | Physically correct scattering |
| Reflection probes | Baked cubemaps, parallax correction | Reflections on non-planar surfaces |

**Dependencies**: 3.5 (post-processing stack), 3.4 (shadow system)

---

## Phase 8: MCP & AI

### 8.0 — MCP Server + Scene Tools

**Docs**: [28-ipc-protocol/](engine/28-ipc-protocol/), [26-ai-content-pipeline/](engine/26-ai-content-pipeline/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| MCP server | Node.js, stdio/SSE transport | Claude connects |
| Scene tools | create_entity, modify_component, delete_entity | LLM creates entities |
| Query tools | get_scene_tree, get_entity_components, get_logs | LLM reads state |
| Screenshot tool | capture_screenshot → return image | LLM sees rendered scene |
| Asset tools | import_asset, modify_material | LLM generates assets |

**Dependencies**: 5.0 (IPC), 3.5b (scene system)

---

### 8.1 — AI Tool Integration

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| AI provider abstraction | `ai::Generate(category, prompt)` routing | Provider swap via config |
| Texture generation | FLUX / Nano Banana | LLM generates texture |
| 3D model generation | Meshy / Hunyuan / Tripo | LLM generates mesh |
| Audio generation | ElevenLabs (voice + SFX), Stable Audio (music) | LLM generates audio |

---

### 8.2 — AI Content Pipeline

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| End-to-end VFX | Prompt → texture + mesh + material + VFX | "Fire explosion" → complete asset |
| End-to-end character | Prompt → model + skeleton + animations + sounds | Playable character from prompt |
| End-to-end level | Prompt → terrain + props + lighting + audio | Walkable level from prompt |
| Visual iteration | Screenshot → LLM feedback → auto-adjust | LLM iterates until satisfied |

**Dependencies**: Phase 5 (editor), Phase 7 (VFX, terrain)

---

## Phase 9: Shipping

### 9.0 — Platform Services

**Docs**: [27-platform-services/](engine/27-platform-services/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Platform abstraction | `IPlatformServices` unified interface | One API for all platforms |
| Steam | Steamworks: login, achievements, leaderboards, workshop | Steam overlay works |
| Epic | EOS: login, achievements, matchmaking | EOS login works |

---

### 9.1 — Networking

**Docs**: [24-networking/](engine/24-networking/)

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Replication | Property sync, delta compression, relevancy | Client sees server state |
| RPCs | Client→Server, Server→Client, Multicast | RPC executes correctly |
| Dedicated server | Headless build, `--server` flag | Server runs without GPU |
| Prediction | Client-side prediction, server reconciliation | Smooth despite latency |
| Replay | Record/playback network state | Replays accurately |

---

### 9.2 — Optimization

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| GPU-driven rendering | Indirect draw, multi-draw-indirect | Draw calls independent of object count |
| Nanite-style meshes | Virtual geometry, cluster LOD | Dense scenes efficient |
| Software GI | Screen-space + surface cache + radiance cache | Indirect lighting without lightmaps |
| Hardware RT | DXR reflections, shadows, AO | RT on RTX/RDNA2+ |
| Memory budget | Streaming, pool tuning, VRAM tracking | Within target budget |

---

### 9.3 — Release

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| CI/CD | GitHub Actions: MSVC/Clang/Zig build matrix | Every PR tested |
| Screenshot regression | Golden image comparison | Visual regressions caught |
| Benchmark suite | Standard scenes, FPS/memory tracking | Performance tracked |
| Documentation | API docs, tutorials, examples | New user can create project |
| VS Marketplace | Publish extension | Extension installable |
| Example projects | FPS + third-person templates | Templates build and run |

---

## Timeline

```
Phase 1  Foundation                 ████████████████████  DONE
Phase 2  Runtime Framework          ░░░███░░░░░░░░░░░░░░  ~3 weeks
Phase 3  First Pixels               ░░░░░████████░░░░░░░  ~8 weeks (parallel tracks)
Phase 4  Playable Scene             ░░░░░░░░░░░██████░░░  ~7 weeks
Phase 5  Editor (incremental)       ░░░░░░░░░░░░█████░░░  ~6 weeks (overlaps Phase 4)
Phase 6  Advanced Gameplay          ░░░░░░░░░░░░░░░████░  ~8 weeks
Phase 7  Content & Polish           ░░░░░░░░░░░░░░░░████  ~10 weeks
Phase 8  MCP & AI                   ░░░░░░░░░░░░░░░░░███  ~6 weeks
Phase 9  Shipping                   ░░░░░░░░░░░░░░░░░░██  ~8 weeks
```

**Key milestones**:
- **First scene rendering**: ~11 weeks (end of Phase 3)
- **First playable demo**: ~18 weeks (end of Phase 4)
- **Minimum viable editor**: ~20 weeks (Phase 5 MVP)
- **Full AAA feature set**: ~50 weeks (~1 year)

---

## Success Metrics

| Metric | Target |
|--------|--------|
| **Frame rate** | 60fps @ 1080p on RTX 3060 / RX 6700 |
| **Draw calls** | GPU-driven: <100 CPU draw calls |
| **Entity count** | 100K+ entities with physics at 60fps |
| **Particle count** | 1M+ GPU particles at 60fps |
| **Load time** | Scene cold load <3s, hot reload <500ms |
| **Memory** | <2GB GPU, <4GB CPU for typical scene |
| **Build time** | Incremental <5s (Zig), <15s (MSVC) |
| **Test coverage** | 95%+ on all engine systems |
| **AI generation** | End-to-end VFX from prompt <60s |

---

## Documentation Reference

Each phase has detailed design docs in [engine/](engine/README.md):

| Phase | Docs |
|-------|------|
| Phase 1 | [01-core/](engine/01-core/), [02-math/](engine/02-math/), [03-rhi/](engine/03-rhi/), [05-shader-pipeline/](engine/05-shader-pipeline/) |
| Phase 2 | [09-application-lifecycle/](engine/09-application-lifecycle/), [07-platform/](engine/07-platform/), [08-resource-system/](engine/08-resource-system/) |
| Phase 3 | [12-rendering/](engine/12-rendering/), [10-entity-component/](engine/10-entity-component/), [21-property-system/](engine/21-property-system/), [11-scene/](engine/11-scene/) |
| Phase 4 | [13-input-system/](engine/13-input-system/), [14-camera/](engine/14-camera/), [16-physics/](engine/16-physics/), [19-animation/](engine/19-animation/), [17-audio/](engine/17-audio/) |
| Phase 5 | [40-editor-framework/](engine/40-editor-framework/) (21 files), [28-ipc-protocol/](engine/28-ipc-protocol/) |
| Phase 6 | [20-gameplay/](engine/20-gameplay/), [15-scripting/](engine/15-scripting/), [18-ui-system/](engine/18-ui-system/) |
| Phase 7 | [25-vfx/](engine/25-vfx/), [08-resource-system/](engine/08-resource-system/) |
| Phase 8 | [26-ai-content-pipeline/](engine/26-ai-content-pipeline/) |
| Phase 9 | [27-platform-services/](engine/27-platform-services/), [24-networking/](engine/24-networking/) |

---

*This roadmap was restructured on 2026-03-12 based on analysis from 8 specialized architecture agents.*
*Previous version archived in git history.*
*Reference: [roadmap-analysis-report.md](research/roadmap-analysis-report.md) for full analysis details.*
