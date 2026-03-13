---
name: UE5 Architecture Analysis for Roadmap
description: UE5-based implementation order analysis for UzEngine roadmap restructuring
type: project
---

# UE5 Architecture Analysis for UzEngine Roadmap

## 1. UE5's Actual Dependency Chain

UE5's runtime initialization order (from `FEngineLoop::PreInit` through `Init` through `Tick`) reveals a strict dependency chain that UzEngine's roadmap partially mirrors but gets wrong in several places.

### UE5 Foundation Layer (must exist before anything renders)

```
HAL/Platform (GenericPlatform, threads, file I/O, memory)
  -> Core (logging, assertions, delegates, containers, math, config)
    -> TaskGraph (job system — used by EVERYTHING downstream)
      -> RHI (abstract GPU interface)
        -> RenderCore (shader compilation, shader types, global shaders)
          -> Renderer (scene proxy, render graph, materials, lighting)
            -> Engine (UWorld, actors, components, ticking)
              -> GameFramework (pawns, controllers, game modes)
```

Key insight: **UE5's TaskGraph is initialized extremely early** — before RHI, before any rendering. This is because shader compilation, asset loading, and even RHI device creation use parallel tasks. The job system is not a "Phase 1.5" add-on; it is foundational infrastructure.

### UE5 Rendering Sub-Chain (within Renderer module)

```
FShader / FShaderType (static registration, permutation framework)
  -> FMaterial / FMaterialRenderProxy (material abstraction, parameter blocks)
    -> FPrimitiveSceneProxy (render representation of game objects)
      -> FSceneRenderer / FDeferredShadingSceneRenderer
        -> FRDGBuilder (render graph — built ON TOP of the above)
          -> Individual passes (GBuffer, Lighting, PostProcess, etc.)
```

Critical: **Render Graph is NOT the first thing you build in the renderer.** UE5 added RDG years after having a working renderer. The render graph is an optimization/organization layer that wraps existing pass logic. Building it before you have passes to graph is premature abstraction.

## 2. Foundation vs Gameplay vs Editor Classification

### Foundation (blocks everything — build first, get right)

| System | UE5 Module | Why Foundation |
|--------|-----------|----------------|
| Platform/HAL | `Core/HAL` | Every system needs file I/O, threads, timing |
| Job System | `Core/TaskGraph` | Shader compile, asset load, physics, culling — all parallel |
| Memory Allocators | `Core/Memory` | Every allocation flows through these |
| Asset Registry / VFS | `CoreUObject/AssetRegistry` | Materials reference textures, meshes reference materials |
| Shader Framework | `RenderCore/Shader.h` | Materials compile to shaders; no materials without this |
| Application Lifecycle | `Launch/FEngineLoop` | Init/tick/shutdown ordering for all subsystems |

### Rendering (builds on foundation, blocks visual content)

| System | UE5 Module | Why Here |
|--------|-----------|----------|
| Material System | `Engine/Materials` | Every visible object needs a material |
| Scene Proxies | `Engine/PrimitiveSceneProxy` | Bridge between game world and renderer |
| Visibility/Culling | `Renderer/SceneVisibility` | Determines what to draw |
| Draw Call Generation | `Renderer/MeshPassProcessor` | Converts proxies into GPU commands |
| Deferred Shading | `Renderer/DeferredShading` | The actual shading pipeline |
| Post Processing | `Renderer/PostProcess` | Final image quality |

### Gameplay (builds on rendering, blocks playable content)

| System | UE5 Module |
|--------|-----------|
| World/Actors/Components | `Engine/World`, `Engine/Actor` |
| Physics | `PhysicsCore` + `Chaos` |
| Animation | `Engine/Animation` |
| Audio | `AudioMixer` |
| Input | `EnhancedInput` |
| AI/Navigation | `AIModule`, `NavigationSystem` |

### Editor (builds on gameplay, blocks content creation UX)

Everything in UE5's `UnrealEd` module.

## 3. Where UzEngine's Roadmap Deviates from UE5 Patterns

### DEVIATION 1: Scene System (1.4) depends on Renderer (1.3) — WRONG

**Current roadmap**: 1.3 Renderer -> 1.4 Scene System

