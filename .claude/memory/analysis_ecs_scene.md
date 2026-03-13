---
name: ECS/Scene Systems Analysis for Roadmap
description: ECS and gameplay dependency chain analysis for UzEngine roadmap
type: project
---

# ECS/Scene Systems — Dependency Chain Analysis

## 1. ECS Foundation Dependency Chain

```
Layer 0: Core (DONE)
  Logging, Config, Paths, Memory, Math, Assertions

Layer 1: Application Lifecycle
  3-phase init (PreInit → Init → Start), main loop, shutdown
  Depends on: Core, RHI, Platform Window

Layer 2: World + Entity
  World (simulation container), Entity (generational handles), sparse set storage
  Depends on: Core (memory, Own<T>)
  NOTE: Does NOT depend on rendering. World is a pure simulation container.

Layer 3: Component Model + Events + Subsystems
  Component/ActorComponent/SceneComponent hierarchy
  MulticastDelegate, EventQueue (deferred)
  WorldSubsystem base + InitializeDependency<T>
  Depends on: World (Layer 2)

Layer 4: Property System (Reflection)  ← CRITICAL GATE
  ClassRegistry, UZ_PROPERTY, PropertyInfo, UZ_COMPONENT macro
  Depends on: Component Model (Layer 3)
  GATES: Scene loading, Inspector, Serialization, Hot-reload, Prefabs

Layer 5: Behaviors + Tick Architecture
  Behavior base class, UZ_REGISTER_BEHAVIOR, ClassRegistry name resolution
  TickGroup phases, UZ_SCHEDULED, tick budget
  Depends on: Component Model (Layer 3), Property System (Layer 4 — for scene resolution)

Layer 6: Scene System
  .uzscene/.uzprefab JSON loader, SceneLoader pipeline, prefab instancing
  Depends on: Property System (Layer 4 — type resolution + deserialization)
  Depends on: Behaviors (Layer 5 — behavior name resolution)
  Does NOT depend on rendering (see section 3)

Layer 7: Rendering Integration
  PrimitiveComponent → RenderProxy, RenderSubsystem, RenderWorld
  Depends on: RHI (1.2), Scene (Layer 6), Component Model (Layer 3)
```

### Exact Build Order

1. **World + Entity** — generational handles, sparse sets, entity CRUD
2. **Component Model** — Component/ActorComponent/SceneComponent, hierarchy, lifecycle
3. **Events** — MulticastDelegate, EventQueue, deferred flush
4. **Subsystems** — WorldSubsystem, dependency ordering
5. **Property System (reflection core)** — ClassRegistry, UZ_PROPERTY, PropertyInfo
6. **Property Serialization** — JSON serialize/deserialize via reflection
7. **Behaviors** — Behavior base, UZ_REGISTER_BEHAVIOR, ClassRegistry
8. **Tick Architecture** — TickGroup, OnTick, UZ_SCHEDULED (single-threaded MVP)
9. **Scene Loader** — .uzscene parser, component deserialization, hierarchy setup
10. **Prefab System** — .uzprefab, override merging, recursive instantiation

## 2. Property System Placement — EARLY FOUNDATION

The property system is a **hard gate** for the scene system. Evidence from docs:

- Scene format doc (§11-01): "Depends on: Property System (§21)"
- SceneLoader pipeline step: "Resolve type name via Property System"
- Component deserialization: "Deserialize JSON fields → UZ_PROPERTY fields"
- Prefab overrides: field-level merging requires property iteration
- Behavior resolution: ClassRegistry maps string names to factories
- Hot-reload: preserves property values by name+type matching

