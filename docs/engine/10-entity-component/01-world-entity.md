# World and Entity API

> **Status**: Done (Phase 3b-3.0b)
> **Source**: `runtime/engine/ecs/entity.h`, `runtime/engine/ecs/world.h`, `runtime/engine/ecs/world.cpp`
> **Depends on**: Core logging, assertions
> **References**: EnTT library (`https://github.com/skypjack/entt`)

## Overview

The World and Entity API provides the foundation for the ECS architecture. Entity is a lightweight generational handle, and World is the container managing all entity/component lifecycle operations.

## Entity — Generational Handle

### Definition

```cpp
struct Entity {
    uint32_t id;           ///< Entity slot index. UINT32_MAX = null/invalid.
    uint32_t generation;   ///< Generation counter for use-after-destroy detection.
};
```

An Entity is **immutable**, **copyable**, and **comparable by value**. It's cheap to pass around (8 bytes).

### Construction and Null Checks

```cpp
// Default construction (or explicit invalid sentinel)
uz::Entity entity = uz::Entity::Invalid();

// Check if a handle looks like a valid ID (not UINT32_MAX)
if (entity.IsValid()) {
    // Has a non-null slot index
}

// Comparison
uz::Entity a = world.CreateEntity();
uz::Entity b = a;
if (a == b) { /* True: same id and generation */ }

// Null check
if (entity == uz::Entity::Invalid()) { /* True if null */ }
```

### Generational Tracking

When an entity is destroyed and its slot is later reused, the generation increments:

```cpp
uz::Entity e1 = world.CreateEntity();  // Entity{0, 1}
world.DestroyEntity(e1);

uz::Entity e2 = world.CreateEntity();  // Slot 0 reused, Entity{0, 2}

// e1's stored generation (1) no longer matches slot's generation (2)
assert(!world.IsValid(e1));  // True: e1 is now stale
assert(world.IsValid(e2));   // True: e2 is alive
```

### Hash Support

Entity can be used in hash containers:

```cpp
std::unordered_set<uz::Entity> active_entities;
std::unordered_map<uz::Entity, std::string> entity_names;

auto entity = world.CreateEntity();
active_entities.insert(entity);
entity_names[entity] = "Player";
```

### Ownership Rules

- **No ownership semantics**: Entity is a handle, not a pointer or unique_ptr
- **Safe to copy**: Pass by value everywhere
- **Safe to store**: Keep handles in containers across frames (validate before use)
- **Reference semantics**: Multiple handles to the same entity are independent (both must be valid)

## World — ECS Container

### Construction and Lifecycle

```cpp
// Default construction
uz::World world;

// Movable (registry is movable)
uz::World world1;
uz::World world2 = std::move(world1);

// Not copyable (registry is not copyable)
// uz::World copy = world;  // Error: deleted copy constructor
```

### Entity Lifecycle

#### CreateEntity()

```cpp
/// Creates a new entity and returns a generational handle.
[[nodiscard]] Entity CreateEntity();
```

Allocates a new entity in the registry and returns a handle with the current generation.

**Example**:
```cpp
uz::Entity player = world.CreateEntity();
uz::Entity enemy = world.CreateEntity();

std::cout << "Player: id=" << player.id << ", gen=" << player.generation << '\n';
// Output: Player: id=0, gen=1
```

**Side Effects**:
- Logs at Trace level: `"CreateEntity: id=X, generation=Y"`
- May reallocate the internal entity storage if capacity is exceeded

#### DestroyEntity()

```cpp
/// Destroys an entity, removing all its components.
///
/// # Arguments
/// * `entity` - Must be valid (asserts via UZ_CHECK).
void DestroyEntity(Entity entity);
```

Removes the entity and all its components. The entity's slot may be reused.

**Example**:
```cpp
uz::Entity entity = world.CreateEntity();
world.AddComponent<CTransform>(entity, {});

// ... later ...

world.DestroyEntity(entity);

// entity is now stale
if (!world.IsValid(entity)) {
    std::cout << "Entity destroyed\n";
}
```

