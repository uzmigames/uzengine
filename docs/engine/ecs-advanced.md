# Advanced ECS Patterns & Registry Management

> **Owner**: `uz_engine.lib` + `game.dll` (EnTT registry)
> **Version**: EnTT 3.14+
> **Thread safety**: Game Logic thread only; registry is not thread-safe

---

## Overview

This document covers advanced patterns for using EnTT in UzEngine: the registry context (ctx) pattern for service locator functionality, reactive systems using signals, component versioning for hot-reload, entity templates and archetypes, and efficient transform hierarchies.

The core ECS design uses the **registry as the main data structure**, replacing the older "GameContext god object" pattern. The registry's `.ctx()` method provides a type-safe, extensible way to access global services.

---

## Registry Context Pattern (Service Locator)

### Problem: GameContext God Object

Earlier versions of UzEngine used a centralized `GameContext` containing pointers to all subsystems:

```cpp
// Old pattern (❌ AVOID)
struct GameContext {
    SceneManager* scene_manager;
    PhysicsWorld* physics;
    AudioSystem* audio;
    ShaderSystem* shaders;
    // ... 20 more pointers ...
};

// Globals dependencies, hard to test
void initialize_game(GameContext& ctx) {
    ctx.physics->set_gravity(Vec3(0, -9.8f, 0));
}
```

**Problems**:
- Every system knows about every other system (tight coupling)
- God object keeps growing (adds 100+ lines per new system)
- Testing requires mocking the entire GameContext
- Circular dependencies are common

### Solution: Registry Context

EnTT's registry provides a context dictionary accessible via `.ctx()`:

```cpp
// New pattern (✅ PREFER)
class PhysicsSystem {
public:
    explicit PhysicsSystem(entt::registry& registry) : registry_(registry) {
        // Store `this` in registry context
        registry_.ctx().emplace<PhysicsSystem*>(this);
    }

    void set_gravity(Vec3 g) { gravity_ = g; }

private:
    entt::registry& registry_;
    Vec3 gravity_;
};

// Game initialization
entt::registry registry;

auto physics = std::make_unique<PhysicsSystem>(registry);
auto audio = std::make_unique<AudioSystem>(registry);
auto scene_mgr = std::make_unique<SceneManager>(registry);

// Anywhere in game code:
void some_function(entt::registry& registry) {
    auto* physics = registry.ctx().at<PhysicsSystem*>();
    physics->set_gravity(Vec3(0, -9.8f, 0));
}
```