**Verdict**: Property System MUST come BEFORE Scene System. It belongs at Layer 4, immediately after Component Model. Without it, there is no way to:
- Load .uzscene files (can't resolve type names or deserialize fields)
- Display inspector (can't enumerate properties)
- Hot-reload C++ DLL (can't preserve state across reload)
- Implement prefab overrides (can't do field-level merge)

The Property System MVP needs only:
- ClassRegistry (type registration + lookup by name)
- UZ_PROPERTY macro (field declaration + metadata)
- PropertyInfo (offset, type, default value)
- JSON serialization (PropertySerializer::SerializeToJSON/DeserializeFromJSON)

Reactivity (Phase 3) and binary serialization (Phase 3) can come later.

## 3. Scene System vs Rendering — Scenes CAN Exist Without Rendering

**Yes, scenes should come before full rendering integration.** Evidence:

- World is a "pure simulation container" — no rendering dependency
- SceneComponent has transform but no render proxy (that's PrimitiveComponent)
- ActorComponent (movement, input, game logic) has no rendering at all
- Scene loader pipeline is: parse JSON → create entities → deserialize components → lifecycle
- RenderSubsystem is just one WorldSubsystem among many (Physics, Audio, Input)
- `ShouldCreate()` on subsystems allows skipping render (e.g., dedicated server)

**Recommended approach**:
1. Build Scene System with non-visual components only (SceneComponent, ActorComponent, Behaviors)
2. Test with headless scenes (entity creation, hierarchy, lifecycle callbacks)
3. Add PrimitiveComponent + RenderSubsystem as the bridge to rendering
4. This matches the existing roadmap: 1.3 (Renderer) → 1.4 (Scene) but Scene's ECS core doesn't need 1.3

**However**, the current roadmap has Scene (1.4) depending on Renderer (1.3) because MeshComponent needs to reference mesh/material assets and create render proxies. The ECS/World/Property foundation could be built in parallel with the renderer, then Scene ties them together.

## 4. Gameplay Systems Ordering

Based on docs and dependency analysis:

```
Tier 1 — Input (no deps beyond Platform + WorldSubsystem)
  InputSystem, InputAction, InputMappingContext, InputComponent
  Required for: anything interactive
  Phase: 2

Tier 2 — Physics (depends on: SceneComponent for transforms)
  PhysicsWorld subsystem, collision channels, queries, bodies
  Required for: character movement, projectiles, triggers
  Phase: 2-4

Tier 3 — Animation (depends on: SkeletalMeshComponent → PrimitiveComponent → RenderWorld)
  Skeleton, AnimSequence, GPU skinning, state machines
  Required for: character visuals, montages
  Phase: 3-4
  NOTE: Animation REQUIRES rendering (GPU skinning, skeletal mesh)

Tier 4 — Audio (depends on: SceneComponent for 3D position)
  AudioSubsystem, AudioSourceComponent, spatial audio
  Relatively independent — can be built any time after WorldSubsystem
  Phase: 2+

Tier 5 — Scripting (depends on: everything above for API bindings)
  C++ game DLL, Lua VM, hot-reload
  Must come last because it exposes all other systems as API
  Phase: late
```

### Per-Frame Execution Order (from tick architecture doc)

```
Frame Start
├── Poll OS events → Input System updates
├── PrePhysics tick group (input processing, movement intent, AI)
├── Physics step (fixed timestep, N substeps)
├── PostPhysics tick group (react to physics, animation, combat)
├── Update tick group (general game logic, camera)
├── PostUpdate tick group (cleanup, UI sync, render prep)
├── Animation evaluation (sample keyframes, blend, IK, joint matrices)
├── Render (collect visible, build draws, submit, present)
└── Deferred entity destroy
```

## 5. Minimum Playable Scene

A "minimum playable scene" requires the ability to:
1. Load a .uzscene file and create entities with components
2. Accept player input and translate to actions
3. Update game state each frame (behaviors tick)
4. See the result (rendering)

### Minimum Systems Required

| System | Why | Can Skip? |
|--------|-----|-----------|
| World + Entity | Entities exist | No |
| Component Model | Components attached | No |
| Property System (MVP) | Scene deserialization | No |
| Scene Loader | Load .uzscene | No |
| Behaviors | Game logic runs | No |
| Tick Architecture (MVP) | OnTick works | No |
| Input (basic) | Player can interact | No — but raw key polling suffices initially |
| Renderer (1.3.0 level) | See something | No |
| Transform hierarchy | Objects move | No |
| Camera | View the scene | No |

### Can Skip Initially

| System | Why Deferrable |
|--------|---------------|
| Physics | Use kinematic movement first |
| Animation | Static meshes work |
| Audio | Silent gameplay is testable |
| Prefabs | Direct entity definition in .uzscene |
| Scripting (Lua) | C++ behaviors suffice |
| Reactivity | Not needed for MVP |
| Events (deferred) | Immediate delegates suffice |
| Tick LOD/Budget | Single-threaded OnTick works |

### Minimum Playable Scene Definition

A `.uzscene` with:
- A camera entity (CameraComponent)
- A renderable entity (StaticMeshComponent + SceneComponent)
- A behavior controlling the camera (FPSCamera or similar)
- Input reading (even raw key polling)
- Transform hierarchy working (parent-child)
- Per-frame tick loop running

This corresponds to roughly: World + Components + Property System (MVP) + Scene Loader + Behaviors + Renderer (1.3.0) + Basic Input.

## 6. Proposed Milestone Breakdown

### Milestone A: ECS Core (no rendering, no scene files)
- World, Entity (generational handles), sparse sets
- Component, ActorComponent, SceneComponent
- Transform hierarchy + dirty flags
- MulticastDelegate + EventQueue
- WorldSubsystem + dependency ordering
- **Test**: create 100K entities, add/remove components, hierarchy, lifecycle

### Milestone B: Property System + Serialization (MVP)
- ClassRegistry, UZ_PROPERTY, PropertyInfo
- PropertySerializer (JSON only)
- UZ_COMPONENT auto-registration
- **Test**: round-trip serialize/deserialize components to JSON

### Milestone C: Behaviors + Scene Loader
- Behavior base, UZ_REGISTER_BEHAVIOR
- TickGroup + OnTick (single-threaded)
- SceneLoader (.uzscene JSON → World)
- **Test**: load .uzscene, behaviors tick, entities interact (headless)

### Milestone D: Rendering Bridge
- PrimitiveComponent + RenderProxy
- RenderSubsystem (collect visible → draw calls)
- StaticMeshComponent, LightComponent, CameraComponent
- **Test**: loaded scene renders to screen

### Milestone E: Input System
- InputSystem subsystem
- InputAction, InputMappingContext, InputComponent
- .uzinput JSON config
- **Test**: player controls camera in loaded scene

### Milestone F: First Playable
- Combine A+B+C+D+E
- A .uzscene loads, renders, accepts input, behaviors run
- **This is "minimum playable scene"**

### Milestone G+: Gameplay Systems (parallel tracks)
- G1: Physics (Jolt integration, bodies, queries)
- G2: Animation (skeleton, skinning, state machine)
- G3: Audio (miniaudio, spatial)
- G4: Prefabs (.uzprefab, overrides, nesting)
- G5: Scripting (C++ DLL hot-reload, Lua VM)