**Pre-Conditions**:
- `world.IsValid(entity)` must be true, or assertion fails
- All components are automatically removed (no cleanup hook)

**Side Effects**:
- Logs at Trace level: `"DestroyEntity: id=X, generation=Y"`
- May reallocate storage if entity had many components

#### IsValid()

```cpp
/// Validates whether an entity handle still refers to a living entity.
///
/// Checks both that the underlying EnTT entity is alive AND that the
/// generation stored in the handle matches the current slot generation.
[[nodiscard]] bool IsValid(Entity entity) const;
```

Validates:
1. Entity's ID is not UINT32_MAX (null check)
2. Entity's ID is within the generations table bounds
3. Entity's stored generation matches the current slot generation
4. EnTT registry considers the entity alive

**Example**:
```cpp
// Immediately after creation
uz::Entity e = world.CreateEntity();
assert(world.IsValid(e));  // True

// After destruction
world.DestroyEntity(e);
assert(!world.IsValid(e));  // True: e is stale

// After new entity reuses the slot
uz::Entity e2 = world.CreateEntity();
assert(!world.IsValid(e));  // Still stale (generation mismatch)
assert(world.IsValid(e2));  // True: e2 is alive
```

**Performance**: O(1) — hash table lookup

### Component Operations

All component operations require a valid entity. An assertion fails if you pass an invalid entity.

#### AddComponent()

```cpp
/// Adds a component to an entity by constructing it in-place.
///
/// If the entity already has a component of type T, this replaces it.
template <typename T, typename... Args>
T& AddComponent(Entity entity, Args&&... args);
```

Emplaces a component into the entity. If the component already exists, it's replaced.

**Example**:
```cpp
struct CPosition {
    float x = 0, y = 0, z = 0;
};

struct CHealth {
    float max_health;
    float current_health;
};

uz::Entity entity = world.CreateEntity();

// Construct with default values
auto& pos = world.AddComponent<CPosition>(entity);

// Construct with arguments
auto& health = world.AddComponent<CHealth>(entity, 100.0f, 100.0f);

// Accessing the returned reference
health.current_health -= 10.0f;
```

**Return Value**: Mutable reference to the emplaced component

**Side Effects**:
- May reallocate the component pool for type T

**Precondition**: Entity must be valid

#### RemoveComponent()

```cpp
/// Removes a component from an entity.
///
/// No-op if the entity does not have the component (logs a warning).
template <typename T>
void RemoveComponent(Entity entity);
```

Removes a component. If the entity doesn't have the component, logs a warning and returns.

**Example**:
```cpp
if (world.HasComponent<CWeapon>(entity)) {
    world.RemoveComponent<CWeapon>(entity);
}

// Or just remove unconditionally (logs warning if missing)
world.RemoveComponent<CWeapon>(entity);
```

**Side Effects**:
- Logs at Warning level if component doesn't exist
- May reallocate storage (downgrade pool size)

**Precondition**: Entity must be valid

#### GetComponent() and GetComponent() const

```cpp
/// Returns a mutable reference to an entity's component.
template <typename T>
[[nodiscard]] T& GetComponent(Entity entity);

/// Returns a const reference to an entity's component.
template <typename T>
[[nodiscard]] const T& GetComponent(Entity entity) const;
```

Access a component by reference. Asserts if the entity doesn't have the component.

**Example**:
```cpp
auto& position = world.GetComponent<CPosition>(entity);
position.x += speed * delta_time;

const auto& health = world.GetComponent<CHealth>(entity);
if (health.current_health <= 0) {
    // Entity is dead
}
```

**Return Value**: Mutable or const reference to the component (never null)

**Pre-Conditions**:
- Entity must be valid
- Entity must have component T, or assertion fails

**Performance**: O(1) — direct pool access

#### HasComponent()

```cpp
/// Checks whether an entity has a specific component.
template <typename T>
[[nodiscard]] bool HasComponent(Entity entity) const;
```

