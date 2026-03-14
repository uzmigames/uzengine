# Entity/Component — World

> **⚠️ NOTE**: This file is deprecated. See the canonical documentation at [01-world-entity.md](./01-world-entity.md) for comprehensive API and usage patterns.

> **Status**: Done (Phase 3b-3.0b)
> **Source**: `runtime/engine/ecs/world.h`, `runtime/engine/ecs/entity.h`
> **Depends on**: Core systems, Memory (`Own<T>`)
> **References**: UE5 `UWorld`, Godot `SceneTree`

## Purpose

The **World** is the top-level simulation container. It owns all entities, components, subsystems, and simulation state. Multiple worlds can coexist (e.g., editor preview + play-in-editor).

## Entity

An **Entity** is a lightweight handle — an ID + generation counter (inspired by Godot's RID pattern). NOT a base class to inherit from.

```cpp
/// Opaque entity handle. 32-bit index + 32-bit generation for safe cross-reference.
/// Stale handles are detected: if the entity was destroyed and the slot reused,
/// the generation won't match and World::IsValid(entity) returns false.
struct Entity {
    uint32_t index;
    uint32_t generation;
};
```

Entities live in a **World**. They have:
- A **name** (debug-only, not used for lookup in shipping)
- A **parent entity** (optional, for hierarchy)
- Zero or more **Components** attached

### Why Generational Handles

Raw indices are dangerous — if entity 42 is destroyed and slot 42 is reused for a new entity, anyone holding a stale reference to "entity 42" silently accesses the wrong object. The generation counter prevents this:

```cpp
Entity e = world.CreateEntity("player");  // index=42, generation=1
world.DestroyEntity(e);                   // slot 42 freed, generation bumped to 2
Entity e2 = world.CreateEntity("enemy");  // reuses index=42, generation=2
world.IsValid(e);  // false — generation 1 != 2
world.IsValid(e2); // true
```

## World API

```cpp
/// Owns all entities, components, subsystems, and the simulation state.
class World {
public:
    // --- Entity Management ---
    Entity CreateEntity(const char* name = nullptr);
    void DestroyEntity(Entity e);               // deferred to end of frame
    bool IsValid(Entity e) const;

    // --- Component Access ---
    template<typename T> T& Add(Entity e, T&& component = {});
    template<typename T> T& Get(Entity e);
    template<typename T> const T& Get(Entity e) const;
    template<typename T> bool Has(Entity e) const;
    template<typename T> void Remove(Entity e);

    // --- Hierarchy ---
    void SetParent(Entity child, Entity parent);
    Entity GetParent(Entity e) const;
    std::span<const Entity> GetChildren(Entity e) const;

    // --- Queries ---
    template<typename... Ts>
    void ForEach(std::function<void(Entity, Ts&...)> fn);

    // --- Subsystems (per-world services) ---
    template<typename T> T* GetSubsystem();
    template<typename T> T* AddSubsystem();

    // --- Lifecycle (called by Application) ---
    void BeginPlay();
    void Tick(float delta);
    void PhysicsTick(float fixed_delta);
    void EndPlay();
};
```

## Entity Hierarchy & Transform Propagation

- `SetParent(child, parent)` makes `child`'s `SceneComponent.position` relative to parent
- World matrix is computed lazily: dirty flag set on position/rotation/scale change
- Dirty flag propagates to all children (like UE5's `FTransformUpdated` + Godot's `xform_change_list`)
- `GetWorldTransform(Entity)` recomputes if dirty, caches result

## Deferred Entity Destruction

`DestroyEntity()` is **always deferred** to the end of the current frame. This prevents:
- Dangling component references during iteration
- Use-after-free when a behavior destroys another entity that still has pending callbacks
- Iterator invalidation in `ForEach` loops

At end-of-frame, the engine:
1. Calls `OnEndPlay()` on all components of destroyed entities
2. Calls `OnUnregister()` on all components
3. Releases component storage
4. Increments the generation counter on the entity slot (making stale handles invalid)

## Component Storage — Sparse Set

Components are stored in **sparse sets** — one per component type. This gives:

| Operation | Cost |
|-----------|------|
| `Add<T>(entity)` | O(1) amortized |
| `Get<T>(entity)` | O(1) — sparse array lookup |
| `Has<T>(entity)` | O(1) — sparse array bounds check |
| `Remove<T>(entity)` | O(1) — swap-and-pop |
| `ForEach<T, U>` | O(N) — linear scan of smallest set, lookup others |

This is the data-oriented half of the hybrid model. Components of the same type are contiguous in memory — cache-friendly for systems that process all instances of a type.

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Entity model | `AActor` (heavy base class, GC-managed) | `Object` + `Node` (tree hierarchy) | **Lightweight handle (index + generation)** |
| Component storage | Per-actor linked list | Per-node properties | **Sparse sets (cache-coherent)** |
| Hierarchy | Actor attachment + component hierarchy | Node tree (deep) | **Flat ECS + parent/child entity relationships** |
| Destruction | Deferred (EndOfFrame) | Deferred (queue_free) | **Deferred (end of frame)** |
| Thread safety | Game thread only (mostly) | Main thread only | **Single-thread MVP, designed for multi-thread** |

### Why Flat ECS with Parent/Child Entities

Godot's deep tree traversal is a performance problem — walking a tree to find components requires pointer chasing at every level. Our model keeps entities flat in sparse sets (linear iteration) while supporting hierarchy for transform propagation only. Systems query components directly via `ForEach`, never by walking a tree.

## Rules

1. **NEVER hold raw pointers to components across frames** — sparse sets may relocate on `Remove`. Use `Entity` handles and `Get<T>()` each frame.
2. **NEVER destroy entities during iteration** — use `DestroyEntity()` (deferred) or collect a list and destroy after the loop.
3. **ALWAYS check `IsValid(entity)`** before accessing components on entities received from external sources (events, saved references).
4. **ALWAYS use `ForEach<T...>`** for bulk processing — it's the fast path. Individual `Get<T>` is for targeted access.
