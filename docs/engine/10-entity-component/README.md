# Entity Component System (ECS)

> **Status**: Done (Phase 3b-3.1b)
> **Source**: `runtime/engine/ecs/`
> **Key Files**: `entity.h`, `world.h`, `world.cpp`, `world_subsystem.h/.cpp`

## Overview

The ECS (Entity Component System) provides a data-oriented architecture for managing game entities, their components, and gameplay logic. Entities are lightweight generational handles, components are plain data structs, and systems are free functions or subsystem ticks that iterate entities via queries.

### What's Managed

- **Entity Lifecycle**: Creation, destruction, and use-after-destroy detection via generational handles
- **Component Storage**: Type-erased storage via EnTT with automatic memory management
- **Component Queries**: Efficient iteration over entities matching specific component patterns
- **WorldSubsystem Integration**: ECS world lifecycle tied to engine startup/shutdown

### Key Problems Solved

1. **Use-After-Destroy Bugs**: Generational handles prevent accessing entities that have been destroyed and their slots reused
2. **Data Cache Locality**: Component storage is packed and iterable, enabling SIMD-friendly iteration
3. **Flexible Composition**: Entities don't inherit from a base class; they're just bundles of components
4. **Hot-Reload Safety**: World and entities persist across script/game DLL reloads with handle validation

## Core Concepts

### Entity

A lightweight generational handle uniquely identifying a living entity:

```cpp
struct Entity {
    uint32_t id;           // Slot index in the registry
    uint32_t generation;   // Bumped when slot is reused after destruction
};
```

Entity handles are **immutable values** — pass them by value, store them freely. They remain valid as long as the entity is alive in the world.

### Component

A plain data struct attached to an entity. No virtual functions, no inheritance required:

```cpp
struct CTransform {
    Vec3 position;
    Quat rotation;
    Vec3 scale;
};

struct CHealth {
    float max_health;
    float current_health;
};
```

Follow the `C` prefix convention (stands for "Component") to distinguish component types from other classes.

### World

The ECS container — owns all entities and components. Provides the API for entity/component manipulation and iteration:

```cpp
uz::World world;
auto entity = world.CreateEntity();
world.AddComponent<CTransform>(entity, CTransform{});
auto& transform = world.GetComponent<CTransform>(entity);
```

### Query (View)

A read-only snapshot of entities matching specified components. Enables efficient data-parallel iteration:

```cpp
// Iterate all entities with CTransform and CHealth
for (auto [enttEntity, transform, health] : world.View<CTransform, CHealth>().each()) {
    // Update logic
}
```

## Architecture Diagrams

### Entity Lifecycle with Generational Handles

```
CreateEntity()           DestroyEntity()
    │                        │
    ├─ Allocate slot 0       ├─ Mark slot 0 as free
    ├─ Bump generation       ├─ Bump generation
    └─ Return Entity{0, 1}   └─ (World keeps history)
         │                        │
         └─ Entity is valid ──────┴─ Entity is INVALID
                                      (stale handle)

Next CreateEntity() reuses slot 0:
    ├─ Allocate slot 0
    ├─ Bump generation to 2
    └─ Return Entity{0, 2}

    Old handle Entity{0, 1} no longer matches slot's generation (2)
    → IsValid() returns false
```

### Component Storage Layout

```
World
├─ entt::registry (EnTT storage)
│  ├─ Component pool<CTransform>
│  │  └─ Packed array: [Transform0, Transform1, Transform2, ...]
│  │
│  ├─ Component pool<CHealth>
│  │  └─ Packed array: [Health0, Health1, Health2, ...]
│  │
│  └─ Entity ID pool
│     └─ [id0, id1, id2, ...] (sparse)
│
└─ generations_ vector
   └─ [gen0, gen1, gen2, ...] (generation per slot)
```

Entities with the same components are stored contiguously in memory, enabling cache-friendly iteration.