Safely check if a component exists without asserting.

**Example**:
```cpp
uz::Entity entity = world.CreateEntity();

// Check before accessing
if (world.HasComponent<CWeapon>(entity)) {
    auto& weapon = world.GetComponent<CWeapon>(entity);
    weapon.ammo--;
} else {
    // This entity is unarmed
}
```

**Pre-Condition**: Entity must be valid (asserts if not)

**Performance**: O(1)

### Iteration and Queries

#### View<Ts...>()

```cpp
/// Returns an EnTT view for iterating entities with the specified components.
///
/// This exposes EnTT's view API directly — no additional wrapping.
template <typename... Ts>
[[nodiscard]] auto View();

template <typename... Ts>
[[nodiscard]] auto View() const;
```

Returns an EnTT view enabling efficient iteration over all entities with the specified components.

**Single Component View**:
```cpp
// Iterate all entities with CPosition
for (auto [enttEntity, position] : world.View<CPosition>().each()) {
    position.x += 1.0f;
}
```

**Multi-Component View** (AND logic):
```cpp
// Iterate all entities with BOTH CPosition AND CHealth
for (auto [enttEntity, position, health] : world.View<CPosition, CHealth>().each()) {
    // Both components are present
    if (health.current_health <= 0) {
        // Despawn dead entity
        // Note: can't destroy during iteration, collect and remove after
    }
}
```

**Const View**:
```cpp
for (const auto [enttEntity, position] : world.View<CPosition>().each()) {
    // Read-only access
    float distance = std::sqrt(position.x * position.x + position.y * position.y);
}
```

**Empty View** (all entities):
```cpp
// Iterate all entities regardless of components
for (auto enttEntity : world.View<>().each()) {
    // enttEntity is raw entt::entity
    uz::Entity e = uz::Entity{static_cast<uint32_t>(entt::to_integral(enttEntity)), 0};
    // (Note: generation is lost, use with caution)
}
```

**Return Type**: `entt::view<Ts...>` — Direct EnTT view object. See EnTT docs for full API.

**Performance**:
- View construction: O(1)
- Iteration: O(M) where M is the number of matching entities
- No allocation; views are temporary query objects

**Important Notes**:
- Views return raw `entt::entity` handles, not our `Entity` handles (no generation tracking)
- Don't store views; they become invalid if components are added/removed
- Safe to iterate while adding/removing components of different types
- Unsafe to add/remove components being iterated

### Utility Methods

#### EntityCount()

```cpp
/// Returns the number of currently alive entities.
[[nodiscard]] size_t EntityCount() const;
```

Returns the count of entities that have been created and not yet destroyed.

**Example**:
```cpp
std::cout << "Entities in world: " << world.EntityCount() << '\n';
```

**Performance**: O(1)

#### Clear()

```cpp
/// Destroys all entities and their components.
///
/// After this call, all previously returned Entity handles become stale.
void Clear();
```

Destroys all entities in the world. Useful for unloading scenes or restarting.

**Example**:
```cpp
// Unload current level
world.Clear();

// All entity handles are now invalid
uz::Entity old_player = /* ... */;
assert(!world.IsValid(old_player));

// Create new level
auto new_player = world.CreateEntity();
```

**Side Effects**:
- Logs at Info level: `"World::Clear: destroying N entities"`
- Clears all components and entity allocations
- Does NOT reset the generation table (old handles stay invalid)

**Performance**: O(N) where N is the number of entities

#### GetRegistry() / GetRegistry() const

```cpp
/// Returns a mutable reference to the underlying EnTT registry.
///
/// Prefer the World API for entity/component operations. Direct registry access
/// is provided for advanced use cases (custom queries, signals, observers).
[[nodiscard]] entt::registry& GetRegistry() { return registry_; }

/// Returns a const reference to the underlying EnTT registry.
[[nodiscard]] const entt::registry& GetRegistry() const { return registry_; }
```

Provides direct access to the EnTT registry for advanced use cases.