**UE5 reality**: `UWorld`, `AActor`, `UActorComponent` exist in the `Engine` module and do NOT depend on the renderer. The renderer depends on the component system (via `UPrimitiveComponent` -> `FPrimitiveSceneProxy`). The dependency is:

```
Component System -> Render Proxy Pattern -> Renderer
NOT
Renderer -> Component System
```

**Recommendation**: Move ECS/World/Components to be parallel with or slightly before the renderer. You need entities to populate the scene that the renderer draws. Currently the roadmap builds a full deferred PBR pipeline (1.3.1) with post-processing (1.3.2) before you even have entities, transforms, or a scene graph. This means 1.3 demos will use ad-hoc hardcoded draw calls that get thrown away when 1.4 arrives.

### DEVIATION 2: Job System is buried in Phase 1.5 — TOO LATE

**Current roadmap**: Job System is in `07-platform/04-job-system.md`, planned for "Milestone 1.5"

**UE5 reality**: `FTaskGraphInterface` is initialized in `FEngineLoop::PreInit()`, before RHI, before window creation. It is used by:
- Shader compilation (parallel permutation compile)
- Asset loading (background I/O)
- Physics broadphase
- Visibility culling
- Animation bone updates

**Recommendation**: Job System should be Milestone 1.3 at latest (after Math/Containers, before or alongside the renderer). Without it, shader compilation is single-threaded, asset loading blocks the main thread, and you cannot do parallel culling.

### DEVIATION 3: Resource System / Asset Registry is not in the critical path — WRONG

**Current roadmap**: Resource System (08) is "Tier 2 — Next Phase Foundation", lumped with rendering

**UE5 reality**: `FAssetRegistry` and the asset loading pipeline (`FStreamableManager`, `FSoftObjectPath`) are initialized early in `FEngineLoop::Init()`. Every subsystem depends on being able to load assets:
- Renderer needs to load shaders, textures, meshes
- Material system needs to load material definitions
- Scene system needs to load scene files
- Audio needs to load sound files

The VFS + Asset Importer Registry + Asset Cache are **foundation** systems. Without them, every demo hardcodes file paths and does ad-hoc loading (which is exactly what UzEngine demos currently do).

**Recommendation**: VFS + basic asset loading should be Milestone 1.2.5 or early 1.3 — after RHI but before any real rendering work. The shader pipeline already partially does this (shader VFS), but mesh/texture loading needs the same infrastructure.

### DEVIATION 4: Application Lifecycle is planned for "Milestone 2.0" — FAR TOO LATE

**Current roadmap**: `09-application-lifecycle/01-overview.md` says "Status: Planned (Milestone 2.0)"

**UE5 reality**: `FEngineLoop` is the FIRST thing that runs. It is not an afterthought — it IS the engine.

**Recommendation**: Application Lifecycle (init/tick/shutdown framework) should be Milestone 1.3.0 at latest. Currently every demo repeats 200+ lines of init/loop/shutdown boilerplate (the doc itself says this). This is the #1 source of bugs and wasted effort. Build the lifecycle framework BEFORE building the deferred renderer on top of it.

### DEVIATION 5: Material System depends on Render Graph — INVERSION

**Current roadmap** (from `12-rendering/05-material-system.md`): "Depends on: Shader Pipeline (05), Render Graph (32), Resource System (08)"

**UE5 reality**: Materials do NOT depend on the render graph. Materials depend on the shader framework and resource system. The render graph depends on materials (passes need to bind materials). The dependency is:

```
Shader Framework -> Material System -> Render Passes -> Render Graph
NOT
Render Graph -> Material System
```

**Recommendation**: Build materials before render graph. Materials are: shader + parameter block + permutation selection. You can render with materials using raw command lists before you have a render graph.

### DEVIATION 6: Full deferred pipeline before basic forward rendering

**Current roadmap**: 1.3.0 "Triangle to Screen" jumps straight to 1.3.1 "Deferred Pipeline"

**UE5 history**: UE3 had a forward renderer for years. UE4 launched with deferred. But the key point is: UE5's deferred pipeline was built incrementally. First you render opaque geometry with a single material. Then you add a second render target. Then you add lighting. Etc.