### Subsystem Initialization

```
EngineLoop::Initialize()
    │
    └─ SubsystemRegistry::InitAll()
        │
        └─ Phase Start (after core systems)
            │
            └─ WorldSubsystem::Init()
                │
                ├─ Create World
                ├─ Log "WorldSubsystem initialized"
                └─ Return true (always succeeds)
```

The World is created in the **Start** phase (after RHI, Renderer, etc.) because gameplay-level constructs depend on core systems being ready.

## Files Documentation

### entity.h

**Purpose**: Generational entity handle definition

**Key Types**:
- `Entity` — Struct with `id` (uint32) and `generation` (uint32)

**Key Methods**:
- `Entity::Invalid()` — Return null sentinel (id=UINT32_MAX)
- `Entity::IsValid()` — Check if handle looks valid (local check only)
- `operator==` / `operator!=` — Compare handles (both id and generation must match)

**Hash Support**:
- `std::hash<Entity>` specialization for use in `unordered_set`, `unordered_map`

### world.h / world.cpp

**Purpose**: ECS container and entity/component lifecycle management

**Key Classes**:
- `World` — Main ECS container

**Entity Lifecycle**:
- `CreateEntity()` — Allocate a new entity, return generational handle
- `DestroyEntity(entity)` — Remove entity and all components
- `IsValid(entity)` — Validate handle against current registry + generation
- `EntityCount()` — Current alive entity count
- `Clear()` — Destroy all entities

**Component Operations**:
- `AddComponent<T>(entity, args...)` — Attach component via in-place construction
- `RemoveComponent<T>(entity)` — Detach component
- `GetComponent<T>(entity)` / `GetComponent<T>(entity) const` — Access component reference
- `HasComponent<T>(entity)` — Check if entity has component

**Queries**:
- `View<Ts...>()` — Return EnTT view for iterating entities with specified components

**Direct Registry Access**:
- `GetRegistry()` — Access underlying `entt::registry` for advanced use cases

**Thread Safety**: NOT thread-safe. All methods must be called from the main thread (game loop).

### world_subsystem.h / world_subsystem.cpp

**Purpose**: ISubsystem adapter wrapping the World lifecycle