**⚠️ Warning**: Creating or destroying entities directly via the registry bypasses generation tracking. Only use the registry for component queries and observers.

**Example**:
```cpp
// Query using EnTT's sort API
auto& registry = world.GetRegistry();
registry.sort<CPosition>([](const CPosition& a, const CPosition& b) {
    return a.x < b.x;
});

// Use EnTT's observer for component change detection
entt::observer observer{registry, entt::collector.replace<CPosition>()};
observer.each([](entt::entity entity) {
    // CPosition was modified
});
```

## Component Patterns

### Basic Component Design

Components are plain data structures with **no virtual functions, no side effects**:

```cpp
// ✅ Good: Plain data
struct CTransform {
    uz::Vec3 position;
    uz::Quat rotation;
    uz::Vec3 scale;
};

// ✅ Good: Simple types
struct CHealth {
    float max_health;
    float current_health;
    uint32_t armor;
};

// ❌ Bad: Virtual function
struct CBadComponent {
    virtual void OnUpdate() = 0;  // Don't do this
};

// ❌ Bad: Side effect in constructor
struct CConfiguration {
    CConfiguration() {
        std::ifstream file("config.txt");  // Don't do this
        // ...
    }
};
```

### Optional Components

Use `HasComponent()` to check before accessing:

```cpp
if (world.HasComponent<CWeapon>(entity)) {
    auto& weapon = world.GetComponent<CWeapon>(entity);
    weapon.ammo--;
}
```

Or query for entities with the component:

```cpp
for (auto [e, weapon] : world.View<CWeapon>().each()) {
    weapon.ammo--;
}
```

### Component Defaults

Initialize components with sensible defaults:

```cpp
struct CHealth {
    float max_health = 100.0f;
    float current_health = 100.0f;
    uint32_t armor = 0;
};

auto& health = world.AddComponent<CHealth>(entity);  // Uses defaults
```

Or provide explicit values:

```cpp
auto& health = world.AddComponent<CHealth>(entity, 50.0f, 50.0f, 10);
```

## Advanced Usage

### Storing Entity Handles Across Frames

Always validate handles before use, since entities can be destroyed:

```cpp
class PlayerController {
public:
    void SetTarget(uz::Entity target) {
        target_ = target;
    }

    void Update(uz::World& world) {
        // Validate before using
        if (!world.IsValid(target_)) {
            target_ = uz::Entity::Invalid();
            return;
        }

        auto& target_pos = world.GetComponent<CPosition>(target_);
        // Use target_pos safely
    }

private:
    uz::Entity target_ = uz::Entity::Invalid();
};
```

### Bulk Operations

Don't destroy entities during iteration. Collect and process after:

```cpp
std::vector<uz::Entity> to_delete;

for (auto [enttEntity, health] : world.View<CHealth>().each()) {
    if (health.current_health <= 0) {
        // Don't destroy here; store for later
        uz::Entity e{static_cast<uint32_t>(entt::to_integral(enttEntity)), 0};
        to_delete.push_back(e);
    }
}

// Now safe to destroy
for (auto entity : to_delete) {
    world.DestroyEntity(entity);
}
```

### Dynamic Component Addition

Add components based on data or gameplay conditions:

```cpp
void SpawnEnemy(uz::World& world, EnemyType type) {
    uz::Entity enemy = world.CreateEntity();

    world.AddComponent<CPosition>(enemy, {0, 0, 0});
    world.AddComponent<CHealth>(enemy, 50, 50, 0);

    switch (type) {
        case EnemyType::Melee:
            world.AddComponent<CMelee>(enemy);
            break;
        case EnemyType::Ranged:
            world.AddComponent<CWeapon>(enemy, 10);
            break;
    }
}
```

### Querying with Multiple Components

The view operates with AND semantics — all specified components must be present:

```cpp
// Entities with BOTH CPosition AND CHealth AND CWeapon
for (auto [e, pos, health, weapon] : world.View<CPosition, CHealth, CWeapon>().each()) {
    // Only armed, alive entities reach here
    weapon.ammo--;
}
```