**Benefits**:
- Zero coupling between systems (they don't know each other)
- Registry context is type-safe (compile-time errors for missing services)
- Easy to test (mock or inject services)
- Self-documenting (clear what a function needs)

### Registry Context API

```cpp
// Store a service in registry context
registry.ctx().emplace<PhysicsSystem*>(physics_ptr);

// Retrieve a service (throws if not found)
auto* physics = registry.ctx().at<PhysicsSystem*>();

// Retrieve with fallback
if (auto* physics = registry.ctx().find<PhysicsSystem*>()) {
    physics->set_gravity(...);
}

// Check if service exists
bool has_physics = registry.ctx().contains<PhysicsSystem*>();

// Remove service
registry.ctx().erase<PhysicsSystem*>();
```

### Example: Decoupled Audio Event Handler

```cpp
// Audio system registers itself
class AudioSystem {
public:
    AudioSystem(entt::registry& registry) : registry_(registry) {
        registry.ctx().emplace<AudioSystem*>(this);

        // Subscribe to damage events (without knowing where they come from)
        EventBus<PlayerDamaged>::subscribe([this](const PlayerDamaged& e) {
            play_sound("sfx/hit");
        });
    }

private:
    entt::registry& registry_;
};

// Damage system emits events (without knowing audio system exists)
class DamageSystem {
public:
    void apply_damage(NodeHandle target, float amount) {
        // Emit event; systems listening will react
        EventBus<PlayerDamaged>::emit(PlayerDamaged{ target, amount });
    }
};

// Neither system knows about the other!
```

---

## Reactive Systems (Signals)

EnTT provides signals for reactive components — fire code when components are created, updated, or destroyed.

### Component Lifecycle Signals

```cpp
// src/game/reactive_systems.h

class TransformUpdateSystem {
public:
    TransformUpdateSystem(entt::registry& registry) : registry_(registry) {
        // Called when CTransform is added to an entity
        registry.on_construct<CTransform>().connect<&TransformUpdateSystem::on_transform_add>(this);

        // Called when CTransform is updated
        registry.on_update<CTransform>().connect<&TransformUpdateSystem::on_transform_update>(this);

        // Called when CTransform is removed
        registry.on_destroy<CTransform>().connect<&TransformUpdateSystem::on_transform_destroy>(this);
    }

    ~TransformUpdateSystem() {
        // Signals are automatically disconnected on function destruction
    }

    void on_transform_add(entt::registry& r, entt::entity e) {
        auto& transform = r.get<CTransform>(e);
        ctx_.log_info(fmt::format("Entity {} added with transform at {}",
                                  static_cast<uint32_t>(e),
                                  transform.position));
    }

    void on_transform_update(entt::registry& r, entt::entity e) {
        // Called after component is modified
        auto& transform = r.get<CTransform>(e);
        // Update derived data (spatial index, matrices, etc.)
    }

    void on_transform_destroy(entt::registry& r, entt::entity e) {
        ctx_.log_info(fmt::format("Entity {} transform removed",
                                  static_cast<uint32_t>(e)));
    }

private:
    entt::registry& registry_;
};
```

### Use Cases for Reactive Patterns

**1. Spatial Index Maintenance**

```cpp
class SpatialSystem {
public:
    SpatialSystem(entt::registry& registry) : registry_(registry) {
        // Update spatial index when transform changes
        registry.on_update<CTransform>().connect<&SpatialSystem::on_entity_moved>(this);
        registry.on_construct<CTransform>().connect<&SpatialSystem::on_entity_added>(this);
        registry.on_destroy<CTransform>().connect<&SpatialSystem::on_entity_removed>(this);
    }

private:
    void on_entity_moved(entt::registry& r, entt::entity e) {
        auto& transform = r.get<CTransform>(e);
        spatial_index_.update(e, transform.get_bounds());  // O(log n) update
    }

    void on_entity_added(entt::registry& r, entt::entity e) {
        auto& transform = r.get<CTransform>(e);
        spatial_index_.insert(e, transform.get_bounds());
    }

    void on_entity_removed(entt::registry& r, entt::entity e) {
        spatial_index_.remove(e);
    }

    SpatialIndex spatial_index_;
};
```

**2. Physics Body Creation**

```cpp
class PhysicsSystem {
public:
    PhysicsSystem(entt::registry& registry) : registry_(registry) {
        // When a physics component is added, create Jolt body
        registry.on_construct<CRigidBody>().connect<&PhysicsSystem::on_rigidbody_added>(this);
        registry.on_destroy<CRigidBody>().connect<&PhysicsSystem::on_rigidbody_removed>(this);
    }

private:
    void on_rigidbody_added(entt::registry& r, entt::entity e) {
        auto& rb = r.get<CRigidBody>(e);
        auto& transform = r.get<CTransform>(e);

        // Create Jolt body with current position
        JPH::BodyID jolt_id = create_jolt_body(transform, rb);

        // Store Jolt handle for later access
        rb.jolt_body_id = jolt_id;
    }

    void on_rigidbody_removed(entt::registry& r, entt::entity e) {
        auto& rb = r.get<CRigidBody>(e);
        destroy_jolt_body(rb.jolt_body_id);
    }
};
```

**3. Change Detection for Lua Scripts**

```cpp
class LuaScriptSystem {
public:
    LuaScriptSystem(entt::registry& registry) : registry_(registry) {
        // Hot-reload: detect when script components are added/modified
        registry.on_construct<CLuaScript>().connect<&LuaScriptSystem::on_script_added>(this);
        registry.on_update<CLuaScript>().connect<&LuaScriptSystem::on_script_modified>(this);
    }

private:
    void on_script_added(entt::registry& r, entt::entity e) {
        auto& script = r.get<CLuaScript>(e);
        load_and_compile_script(script);
        call_on_start(script);
    }

    void on_script_modified(entt::registry& r, entt::entity e) {
        auto& script = r.get<CLuaScript>(e);
        reload_script(script);  // Hot-reload
    }
};
```

---

## Component Versioning for Hot-Reload

When game.dll is reloaded, component data structures may have changed (new fields added, old fields removed). Components need a versioning system to migrate old data to new schema.

### Component Versioning Pattern

```cpp
// src/game/components/character.h

struct CCharacter {
    static constexpr uint32_t VERSION = 2;

    // v1: basic fields
    float health = 100.0f;
    float max_health = 100.0f;

    // v2: added new field
    float shield = 0.0f;
    float max_shield = 50.0f;

    // v3: (future) might add mana system
};

// Migration function: convert v1 to v2
struct CCharacterMigration {
    static void from_v1_to_v2(CCharacter& c) {
        // New shield field defaults to 0
        // Old data is preserved
    }

    static void apply(uint32_t old_version, CCharacter& c) {
        if (old_version < 2) from_v1_to_v2(c);
        if (old_version < 3) from_v2_to_v3(c);
    }
};

// Registry hot-reload hook
void registry_hot_reload(entt::registry& registry) {
    auto view = registry.view<CCharacter>();
    for (auto entity : view) {
        auto& comp = view.get<CCharacter>(entity);
        // Get old version from serialized data
        uint32_t old_version = get_component_version(entity, "CCharacter");
        CCharacterMigration::apply(old_version, comp);
    }
}
```

### Serialization with Version

When saving entities:

```cpp
struct SerializedComponent {
    std::string type_name;
    uint32_t version;
    std::vector<uint8_t> data;
};

void serialize_entity(entt::entity e, const entt::registry& registry) {
    std::vector<SerializedComponent> components;

    // Check each component type
    if (registry.has<CCharacter>(e)) {
        auto& comp = registry.get<CCharacter>(e);
        components.push_back({
            .type_name = "CCharacter",
            .version = CCharacter::VERSION,
            .data = serialize_binary(&comp, sizeof(comp)),
        });
    }

    // Save to disk or memory
    save_entity_data(e, components);
}

void deserialize_entity(entt::entity e, entt::registry& registry,
                        const std::vector<SerializedComponent>& components) {
    for (const auto& comp : components) {
        if (comp.type_name == "CCharacter") {
            auto* char_comp = new CCharacter();
            deserialize_binary(comp.data, char_comp);

            // Apply migration if version mismatch
            if (comp.version < CCharacter::VERSION) {
                CCharacterMigration::apply(comp.version, *char_comp);
            }

            registry.emplace<CCharacter>(e, *char_comp);
            delete char_comp;
        }
    }
}
```

---

## Entity Templates & Archetypes

Entity templates define the initial set of components and their values. Think of them as prefab blueprints.

### Template Definition (.uzentity format)

Entity templates are INI files:

```ini
# res://entities/player.uzentity
# Template that defines a player character

[metadata]
id = player_template
version = 1
description = Player character archetype

[component.CTransform]
position.x = 0
position.y = 0
position.z = 0
rotation.x = 0
rotation.y = 0
rotation.z = 0
rotation.w = 1

[component.CCharacter]
health = 100
max_health = 100
shield = 0
max_shield = 50

[component.CAnimator]
skeleton = res://models/player.gltf
animation_layer_count = 4

[component.CLuaScript]
script_path = res://scripts/player_controller.lua

[component.CRigidBody]
mass = 80
friction = 0.3
restitution = 0.1

[component.CColliderCapsule]
radius = 0.3
half_height = 0.9
offset.y = 0.9
```

### C++ Instantiation API

```cpp
// src/game/entity_template.h

struct EntityTemplate {
    StringId id;
    std::vector<std::pair<std::string, Variant>> components;
};

class EntityTemplateRegistry {
public:
    // Load template from .uzentity asset
    bool load_template(std::string_view asset_path);

    // Instantiate template as new entity in registry
    entt::entity create_from_template(entt::registry& registry,
                                       StringId template_id);

    // Override component values when instantiating
    entt::entity create_from_template(entt::registry& registry,
                                       StringId template_id,
                                       const TemplateOverrides& overrides);
};

// Instantiation
entt::entity player = registry.create();
template_registry.instantiate(registry, player, "player_template");

// With overrides
TemplateOverrides overrides;
overrides.emplace<CTransform>(CTransform{
    .position = Vec3(10, 0, 0),  // Override spawn position
});

entt::entity npc = registry.create();
template_registry.instantiate(registry, npc, "enemy_template", overrides);
```

### Lua Template API

```lua
-- Create entity from template
local player = ctx.world:create_from_template("player_template")

-- With overrides
local npc = ctx.world:create_from_template("enemy_template", {
    position = Vec3(10, 5, 0),
    max_health = 50,  -- Override default max_health
})
```

---

## Prefab Deep Nesting & Hierarchies

### Prefab Hierarchy Problem

Prefabs can contain child entities (nested prefabs). When overriding a field on a parent, should it propagate to children?

```
PlayerCharacter (prefab)
├── Body (prefab)
│   ├── Head (mesh)
│   ├── Torso (mesh)
│   └── Arms (mesh)
├── Weapon (prefab)
│   └── Blade (collider)
└── Armor (prefab)
    └── Helmet (visual)
```

**Problem**: If the parent prefab is reused with different settings, do children inherit those settings?

### Prefab Override Strategy

```cpp
struct PrefabOverride {
    std::string component_type;  // "CTransform", "CCharacter", etc.
    std::string field_name;      // "health", "position", etc.
    Variant new_value;
    bool propagate_to_children;  // Cascade override to children
};

struct CPrefabInstance {
    StringId prefab_id;
    std::vector<PrefabOverride> overrides;
    entt::entity parent_prefab;  // If this is a child prefab
};
```

Example:

```ini
# res://prefabs/armor_set.uzprefab

[prefab]
id = armor_set
root_entity = Armor
child_count = 3

[child.0]
id = helmet
prefab_template = armor_helmet

[override.0]
entity = helmet
component = CVisual
field = color_tint
value = #FF0000FF
propagate_to_children = false

[override.1]
entity = Armor  # Root
component = CTransform
field = position
value = 0 2 0
propagate_to_children = true  # All descendants inherit position offset
```

---

## Transform Dirty Flags & Hierarchical Updates

### Problem: Expensive Matrix Recalculation

Every time a parent's transform changes, all children's world-space matrices must be recalculated:

```
Parent moves → Children's world position changes
              → All children's world matrices invalid
              → Must recalculate all children
```

With deep hierarchies (100+ levels), this can cause O(n) work per frame.

### Solution: Dirty Flag Propagation

```cpp
struct CTransform {
    // Local-space (relative to parent)
    Vec3 position;
    Quat rotation;
    Vec3 scale;

    // Cached world-space
    Vec3 world_position;
    Quat world_rotation;
    Vec3 world_scale;

    Mat4x4 local_matrix;   // Local space
    Mat4x4 world_matrix;   // World space

    // Dirty flags
    bool local_dirty = true;   // Local matrix needs rebuild
    bool world_dirty = true;   // World matrix needs rebuild
};

class TransformSystem {
public:
    void on_transform_modified(entt::entity e, CTransform& transform) {
        // Mark local as dirty (will recalculate on next access)
        transform.local_dirty = true;

        // Propagate world_dirty to all children
        propagate_world_dirty(e);
    }

    Mat4x4 get_world_matrix(entt::entity e) {
        auto& transform = registry_.get<CTransform>(e);

        if (transform.world_dirty) {
            // Recalculate
            if (auto parent = get_parent(e)) {
                Mat4x4 parent_mat = get_world_matrix(parent);
                transform.world_matrix = parent_mat * get_local_matrix(e);
            } else {
                transform.world_matrix = get_local_matrix(e);
            }
            transform.world_dirty = false;
        }

        return transform.world_matrix;
    }

private:
    void propagate_world_dirty(entt::entity parent) {
        // Mark all children as world_dirty
        auto children = get_children(parent);
        for (auto child : children) {
            auto& child_transform = registry_.get<CTransform>(child);
            if (!child_transform.world_dirty) {
                child_transform.world_dirty = true;
                propagate_world_dirty(child);  // Recursive
            }
        }
    }
};
```

**Benefit**: Changes propagate only to affected descendants; unmodified branches stay cached.

---

## Thread Safety Considerations

### Current: Single-Threaded (Game Logic Thread)

The EnTT registry is **not thread-safe**. All modifications must happen on the Game Logic thread.

```cpp
// ❌ UNSAFE: Modifying registry from worker thread
std::thread worker([&registry]() {
    registry.emplace<CComponent>(entity);  // Crash!
});

// ✅ SAFE: Queue changes for next frame
void submit_component_from_worker(entt::entity e, const CComponent& comp) {
    work_queue.push([=]() {
        registry.emplace<CComponent>(e, comp);
    });
}

// In game loop:
while (running) {
    work_queue.drain();  // Execute all queued changes
    // ... rest of frame
}
```

### Future: Multi-Threaded ECS (Phase 4)

If multi-threading is needed:
- Use separate registry per thread, or
- Use a thread-safe queue to synchronize changes, or
- Adopt a lock-free concurrent ECS library

---

## Best Practices

### DO

✅ Use registry context for service locator pattern
✅ Subscribe to lifecycle signals for derived data
✅ Version components before game.dll reload
✅ Use templates for repeated entity archetypes
✅ Cache world matrices with dirty flags

### DON'T

❌ Pass GameContext everywhere (use registry.ctx() instead)
❌ Create systems that know about other systems' internals
❌ Iterate the entire registry in a query (use filtered views)
❌ Modify registry from multiple threads without synchronization
❌ Ignore component version changes across hot-reloads

---

## Testing

```cpp
// test/ecs_advanced_test.cpp

TEST(RegistryContext, StoresAndRetrievesService) {
    entt::registry registry;
    int my_service = 42;

    registry.ctx().emplace<int*>(&my_service);
    EXPECT_EQ(*registry.ctx().at<int*>(), 42);
}

TEST(Signals, FiresOnComponentUpdate) {
    entt::registry registry;
    entt::entity e = registry.create();

    int update_count = 0;
    registry.on_update<CTransform>().connect<[&update_count](auto&, auto&) {
        ++update_count;
    }>();

    registry.emplace<CTransform>(e);
    EXPECT_EQ(update_count, 1);

    registry.patch<CTransform>(e, [](auto& t) {
        t.position.x = 5.0f;
    });
    EXPECT_EQ(update_count, 2);
}

TEST(ComponentVersioning, MigratesOldData) {
    entt::registry registry;
    entt::entity e = registry.create();

    // Simulate old v1 data
    CCharacter v1_data{.health = 50, .max_health = 100};
    registry.emplace<CCharacter>(e, v1_data);

    // Apply migration
    auto& comp = registry.get<CCharacter>(e);
    CCharacterMigration::apply(1, comp);

    EXPECT_EQ(comp.shield, 0.0f);  // New v2 field initialized
}

TEST(TransformDirtyFlags, PropagateToChildren) {
    entt::registry registry;
    auto parent = registry.create();
    auto child = registry.create();
    auto grandchild = registry.create();

    registry.emplace<CTransform>(parent, CTransform{});
    registry.emplace<CTransform>(child, CTransform{});
    registry.emplace<CTransform>(grandchild, CTransform{});

    // Set parent-child relationships
    set_parent(registry, child, parent);
    set_parent(registry, grandchild, child);

    // Modify parent
    registry.patch<CTransform>(parent, [](auto& t) {
        t.position.y = 5.0f;
    });

    // Check dirty flags propagated
    EXPECT_TRUE(registry.get<CTransform>(child).world_dirty);
    EXPECT_TRUE(registry.get<CTransform>(grandchild).world_dirty);
}
```

---

## Related Docs

- [Scene System](../specs/scene-system.md) — Entity and node management
- [Event Bus](../specs/event-bus.md) — Reactive patterns with events
- [Gameplay Systems](../specs/gameplay-systems.md) — System scheduler built on ECS
- [Lua Scripting](../specs/lua-scripting.md) — Lua binding to ECS components