**Recommendation**: Between 1.3.0 and 1.3.1, insert a "Forward PBR" milestone that renders lit geometry with a single point/directional light using a forward pass. This validates the full material -> mesh -> draw call -> lighting pipeline before adding G-Buffer complexity. The deferred pipeline is then a refactor of the forward pass into multiple render targets.

## 4. Critical Path Analysis — What Blocks the Most Downstream Systems

### The True Critical Path

```
1.0 Bootstrap (DONE)
  -> 1.1 Math/Containers (DONE)
    -> 1.2 RHI DX12 (DONE)
      -> [NEW] Application Lifecycle Framework
        -> [NEW] Job System (basic: enqueue, wait, parallel-for)
          -> [NEW] VFS + Asset Registry (basic: load file by path, content hash)
            -> [NEW] Entity/Component System (World, Entity, Component, SceneComponent)
              -> Shader Framework + Material System (basic: load shader, bind params)
                -> Forward Renderer (mesh + material + one light)
                  -> Render Proxy Pattern (component -> proxy sync)
                    -> Deferred Pipeline (G-Buffer + lighting pass)
                      -> Render Graph (wrap existing passes in DAG)
                        -> Post-Processing
                          -> Advanced Rendering
```

### What Blocks the Most Systems

| System | Blocks | Priority |
|--------|--------|----------|
| **Application Lifecycle** | Every demo, every subsystem init, all testing | CRITICAL — build next |
| **Job System** | Shader compile perf, asset loading, physics, culling | CRITICAL — build next |
| **VFS + Asset Registry** | Every asset load (mesh, texture, shader, scene, audio) | HIGH |
| **Entity/Component** | Scene system, physics, animation, scripting, editor | HIGH |
| **Material System** | Every visible object, all rendering beyond clear-screen | HIGH |
| **Render Graph** | Post-processing, async compute, resource aliasing | MEDIUM (can defer) |
| **Physics** | Gameplay, character controller, collision | MEDIUM |
| **Animation** | Animated characters | MEDIUM |

## 5. Parallelization Opportunities

### Can Be Developed in Parallel (no dependency between them)

| Track A | Track B | Track C |
|---------|---------|---------|
| Job System | Application Lifecycle | VFS + Asset Registry |
| (after: core) | (after: core + RHI) | (after: core) |

Once the above three are done:

| Track A | Track B |
|---------|---------|
| Entity/Component System | Material System + Shader Framework |
| (after: lifecycle) | (after: VFS + shader pipeline) |

Once both tracks merge:

| Track A | Track B |
|---------|---------|
| Forward Renderer + Render Proxy | Input System |
| (after: ECS + materials) | (after: ECS + lifecycle) |

### Must Be Sequential (strict dependency)

```
Application Lifecycle -> Entity/Component -> Render Proxy Pattern -> Deferred Pipeline
Material System -> Forward Renderer -> Deferred Pipeline -> Post-Processing -> Render Graph
VFS -> Asset Loading -> Mesh Pipeline -> Texture Pipeline -> Scene Serialization
```

## 6. Recommended Phase Order

### Phase 1.3: Runtime Framework (NEW — currently missing)

| # | System | Duration Est. | Justification |
|---|--------|--------------|---------------|
| 1.3.0 | Application Lifecycle | 1 week | Eliminates 200+ lines of boilerplate per demo, enforces init/shutdown ordering |
| 1.3.1 | Job System (basic) | 1 week | Unblocks parallel shader compile, async asset load |
| 1.3.2 | VFS + Asset Registry | 1 week | Unblocks loading meshes, textures, materials by path |
| 1.3.3 | Entity/Component (World + ECS + SceneComponent) | 2 weeks | Unblocks scene population, render proxy, physics |

### Phase 1.4: Rendering Pipeline (was 1.3, reordered)