For OR logic, iterate multiple views:

```cpp
// All armed entities (melee OR ranged)
for (auto [e, melee] : world.View<CMelee>().each()) {
    melee.Attack();
}
for (auto [e, ranged] : world.View<CWeapon>().each()) {
    ranged.Shoot();
}
```

## Thread Safety

**NOT thread-safe**. All World methods must be called from the main thread (game loop).

For multi-threaded scenarios:

1. **Message Passing**: Queue entity creation/destruction/component changes, apply on main thread
2. **Lock-Free Queues**: Use `moodycamel::ConcurrentQueue` to pass commands from worker threads
3. **Per-Thread Worlds**: Create separate World instances per worker if isolation is needed
4. **Read-Only Views**: Views can be read by multiple threads if no world modifications occur

**Example with message passing**:
```cpp
class WorldCommands {
public:
    void QueueAddComponent(uz::Entity entity, std::function<void(uz::World&)> command) {
        commands_.push_back(command);
    }

    void Apply(uz::World& world) {
        for (auto& cmd : commands_) {
            cmd(world);
        }
        commands_.clear();
    }

private:
    std::vector<std::function<void(uz::World&)>> commands_;
};
```

## Engine Comparison

| Aspect | UE5 | Godot | **UzEngine** |
|--------|-----|-------|------------|
| **Handle system** | Sparse entity IDs + generation in UObject | Integer node IDs | **Generational Entity struct** |
| **Use-after-free** | FWeakObjectPtr validation | Null checking | **Generation mismatch detection** |
| **Container** | Implicit (AActor hierarchy) | Explicit (World/Viewport) | **Explicit World (EnTT)** |
| **Thread safety** | Main thread only (FScopeLock) | Main thread only | **Main thread only (no locks)** |
| **Performance** | OOP (vtable lookup) | OOP (vtable lookup) | **Data-oriented (packed arrays)** |
| **Iteration** | Manual actor list | SceneTree traversal | **EnTT views (cache-friendly)** |
| **Component model** | Properties + UObject | Components in scene tree | **Plain data structs** |
| **Composition** | Single inheritance (AActor) | Node hierarchy | **Flat composition (no hierarchy)** |

## Troubleshooting

### "Assertion failed: IsValid(entity)"

**Cause**: Trying to use a stale entity handle (was destroyed or belongs to a different World).

**Fix**: Check `world.IsValid(entity)` before accessing. Store handles carefully across frames.

### "RemoveComponent: entity does not have the requested component"

**Cause**: Trying to remove a component that was never added.

**Fix**: Use `HasComponent()` before removing, or catch the warning log.

### "View iteration is slow"

**Cause**: Iterating too many entities with a sparse component set.

**Fix**: Add more specific components to the view to narrow the result set. The view filters efficiently but can't optimize away unrelated entities.

### Entities disappear after destroying one

**Cause**: Destroying an entity during view iteration may invalidate the view iterator.

**Fix**: Collect entities to delete in a separate list, then delete after iteration completes.

### Can't iterate empty world

**Cause**: Views return empty if no entities exist.

**Fix**: This is expected. Create entities before querying.

## Performance Tips

1. **Use specific views**: `View<CPosition, CHealth>()` is faster than `View<CPosition>()` if most entities aren't both
2. **Batch operations**: Collect changes and apply them after iteration
3. **Store handles by value**: Entity is 8 bytes; pass and store freely
4. **Avoid recreating views**: Don't call `View<>()` every frame in a tight loop; compute once
5. **Keep components small**: Cache lines are 64 bytes; multiple small components = better packing

## See Also

- [Entity Component System Overview](./README.md) — High-level architecture and patterns
- [Component Model](./02-component-model.md) — Component hierarchies and transformation (Phase 3b-3.1b)
- EnTT Documentation: `https://skypjack.github.io/entt/`
- Application Lifecycle: `../09-application-lifecycle/README.md`