**Key Methods** (from ISubsystem):
- `GetName()` — Returns "World"
- `GetInitPhase()` — Returns `InitPhase::Start`
- `GetTickGroup()` — Returns `TickGroup::None` (World doesn't tick, systems do)
- `GetDependencies()` — Empty (World is self-contained)
- `Init()` — Create the World instance
- `Shutdown()` — Clear all entities and destroy World

**Access**:
- `GetWorld()` — Access the owned World pointer (or nullptr if not initialized)

## Common Patterns

### Creating and Using Entities

```cpp
// Get the World from the subsystem registry
auto* world_subsystem = static_cast<uz::WorldSubsystem*>(
    uz::SubsystemRegistry::Find("World")
);
if (!world_subsystem) return;  // World not initialized yet
uz::World* world = world_subsystem->GetWorld();

// Create an entity
uz::Entity player = world->CreateEntity();

// Add components
world->AddComponent<CTransform>(player, CTransform{
    .position = {0, 0, 0},
    .rotation = uz::Quat::Identity(),
    .scale = {1, 1, 1}
});

world->AddComponent<CHealth>(player, CHealth{
    .max_health = 100.0f,
    .current_health = 100.0f
});

// Access components
auto& health = world->GetComponent<CHealth>(player);
health.current_health -= 10.0f;

// Verify entity is still alive before using it
if (world->IsValid(player)) {
    auto& transform = world->GetComponent<CTransform>(player);
    // Use transform
}
```

### Iterating Entities

```cpp
// Single component query
for (auto [enttEntity, transform] : world->View<CTransform>().each()) {
    transform.position += {1, 0, 0};  // Move all entities right
}

// Multi-component query (AND logic)
for (auto [enttEntity, transform, health] : world->View<CTransform, CHealth>().each()) {
    // Process entities that have BOTH CTransform AND CHealth
    if (health.current_health <= 0) {
        // Despawn dead entity
        world->DestroyEntity(/* enttEntity to Entity conversion needed */);
    }
}

// Const iteration
for (const auto [enttEntity, transform, health] : world->View<CTransform, CHealth>().each()) {
    // Read-only access to components
    float distance = uz::Length(transform.position);
}
```

### Handle Validation

```cpp
// Store entity handles across frames
uz::Entity saved_entity = player;

// ... later ...

// Always validate before using
if (world->IsValid(saved_entity)) {
    auto& transform = world->GetComponent<CTransform>(saved_entity);
    // Safe to use
} else {
    // Entity was destroyed, find a new target
}
```

### Conditional Component Checks

```cpp
// Check before accessing
if (world->HasComponent<CWeapon>(entity)) {
    auto& weapon = world->GetComponent<CWeapon>(entity);
    weapon.ammo--;
}

// Or query only entities with specific components
for (auto [e, weapon] : world->View<CWeapon>().each()) {
    weapon.ammo = std::max(0, weapon.ammo - 1);
}
```

## Rules and Best Practices

### MUST DO

1. ✅ **Validate entity handles before use** — Use `IsValid()` if storing handles across frames
2. ✅ **Follow C prefix convention** — Name components `CTransform`, `CHealth`, etc.
3. ✅ **Keep components as plain data** — No virtual functions, no complex constructors
4. ✅ **Pass entities by value** — They're 8 bytes (id + generation), not pointers
5. ✅ **Use views for iteration** — `View<CTransform, CHealth>()` is faster than manual loops
6. ✅ **Call from main thread only** — World is not thread-safe

### MUST NOT DO

1. ❌ **Do NOT store entt::entity directly** — Use our `Entity` handle for generation tracking
2. ❌ **Do NOT create polymorphic components** — Inheritance is for systems, not components
3. ❌ **Do NOT assume component existence** — Always use `HasComponent()` or rely on view filtering
4. ❌ **Do NOT call World methods from threads** — Use message passing or lock-free queues
5. ❌ **Do NOT iterate and modify during iteration** — Collect changes and apply after the loop
6. ❌ **Do NOT store raw entity pointers** — Pointers become invalid when entities move in memory

## Performance Notes

- **Creation**: O(1) amortized
- **Destruction**: O(N) where N is the number of components on the entity (removes all)
- **AddComponent**: O(1) amortized (may reallocate pool)
- **RemoveComponent**: O(N) worst case if removing shifts data
- **View iteration**: O(M) where M is the number of matching entities (cache-friendly)
- **IsValid**: O(1)

Use `View<T, U>()` for hot loops — it returns a pre-filtered list of entities, eliminating unnecessary indirection.

## Phase Roadmap

| Phase | Milestone | Scope |
|-------|-----------|-------|
| **3b-3.0b** | World/Entity | Generational handles, component storage, queries, WorldSubsystem |
| **3b-3.1b** | Component Model | Component hierarchies, transform propagation, serialization |
| **3b-3.2b** | Property System | Reflection, hot property editing, data-driven components |
| **3b-3.3b** | Behaviors/Tick | Component-level tick hooks, lifecycle events, message passing |
| **3b-3.4b** | Scene Loader | .uzscene format, entity templates, prefab instantiation |
| **3b-3.5b** | Render Bridge | Entity to render world projection, batching, shadow casting |

## See Also

- [World/Entity Documentation](./01-world-entity.md) — Detailed API and usage patterns
- [Component Model](./02-component-model.md) — Component hierarchies and transformation (Phase 3b-3.1b)
- [Application Lifecycle](../09-application-lifecycle/) — Engine initialization and subsystems
- EnTT Library: `https://github.com/skypjack/entt` — Underlying ECS implementation