| # | System | Duration Est. | Justification |
|---|--------|--------------|---------------|
| 1.4.0 | Triangle to Screen (mesh + camera + texture) | 1 week | First real geometry on screen |
| 1.4.1 | Material System (basic: shader + params + permutations) | 2 weeks | Every object needs a material |
| 1.4.2 | Render Proxy + Sync (PrimitiveComponent -> RenderWorld) | 1 week | Bridge ECS to renderer |
| 1.4.3 | Forward PBR (single light, no G-Buffer) | 1 week | Validates full pipeline before deferred |
| 1.4.4 | Deferred Pipeline (G-Buffer + lighting) | 2 weeks | Multi-light support |
| 1.4.5 | Shadow Maps (CSM) | 1 week | Required for any serious scene |
| 1.4.6 | Render Graph | 2 weeks | Wraps existing passes, enables post-processing |
| 1.4.7 | Post-Processing (tonemap, bloom, SSAO, TAA) | 2 weeks | Final image quality |

### Phase 1.5: Scene & Content (was 1.4)

| # | System | Duration Est. |
|---|--------|--------------|
| 1.5.0 | Scene Serialization (.uzscene load/save) | 1 week |
| 1.5.1 | Prefabs + Asset References | 1 week |
| 1.5.2 | Hot Reload (assets) | 1 week |
| 1.5.3 | Input System (Enhanced Input) | 1 week |
| 1.5.4 | Camera System (SpringArm, modes) | 1 week |

### Phase 1.6: Gameplay (was 1.5)

Physics, Animation, Audio, Navigation, Scripting — order unchanged, just renumbered.

## 7. Anti-Patterns in Current Roadmap

### Anti-Pattern 1: "Build the renderer, then figure out how to populate it"

The current roadmap builds a complete deferred PBR renderer with post-processing (1.3.0 through 1.3.2) before creating any entity/component system. This means all 1.3 demos will use hardcoded draw calls that get rewritten when 1.4 (Scene System) arrives. UE5 never had this problem because `UWorld` and `AActor` existed from day one.

### Anti-Pattern 2: "Infrastructure is boring, skip to the GPU"

Job System, VFS, Application Lifecycle, and Asset Registry are not glamorous but they are load-bearing walls. Every UE5 developer knows that `FEngineLoop`, `FTaskGraphInterface`, and `FAssetRegistry` are the backbone. UzEngine's roadmap treats them as Tier 2/Phase 2 afterthoughts.

### Anti-Pattern 3: "Render Graph first, passes second"

The render graph is an optimization pattern, not a prerequisite. UE5 shipped UE4.0 through 4.21 without a render graph. RDG was added in 4.22 as a refactor of existing passes. Building the render graph before you have passes to graph means you are designing an abstraction without knowing the concrete requirements.

### Anti-Pattern 4: "Material System depends on Render Graph"

This inverted dependency will force you to build the render graph before you can render a textured object. In UE5, you can create and use materials with zero render graph involvement. Materials produce shaders + parameter blocks; the renderer (graph or not) consumes them.

## 8. Key Files Referenced

- `e:\UzEngine\0.2\docs\ROADMAP.md` — current roadmap with phase structure
- `e:\UzEngine\0.2\docs\engine\README.md` — all 32 documented engine sections
- `e:\UzEngine\0.2\docs\engine\09-application-lifecycle\01-overview.md` — lifecycle planned for Milestone 2.0
- `e:\UzEngine\0.2\docs\engine\12-rendering\01-render-world.md` — render proxy design
- `e:\UzEngine\0.2\docs\engine\12-rendering\04-render-graph.md` — render graph design
- `e:\UzEngine\0.2\docs\engine\12-rendering\05-material-system.md` — material system (incorrectly depends on render graph)
- `e:\UzEngine\0.2\docs\engine\08-resource-system\01-overview.md` — VFS + asset pipeline
- `e:\UzEngine\0.2\docs\engine\07-platform\04-job-system.md` — job system planned for Milestone 1.5
- `e:\UzEngine\0.2\docs\engine\10-entity-component\02-component-model.md` — component hierarchy
- `e:\UzEngine\0.2\runtime\core\rhi\` — completed RHI with DX12 backend
- `e:\UzEngine\0.2\runtime\core\shader\` — completed shader pipeline
- `e:\UzEngine\0.2\runtime\core\renderer\` — basic renderer (camera, mesh, upload, vertex, constants)
