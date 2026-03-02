# ECS & Game Systems — AAA Gap Analysis

> **Author**: ECS Systems Engineer
> **Date**: 2026-03-02
> **Scope**: Critical missing features for a production-grade game engine
> **Documents Reviewed**: scene-system.md, event-bus.md, tags-groups.md, timer-system.md, lua-scripting.md, engine-vision.md, ROADMAP.md, PRD.md, ecs-and-scene.md, cpp-architecture.md, prefab-edit-mode.md, physics-system.md, save-system.md, particle-system.md

---

## Executive Summary

UzEngine's current documentation and roadmap cover the fundamentals of a functional game engine: scene graph, ECS, scripting, physics, audio, rendering, and editor tooling. The architecture is sound — the dual-layer scene graph + ECS design, the C ABI boundary for game.dll, and the AI-first philosophy are all well-conceived.

However, when measured against what AAA and high-quality indie engines provide (Unreal Engine, Unity, Godot 4, Bevy, Flax Engine), there are **14 critical gaps** that would severely limit the engine's ability to support real-world game production. These gaps span entity lifecycle management, spatial data structures, world streaming, data-driven gameplay, and system execution control.

This review is ordered by impact severity: features listed first would cause the most visible failures in production.

---

## Gap 1: World Streaming / Level Streaming

### Why Critical for AAA

Every AAA engine supports loading and unloading regions of the world at runtime. Unreal has World Partition with HLOD, Unity has Addressables + SceneManager.LoadSceneAsync, Godot 4 has ResourceLoader.load_threaded. Without streaming, the entire game world must fit in memory simultaneously. This limits UzEngine to trivially small levels.

The current spec (`scene-system.md`) treats a scene as a monolithic unit: `SceneManager::load` loads everything, `SceneManager::unload` destroys everything. There is no concept of partial loading, additive scenes, or region-based streaming.

### Impact If Absent

- Open-world games impossible (any level > 500 nodes cited in NFR-006)
- Loading screens between every area transition (unacceptable in 2026)
- Memory ceiling hit quickly with detailed environments
- No seamless indoor/outdoor transitions

### Technical Design

#### Data Model

```cpp
// src/engine/scene/streaming.h

struct StreamingCell {
    uint32_t        cell_id;
    AABB            bounds;            // World-space axis-aligned bounding box
    StringId        scene_path;        // res:// path to .uzscene chunk
    StreamState     state;             // UNLOADED, LOADING, LOADED, UNLOADING
    float           load_distance;     // Distance from camera at which to trigger load
    float           unload_distance;   // Distance at which to unload (hysteresis margin)
    uint8_t         priority;          // 0 = critical (player cell), 255 = background
};

enum class StreamState : uint8_t {
    UNLOADED,       // Not in memory
    QUEUED,         // In the async load queue
    LOADING,        // Background thread is parsing + creating entities
    LOADED,         // Fully active, entities in registry
    UNLOADING       // Being torn down (scripts on_stop, entities destroyed)
};

class WorldStreamer {
    std::vector<StreamingCell>      cells_;
    AsyncLoadQueue                  load_queue_;       // Priority queue, background thread
    entt::registry*                 world_;
    Vec3                            camera_position_;

public:
    void register_cell(const StreamingCell& cell);
    void update(Vec3 camera_pos);                      // Called each frame
    void force_load(uint32_t cell_id);                 // Blocking load (teleport)
    void force_unload(uint32_t cell_id);
    bool is_loaded(uint32_t cell_id) const;
    StreamState get_state(uint32_t cell_id) const;
};
```

#### .uzworld Format (Grid Definition)

```ini
[uzworld version=1 name="OpenWorld"]
cell_size = 128.0    ; meters per cell side
grid_x    = 8
grid_z    = 8

[cell x=0 z=0]
scene = "res://world/cell_0_0.uzscene"
load_distance = 256.0
unload_distance = 320.0

[cell x=1 z=0]
scene = "res://world/cell_1_0.uzscene"
load_distance = 256.0
unload_distance = 320.0
```

#### Async Load Pipeline

```
Frame N: WorldStreamer::update(camera_pos)
  |-- For each cell: compute distance to camera
  |-- If distance < load_distance && state == UNLOADED:
  |     enqueue(cell, priority = distance)
  |-- If distance > unload_distance && state == LOADED:
  |     begin_unload(cell)
  |
  |-- Background thread picks highest-priority cell:
  |     1. Parse .uzscene (IO-bound, can overlap with rendering)
  |     2. Create NodeBase tree in staging buffer (no registry access)
  |     3. Signal main thread: "cell ready for integration"
  |
Frame N+K: Main thread integrates staged cell:
  |-- Create entt::entities from staged NodeBase tree
  |-- Run on_start for Lua scripts in the cell
  |-- Set cell state = LOADED
```

#### Cross-Cell Entity References

Entities in one cell may need to reference entities in another (e.g., a trigger in cell A opens a door in cell B). Use `GlobalEntityId = {cell_id, local_node_id}` resolved at load time. Unresolved references hold a pending queue until the target cell loads.

#### Integration Points

- **SceneManager**: Must support multiple active scenes (additive loading). Currently assumes exactly one active scene.
- **Physics (Jolt)**: Bodies must be added/removed from `JPH::PhysicsSystem` incrementally as cells load/unload. Jolt supports this natively via `BodyInterface::AddBody/RemoveBody`.
- **Audio**: Spatial audio sources in unloaded cells must be silenced, not destroyed (allows crossfade on cell boundaries).
- **Lua scripts**: `on_start` called when cell loads; `on_stop` when cell unloads. Scripts in adjacent cells may communicate via EventBus (events from unloaded cells are dropped).

### Complexity: XL (1-2 months)

### Dependencies

- Scene System (additive scene loading)
- Physics System (incremental body management)
- Asset Pipeline (cell-based .uzpak partitioning)

---

## Gap 2: Spatial Partitioning for ECS Queries

### Why Critical for AAA

The current tag query system (`tags-groups.md`) performs O(n) linear scans over all entities: "scan all entities with CTags, filter by bit." With 500 nodes (NFR-006 target), this is fine. With 10,000+ entities (standard for any real game scene), this becomes a frame-time bottleneck. Every gameplay system that asks "what is near me?" pays this cost.

Unreal uses a spatial hash + octree for overlaps and proximity queries. Unity has `Physics.OverlapSphere` backed by PhysX broadphase. Godot uses a BVH. UzEngine's ECS layer has no spatial acceleration structure.

### Impact If Absent

- `ctx.scene:find_by_tag("enemy")` followed by distance filter = O(n) per query per frame
- AI systems that query "nearby enemies" become O(n^2) across all AI agents
- Frustum culling for rendering becomes brute-force O(n) (acceptable for small scenes, catastrophic at scale)
- Explosion radius checks, proximity triggers, and area effects become prohibitively expensive

### Technical Design

#### Loose Octree for Dynamic Entities

```cpp
// src/engine/spatial/loose_octree.h

struct OctreeNode {
    AABB            bounds;           // Tight bounds of this octree cell
    AABB            loose_bounds;     // Expanded bounds (2x cell size) — loose octree
    uint32_t        children[8];      // Child node indices (0 = no child)
    uint32_t        first_entity;     // Head of linked list of entities in this cell
    uint16_t        entity_count;
    uint8_t         depth;
};

struct SpatialEntry {
    entt::entity    entity;
    AABB            bounds;          // Entity's world-space AABB (from CTransform + mesh bounds)
    uint32_t        next;            // Next entity in same octree cell (linked list)
    uint32_t        octree_node;     // Which octree node this entity lives in (for fast removal)
};

class SpatialIndex {
    std::vector<OctreeNode>   nodes_;
    std::vector<SpatialEntry> entries_;
    Pool<uint32_t>            free_entries_;

    static constexpr uint8_t MAX_DEPTH = 8;
    static constexpr uint16_t MAX_ENTITIES_PER_NODE = 32;  // Split threshold

public:
    // Lifecycle
    void init(AABB world_bounds);
    void clear();

    // Entity management
    void insert(entt::entity e, AABB bounds);
    void remove(entt::entity e);
    void update(entt::entity e, AABB new_bounds);   // Re-insert if cell changed

    // Queries (return results via callback to avoid allocation)
    void query_aabb(AABB region, std::function<void(entt::entity)> fn) const;
    void query_sphere(Vec3 center, float radius, std::function<void(entt::entity)> fn) const;
    void query_frustum(const Frustum& frustum, std::function<void(entt::entity)> fn) const;

    // Batch update (call once per frame after transform system)
    void rebuild_dirty(entt::registry& world);
};
```

#### ECS Integration

```cpp
// New component: marks entity as needing spatial tracking
struct CSpatial {
    uint32_t    spatial_entry_index;   // Index into SpatialIndex::entries_
    AABB        cached_bounds;         // Last known bounds (detect movement)
    bool        dirty;                 // Set when CTransform changes
};

// System: update spatial index after transforms
void system_update_spatial(entt::registry& world, SpatialIndex& index) {
    auto view = world.view<CSpatial, const CTransform>();
    for (auto [entity, spatial, transform] : view.each()) {
        AABB new_bounds = compute_world_aabb(transform);
        if (new_bounds != spatial.cached_bounds) {
            index.update(entity, new_bounds);
            spatial.cached_bounds = new_bounds;
        }
    }
}
```

#### Lua API Extension

```lua
-- Spatial queries via ctx.world (augmenting existing physics queries)
local nearby = ctx.world:query_sphere(self.position, 15.0)
for _, entity in ipairs(nearby) do
    if entity:has_tag("enemy") then
        -- ...
    end
end

-- Frustum query for custom culling
local visible = ctx.world:query_frustum(camera_frustum)
```

#### Why Not Just Use Jolt's Broadphase?

Jolt's broadphase only contains physics bodies. Many gameplay entities (pickups, triggers, waypoints, spawn points) have no physics body but still need spatial queries. A dedicated spatial index covers all entities.

### Complexity: L (2-4 weeks)

### Dependencies

- ECS component system (CSpatial)
- Transform system (dirty flag propagation)
- Renderer (frustum culling integration)

---

## Gap 3: Entity Lifecycle Management and Object Pooling

### Why Critical for AAA

The current entity lifecycle is simple: `registry.create()` on spawn, `registry.destroy()` on death. In a real game, entities are created and destroyed frequently (bullets, particles, NPCs, pickups). Each create/destroy involves:

1. EnTT entity allocation (slot reuse, but with memory fragmentation)
2. Component emplacement (multiple allocations if components are large)
3. Scene graph NodeBase creation (linked list manipulation)
4. Lua script `on_start` / `on_stop` (VM allocation, table creation)
5. Physics body creation/destruction (Jolt BodyInterface calls)

Unreal uses ActorPool and FActorPoolingSubsystem. Unity has built-in ObjectPool<T>. Even Godot recommends the "free queue" pattern. Without pooling, a bullet-hell game creating 100 projectiles/second will stutter from allocation pressure.

### Impact If Absent

- Frame hitches during rapid entity spawn/destroy (bullets, particles, VFX)
- GC-like pauses from EnTT internal memory management under churn
- Lua VM stress from creating/destroying per-node tables every frame
- Physics body allocation latency (Jolt allocates shape memory)

### Technical Design

#### Pooled Entity Pattern

```cpp
// src/engine/ecs/entity_pool.h

template<typename... Components>
class EntityPool {
    struct PooledEntity {
        entt::entity    entity;
        bool            active;
    };

    std::vector<PooledEntity> pool_;
    std::vector<uint32_t>     free_list_;   // Indices into pool_ that are inactive
    entt::registry*           world_;
    uint32_t                  high_water_;  // Max simultaneous active entities

public:
    void init(entt::registry& world, uint32_t initial_count) {
        world_ = &world;
        pool_.resize(initial_count);
        for (uint32_t i = 0; i < initial_count; ++i) {
            pool_[i].entity = world.create();
            // Emplace all components with default values
            (world.emplace<Components>(pool_[i].entity, Components{}), ...);
            pool_[i].active = false;
            deactivate_entity(pool_[i].entity);
            free_list_.push_back(i);
        }
    }

    entt::entity acquire() {
        if (free_list_.empty()) {
            grow(pool_.size() / 2 + 1);  // Grow by 50%
        }
        uint32_t idx = free_list_.back();
        free_list_.pop_back();
        pool_[idx].active = true;
        activate_entity(pool_[idx].entity);
        high_water_ = std::max(high_water_, (uint32_t)(pool_.size() - free_list_.size()));
        return pool_[idx].entity;
    }

    void release(entt::entity e) {
        // Reset components to defaults, don't destroy entity
        (world_->get<Components>(e) = Components{}, ...);
        deactivate_entity(e);
        // Return to free list
        // ...
    }

private:
    void activate_entity(entt::entity e) {
        world_->emplace_or_replace<CActive>(e);  // Tag component: marks as active
    }

    void deactivate_entity(entt::entity e) {
        world_->remove<CActive>(e);  // Removes from active queries
    }
};
```

#### CActive Tag Component

```cpp
// Lightweight tag component — no data, just presence/absence
struct CActive {};

// All gameplay systems filter on CActive:
auto view = world.view<CTransform, CMeshRenderer, CActive>();
// Pooled-but-inactive entities are excluded automatically
```

#### Lua Integration

```lua
-- Pool-aware spawn/release in Lua
local bullet = ctx.pool:acquire("Bullet")
bullet.position = spawn_pos
bullet.velocity = direction * speed

-- On impact:
ctx.pool:release(bullet)
-- Entity is deactivated, not destroyed. No allocation next time.
```

#### Physics Body Pooling

Jolt bodies should NOT be destroyed — instead, move them to `JPH::BodyInterface::SetMotionType(BodyID, EMotionType::Static)` and teleport to a "graveyard" position `(0, -10000, 0)`. On re-acquire, reset position and motion type. This avoids the expensive `AddBody` / `RemoveBody` cycle.

### Complexity: M (1-2 weeks)

### Dependencies

- ECS component system (CActive tag)
- Lua binding (ctx.pool API)
- Physics system (body recycling)

---

## Gap 4: Reactive Systems / Change Detection

### Why Critical for AAA

The current ECS design runs all systems every frame over all entities with matching components. This is the brute-force approach. In practice, most entities are idle most frames — a static mesh's transform doesn't change, a dormant NPC's AI doesn't need ticking.

Unreal's Gameplay Ability System uses "dirty" attributes to avoid re-evaluating unchanged state. Unity's ECS (DOTS) has chunk-level change filtering via `SystemState` and `DidChange<T>`. Bevy has `Changed<T>` and `Added<T>` query filters. EnTT natively supports `on_construct`, `on_update`, `on_destroy` signals — but UzEngine's specs never mention using them.

### Impact If Absent

- Transform hierarchy recalculation runs for all 10,000 nodes even when only 5 moved
- Lua `on_update` called for stationary objects doing nothing (pure overhead)
- Physics body sync iterates all rigid bodies even if none moved via gameplay code
- EventBus cannot implement property-changed events without manual dirty flags everywhere

### Technical Design

#### EnTT Signals (Already Available)

```cpp
// EnTT provides per-component signals — UzEngine should use them

// At scene init:
registry.on_construct<CTransform>().connect<&on_transform_added>();
registry.on_update<CTransform>().connect<&on_transform_changed>();
registry.on_destroy<CTransform>().connect<&on_transform_removed>();
```

#### CDirty Component Pattern

```cpp
// src/engine/ecs/components.h

// Lightweight tag: "this entity's transform changed this frame"
struct CDirtyTransform {
    uint64_t frame_changed;   // Frame index when last dirtied
};

// System: only update world matrices for dirty transforms
void system_update_world_transforms(entt::registry& world, uint64_t frame) {
    // Only process entities whose transform was modified this frame
    auto view = world.view<CTransform, CDirtyTransform>();
    for (auto [entity, tf, dirty] : view.each()) {
        if (dirty.frame_changed != frame) continue;

        tf.world_matrix = compute_world_matrix(world, entity, tf);

        // Propagate dirty to children
        propagate_dirty_to_children(world, entity, frame);
    }
}

// When Lua writes node.position:
void set_node_position(entt::registry& world, entt::entity e, Vec3 pos) {
    world.get<CTransform>(e).position = pos;
    world.emplace_or_replace<CDirtyTransform>(e, CDirtyTransform{ current_frame() });
}
```

#### Reactive Event Emission

```cpp
// When CTransform changes, automatically emit EventBus event
void on_transform_changed(entt::registry& reg, entt::entity entity) {
    auto& tf = reg.get<CTransform>(entity);
    EventBus<TransformChanged>::emit({ entity, tf.position, tf.rotation });
}
```

#### Sleep System for Lua Scripts

```cpp
// Scripts that haven't done anything for N frames can be put to sleep
struct CScriptSleep {
    uint32_t idle_frames;       // Frames since last meaningful action
    uint32_t wake_threshold;    // Frames before sleeping (default: 60 = 1 second)
    bool     sleeping;
};

// system_update_scripts skips sleeping scripts
void system_update_scripts(entt::registry& world, LuaVM& vm, float dt) {
    auto view = world.view<CScriptRef, CScriptSleep>();
    for (auto [entity, script, sleep] : view.each()) {
        if (sleep.sleeping) continue;  // Skip — no on_update call

        // ... run on_update

        // If script didn't modify anything, increment idle counter
        if (!script_modified_state) {
            sleep.idle_frames++;
            if (sleep.idle_frames >= sleep.wake_threshold) {
                sleep.sleeping = true;
            }
        } else {
            sleep.idle_frames = 0;
        }
    }
}

// Wake a sleeping script (called when event/timer targets it)
void wake_script(entt::registry& world, entt::entity e) {
    auto& sleep = world.get<CScriptSleep>(e);
    sleep.sleeping = false;
    sleep.idle_frames = 0;
}
```

### Complexity: M (1-2 weeks)

### Dependencies

- ECS component system
- Transform system (dirty propagation)
- Lua scripting (sleep/wake integration)

---

## Gap 5: System Execution Order and Tickable Priorities

### Why Critical for AAA

The current frame loop (`ecs-and-scene.md` section 6.3) has a hardcoded execution order: Input -> Physics -> Events -> Scripts -> Render. There is no mechanism for:

1. Ordering gameplay systems relative to each other
2. Running some systems at different frequencies (AI every 200ms, animation every frame)
3. Declaring dependencies between systems
4. Enabling/disabling systems at runtime

Unreal has `TickGroups` (PrePhysics, DuringPhysics, PostPhysics, PostUpdateWork) and per-component `TickInterval`. Unity DOTS has `[UpdateBefore]` / `[UpdateAfter]` attributes and system groups. Bevy has stages, system sets, and run conditions. Without execution control, adding a new gameplay system requires editing the hardcoded loop in `game.cpp`, which does not scale.

### Impact If Absent

- Adding new game systems requires modifying the core game loop (fragile, error-prone)
- AI running every frame wastes CPU (most AI only needs 5-10 Hz updates)
- Physics callbacks and gameplay scripts may execute in unpredictable relative order
- No way to pause specific systems (e.g., pause gameplay but keep UI responsive)

### Technical Design

#### System Registry with Priorities

```cpp
// src/engine/ecs/system_scheduler.h

enum class TickGroup : uint8_t {
    PRE_PHYSICS     = 0,    // Input processing, scene commands
    PHYSICS         = 1,    // Fixed-step physics (Jolt)
    POST_PHYSICS    = 2,    // Physics event processing, collision responses
    GAME_LOGIC      = 3,    // Lua scripts, game systems, timers
    LATE_UPDATE      = 4,    // Camera follow, IK solve, audio positioning
    PRE_RENDER      = 5,    // Culling, LOD selection, draw call batching
};

struct SystemDesc {
    StringId            name;
    TickGroup           group;
    int16_t             priority;        // Within group: lower = runs first
    float               tick_interval;   // 0 = every frame, 0.2 = every 200ms
    bool                enabled;

    using UpdateFn = void(*)(entt::registry& world, float dt);
    UpdateFn            update_fn;

    // Optional: run conditions
    using ConditionFn = bool(*)(const entt::registry& world);
    ConditionFn         run_condition = nullptr;  // null = always run
};

class SystemScheduler {
    std::vector<SystemDesc>   systems_;
    bool                      sorted_ = false;

    // Per-system accumulators for tick_interval
    std::vector<float>        accumulators_;

public:
    void register_system(SystemDesc desc);
    void unregister_system(StringId name);
    void set_enabled(StringId name, bool enabled);
    void set_tick_interval(StringId name, float interval);

    // Called once per frame with current delta time
    void execute_all(entt::registry& world, float dt);

    // Execute only a specific tick group (useful for fixed-step physics)
    void execute_group(TickGroup group, entt::registry& world, float dt);
};
```

#### Registration Pattern in game.dll

```cpp
// game.cpp — on_start
void uz_game_start(GameContext* ctx) {
    auto& sched = ctx->system_scheduler;

    sched.register_system({
        .name = "update_ai"_sid,
        .group = TickGroup::GAME_LOGIC,
        .priority = 10,
        .tick_interval = 0.2f,          // AI ticks at 5 Hz
        .enabled = true,
        .update_fn = &system_update_ai,
    });

    sched.register_system({
        .name = "update_camera"_sid,
        .group = TickGroup::LATE_UPDATE,
        .priority = 0,
        .tick_interval = 0.0f,          // Every frame
        .enabled = true,
        .update_fn = &system_update_camera,
    });

    sched.register_system({
        .name = "update_animation"_sid,
        .group = TickGroup::LATE_UPDATE,
        .priority = -10,                // Before camera (camera reads bone positions)
        .tick_interval = 0.0f,
        .enabled = true,
        .update_fn = &system_update_animation,
    });
}
```

#### Lua System Registration

```lua
-- Lua scripts can register lightweight systems
ctx.systems:register("enemy_wave_spawner", {
    group     = "GAME_LOGIC",
    priority  = 50,
    interval  = 1.0,    -- once per second
    update    = function(dt)
        check_wave_conditions()
    end
})
```

### Complexity: M (1-2 weeks)

### Dependencies

- Game loop refactoring (replace hardcoded sequence with scheduler dispatch)
- game.dll ABI (SystemScheduler pointer in GameContext)

---

## Gap 6: Component Serialization and Schema Evolution

### Why Critical for AAA

The save system (`save-system.md`) serializes game state as msgpack. The .uzscene format serializes node properties as INI key-value pairs. Neither spec addresses what happens when a component's structure changes between engine versions:

- Field added: Old saves missing the new field
- Field removed: Old saves have an orphan field
- Field type changed: Deserialization crashes or produces garbage
- Field renamed: Data silently lost

Unreal uses UObject reflection + property versioning. Unity uses SerializedObject with `[FormerlySerializedAs]`. Protobuf and FlatBuffers solve this with field numbering. Without schema evolution, every engine update risks breaking all existing save files and scene files.

### Impact If Absent

- Engine updates break existing .uzscene files (fields silently dropped)
- Save files from older versions crash or corrupt on load
- Prefab overrides break when the prefab's schema changes
- AI-generated scenes from older prompts become incompatible

### Technical Design

#### Component Schema Registry

```cpp
// src/engine/ecs/schema.h

struct FieldDesc {
    StringId    name;
    uint16_t    field_id;        // Stable numeric ID (like protobuf field numbers)
    FieldType   type;            // FLOAT, VEC3, STRING, BOOL, INT32, etc.
    uint32_t    offset;          // offsetof() in component struct
    uint32_t    size;            // sizeof() of field
    bool        has_default;
    uint8_t     default_value[16]; // Inline default (up to 16 bytes)
};

struct ComponentSchema {
    StringId                name;           // "CTransform"
    uint16_t                schema_version; // Bump on breaking change
    std::vector<FieldDesc>  fields;

    // Migration functions for version upgrades
    using MigrateFn = void(*)(void* old_data, uint16_t old_version, void* new_data);
    std::vector<MigrateFn>  migrations;     // [0] = v1->v2, [1] = v2->v3, etc.
};

class SchemaRegistry {
    std::unordered_map<StringId, ComponentSchema> schemas_;

public:
    void register_component(const ComponentSchema& schema);

    // Serialize component using field IDs (not field names or offsets)
    void serialize(StringId component_name, const void* data, SaveWriter& writer);

    // Deserialize with version migration
    void deserialize(StringId component_name, void* data, SaveReader& reader);

    // Generate .uzscene property list from component
    void to_ini(StringId component_name, const void* data, INIWriter& writer);
    void from_ini(StringId component_name, void* data, const INISection& section);
};
```

#### Registration Macro

```cpp
// Usage in components.h
UZ_COMPONENT(CTransform, 3 /* schema version */)
    UZ_FIELD(1, position, Vec3, Vec3(0,0,0))
    UZ_FIELD(2, rotation, Quat, Quat::identity())
    UZ_FIELD(3, scale,    Vec3, Vec3(1,1,1))
    // Field 4 was removed in v3 (old: euler_hint)
    UZ_FIELD(5, world_matrix, Mat4, Mat4::identity())  // Added in v2
UZ_COMPONENT_END()

// Migration from v2 to v3: remove euler_hint
UZ_MIGRATE(CTransform, 2, 3, [](void* old_data, void* new_data) {
    // euler_hint field (id=4) is simply dropped; no action needed
    // All other fields copied by field_id matching
});
```

#### .uzscene Backward Compatibility

```ini
; Old format (v1): position stored as 3 floats
[node id=1 type=Node3D name="Root" parent=0]
position = 0, 0, 0

; New format (v2): position stored the same way, but world_matrix is now computed
; Schema evolution handles missing world_matrix field by using the default (identity)
```

Unknown fields in .uzscene are preserved in a `custom_properties` map on the NodeBase, ensuring that loading a scene from a newer engine version in an older version does not lose data.

### Complexity: L (2-4 weeks)

### Dependencies

- ECS component system (reflection macros)
- Save system (versioned serialization)
- Scene loader (.uzscene version handling)

---

## Gap 7: Snapshot / Rollback for Deterministic Netcode

### Why Critical for AAA

The PRD explicitly lists networking as "out of scope for Phase 1-4" (section 7.1), but notes it is "Planned for Phase 5+." However, the architectural foundations for rollback netcode must be designed NOW, because they require:

1. Deterministic simulation (fixed-step physics, deterministic Lua execution order)
2. Fast full-state snapshot of the ECS registry
3. Fast rollback (restore snapshot, re-simulate N frames)

If these are not built into the ECS architecture from the start, retrofitting is prohibitively expensive. Unreal's Gameplay Prediction system, GDC talks on GGPO-style rollback, and Unity's Netcode for Entities all demonstrate that netcode architecture must be baked in early.

### Impact If Absent

- Phase 5+ networking becomes a rewrite instead of an extension
- Fighting games, racing games, and competitive shooters impossible without rollback
- Replay system (useful even in single-player) requires the same snapshot capability
- Debug "rewind time" feature cannot exist

### Technical Design

#### ECS Snapshot System

```cpp
// src/engine/ecs/snapshot.h

struct WorldSnapshot {
    uint64_t                frame_index;
    float                   time_elapsed;
    Arena                   data_arena;       // All component data stored here

    // Per-component type: entity list + raw data blob
    struct ComponentBlob {
        StringId            component_name;
        uint32_t            entity_count;
        entt::entity*       entities;         // Arena-allocated
        void*               data;             // Arena-allocated, packed
        uint32_t            stride;           // sizeof(Component)
    };

    std::vector<ComponentBlob> blobs;
};

class SnapshotManager {
    static constexpr uint32_t MAX_SNAPSHOTS = 120;  // 2 seconds at 60 FPS
    RingBuffer<WorldSnapshot> history_;

public:
    // Capture current world state (should be < 1ms for 1000 entities)
    void capture(const entt::registry& world, uint64_t frame, float time);

    // Restore world to a previous snapshot
    void rollback(entt::registry& world, uint64_t target_frame);

    // Get snapshot for frame (for diff-based networking)
    const WorldSnapshot* get(uint64_t frame) const;

    // Delta between two snapshots (for bandwidth optimization)
    void compute_delta(uint64_t from_frame, uint64_t to_frame, DeltaBuffer& out);
};
```

#### Snapshot-Friendly Component Marker

```cpp
// Components marked as "netcode-relevant" are included in snapshots
// Components NOT marked (e.g., CMeshRenderer — purely visual) are excluded

template<typename T>
struct NetSync {};  // Tag trait

// In components.h:
struct CTransform : NetSync<CTransform> { ... };
struct CRigidBody : NetSync<CRigidBody> { ... };
struct CHealth    : NetSync<CHealth>    { ... };

// CMeshRenderer is NOT NetSync — visual only, not snapshotted
struct CMeshRenderer { ... };
```

#### Determinism Requirements

For rollback to work, the simulation must be deterministic:

1. **Fixed-step physics**: Already planned (60 Hz accumulator). Good.
2. **Deterministic Lua execution**: Script `on_update` must iterate entities in a stable order. Currently undocumented — must specify that `CScriptRef` iteration follows entity creation order.
3. **No floating-point non-determinism**: Use `_controlfp` to force consistent FP rounding on x64. Document this requirement.
4. **Timer drain order**: Already specified as deterministic (vector iteration order). Good.

### Complexity: L (2-4 weeks) for snapshot; XL for full rollback netcode

### Dependencies

- ECS component system (NetSync trait, schema registry)
- Physics system (deterministic step)
- Timer system (deterministic drain)

---

## Gap 8: Gameplay Ability System (GAS Equivalent)

### Why Critical for AAA

Unreal's Gameplay Ability System (GAS) is the de facto standard for RPG/action game mechanics: abilities, effects, attributes, cooldowns, stacking, prediction. Unity has third-party equivalents (Able, Gameplay Ingredients). Without a structured ability/effect system, every game reimplements the same patterns: health/mana attributes, damage/buff effects with duration, cooldown management, ability activation conditions.

UzEngine's current plan leaves all of this to raw Lua scripts. This works for prototyping but produces unmaintainable spaghetti in production.

### Impact If Absent

- Every RPG/action game must reinvent attribute system, effect stacking, cooldowns
- No standard way to query "can this ability activate?" (resource checks, cooldowns, tags)
- Buff/debuff systems are ad-hoc and inconsistent
- AI-generated gameplay code has no framework to target — each game is unique

### Technical Design

#### Attribute Set Component

```cpp
// src/engine/gameplay/attributes.h

struct AttributeDef {
    StringId    name;           // "Health", "Mana", "MovementSpeed"
    float       base_value;     // Default value
    float       min_value;      // Clamp minimum
    float       max_value;      // Clamp maximum
};

struct CAttributeSet {
    static constexpr uint32_t MAX_ATTRIBUTES = 32;

    struct Attribute {
        StringId    name;
        float       base_value;       // Unmodified value
        float       current_value;    // base + all modifier stacks
        float       min_value;
        float       max_value;
    };

    Attribute   attributes[MAX_ATTRIBUTES];
    uint8_t     count;

    float get(StringId name) const;
    void  set_base(StringId name, float value);
    void  recalculate(StringId name);  // Recompute current from base + modifiers
};
```

#### Gameplay Effect

```lua
-- res://game/effects/fire_dot.lua (data-driven effect definition)
return {
    name = "FireDOT",
    duration = 5.0,           -- seconds (0 = instant, -1 = infinite)
    period = 1.0,             -- tick every 1 second
    stacking = "refresh",     -- "refresh" | "stack" | "none"
    max_stacks = 3,

    modifiers = {
        { attribute = "Health", op = "add", value = -10 },      -- -10 HP per tick
        { attribute = "MovementSpeed", op = "multiply", value = 0.8 },  -- 20% slow
    },

    tags_granted = { "burning" },
    tags_required = { "flammable" },      -- Target must have this tag
    tags_blocked = { "fire_immune" },     -- Cancel if target has this tag
}
```

#### Ability Definition

```lua
-- res://game/abilities/fireball.lua
return {
    name = "Fireball",
    cooldown = 3.0,
    cost = { attribute = "Mana", value = 25 },

    activation_tags_required = { "can_cast" },
    activation_tags_blocked  = { "silenced", "stunned" },
    cancel_tags = { "stunned" },

    on_activate = function(ctx, caster, target_pos)
        local projectile = ctx.pool:acquire("FireballProjectile")
        projectile.position = caster.position + Vec3(0, 1.5, 0)
        projectile.velocity = (target_pos - caster.position):normalized() * 20.0
    end,

    on_hit = function(ctx, caster, target)
        ctx.effects:apply(target, "res://game/effects/fire_dot.lua", {
            instigator = caster,
            level = caster:get_attribute("FirePower"),
        })
    end,
}
```

#### C++ Runtime

```cpp
// src/engine/gameplay/ability_system.h

struct CAbilitySystem {
    struct ActiveAbility {
        StringId    ability_id;
        float       cooldown_remaining;
        bool        is_active;
    };

    struct ActiveEffect {
        StringId    effect_id;
        float       duration_remaining;
        float       period_timer;
        uint8_t     stacks;
        int         lua_ref;          // Lua effect table reference
    };

    ActiveAbility   abilities[16];
    uint8_t         ability_count;

    ActiveEffect    effects[32];
    uint8_t         effect_count;
};

// System function
void system_update_abilities(entt::registry& world, float dt);
void system_update_effects(entt::registry& world, float dt, LuaVM& vm);
```

### Complexity: L (2-4 weeks)

### Dependencies

- Tags system (activation/block conditions use tags)
- Timer system (cooldowns, periodic effects)
- EventBus (ability activated/cancelled events)
- Lua scripting (data-driven definitions)

---

## Gap 9: Finite State Machines / Behavior Trees (Native)

### Why Critical for AAA

The animation system spec references a "state machine" for animation blending, but there is no general-purpose FSM or Behavior Tree framework for gameplay AI. Every AAA engine provides this:

- Unreal: Behavior Tree + Blackboard + EQS (Environment Query System)
- Unity: Animator Controller (FSM) + third-party BT (NodeCanvas, BehaviorDesigner)
- Godot: AnimationTree FSM + GDScript BT libraries

Currently, NPC AI in UzEngine would be written as raw Lua `if/elseif` chains in `on_update`. This does not scale past trivial behavior.

### Impact If Absent

- Complex NPC behavior (patrol -> investigate -> chase -> attack -> flee) becomes unmanageable
- No visual debugging of AI state (which state is the NPC in? why did it transition?)
- Each developer reinvents state management ad-hoc
- AI-generated behavior code has no structure to follow

### Technical Design

#### FSM Definition (.uzfsm)

```ini
[uzfsm version=1 name="EnemyAI"]

[state name="Idle"]
script = "res://game/ai/states/idle.lua"

[state name="Patrol"]
script = "res://game/ai/states/patrol.lua"

[state name="Chase"]
script = "res://game/ai/states/chase.lua"

[state name="Attack"]
script = "res://game/ai/states/attack.lua"

[transition from="Idle" to="Patrol" condition="has_patrol_points"]
[transition from="Patrol" to="Chase" condition="enemy_spotted"]
[transition from="Chase" to="Attack" condition="in_attack_range"]
[transition from="Chase" to="Patrol" condition="lost_sight"]
[transition from="Attack" to="Chase" condition="out_of_range"]
```

#### Behavior Tree Definition (.uzbt)

```ini
[uzbt version=1 name="EnemyBT"]

[selector id=1 name="Root"]
children = 2, 5

[sequence id=2 name="CombatBranch"]
children = 3, 4

[condition id=3 name="HasTarget"]
script = "res://game/ai/conditions/has_target.lua"

[action id=4 name="AttackTarget"]
script = "res://game/ai/actions/attack.lua"

[sequence id=5 name="PatrolBranch"]
children = 6, 7

[action id=6 name="FindNextWaypoint"]
script = "res://game/ai/actions/find_waypoint.lua"

[action id=7 name="MoveToWaypoint"]
script = "res://game/ai/actions/move_to.lua"
```

#### C++ Runtime

```cpp
// src/engine/ai/behavior_tree.h

enum class BTNodeType : uint8_t {
    SELECTOR, SEQUENCE, PARALLEL,
    CONDITION, ACTION, DECORATOR,
};

enum class BTStatus : uint8_t {
    RUNNING, SUCCESS, FAILURE,
};

struct BTNode {
    uint32_t        id;
    BTNodeType      type;
    StringId        name;
    StringId        script_id;        // Lua script for conditions/actions
    uint32_t        children[8];      // Max 8 children per composite
    uint8_t         child_count;
};

struct CBehaviorTree {
    StringId        tree_id;           // res:// path to .uzbt
    uint32_t        current_node;      // Node being executed
    BTStatus        last_status;
};

// Blackboard: per-entity key-value store for AI data sharing
struct CBlackboard {
    struct Entry {
        StringId    key;
        Variant     value;     // float, Vec3, entity handle, bool, string
    };
    Entry           entries[32];
    uint8_t         count;
};
```

#### Lua State Script Interface

```lua
-- res://game/ai/states/chase.lua
local M = {}

function M:on_enter(ctx, blackboard)
    self.target = blackboard:get("target")
    ctx:log("Entering Chase state")
end

function M:on_update(ctx, dt, blackboard)
    if not self.target or not self.target:is_valid() then
        return "Patrol"  -- transition to Patrol
    end

    local dir = (self.target.position - self.position):normalized()
    self.position = self.position + dir * self.chase_speed * dt

    local dist = (self.target.position - self.position):length()
    if dist < self.attack_range then
        return "Attack"  -- transition to Attack
    end

    return nil  -- stay in Chase
end

function M:on_exit(ctx, blackboard)
    ctx:log("Leaving Chase state")
end

return M
```

### Complexity: L (2-4 weeks)

### Dependencies

- Lua scripting (state/action scripts)
- Tags system (condition evaluation)
- NavMesh (movement actions)
- Timer system (behavior tick intervals)

---

## Gap 10: Prefab Override System (Deep Nesting)

### Why Critical for AAA

The `prefab-edit-mode.md` spec describes opening a prefab in isolation for editing. The `ecs-and-scene.md` spec shows a basic override syntax. But critical features are missing:

1. **Nested prefabs**: A prefab containing instances of other prefabs
2. **Override inheritance**: When a parent prefab changes a property, instances inherit unless they have their own override
3. **Override visualization**: Inspector must show which properties are overridden (blue highlight in Unity)
4. **Override revert**: Right-click to revert a property to the prefab default
5. **Added/removed nodes**: Instance can add nodes not in the prefab, or hide prefab nodes

Unreal handles this with `ChildActorComponent` and property override chains. Unity's prefab system supports unlimited nesting depth with per-property overrides. Godot uses scene inheritance. UzEngine's current spec only handles flat property patches with no nesting.

### Impact If Absent

- Complex prefab hierarchies (character = body + weapon + armor, each a prefab) cannot be composed
- Changing a base prefab does not propagate to instances (or propagates destructively)
- Level designers cannot customize prefab instances without breaking the prefab link
- AI-generated prefab variants have no structured way to express "same as base, but with these changes"

### Technical Design

#### Override Data Model

```cpp
// src/engine/scene/prefab_override.h

struct PropertyOverride {
    StringId    node_path;     // Relative path within prefab: "Body/Helmet"
    StringId    property_name; // "material", "position.x", "cast_shadow"
    Variant     value;         // Overridden value
};

struct NodeAddition {
    uint32_t    parent_path_hash;   // Hash of relative path to parent in prefab
    NodeBase    added_node;         // Full node definition
};

struct NodeSuppression {
    StringId    node_path;     // Relative path of node to hide
};

struct PrefabInstance {
    StringId                        source_path;        // res:// path to .uzscene prefab
    std::vector<PropertyOverride>   property_overrides;
    std::vector<NodeAddition>       added_nodes;
    std::vector<NodeSuppression>    suppressed_nodes;
};
```

#### .uzscene Format Extension

```ini
[node id=20 type=Node3D name="EliteGoblin" parent=1]
prefab = "res://prefabs/goblin.uzscene"

; Property overrides (only store differences from prefab)
[override node="EliteGoblin/Body" property="material"]
value = "res://materials/elite_skin.uzmat"

[override node="EliteGoblin/Body" property="scale"]
value = [1.5, 1.5, 1.5]

; Added node (not in original prefab)
[added_node parent="EliteGoblin" id=21 type=PointLight name="AuraGlow"]
color = [1.0, 0.2, 0.2]
intensity = 3.0
radius = 5.0

; Suppressed node (exists in prefab but hidden in this instance)
[suppressed node="EliteGoblin/WeakSpot"]
```

#### Nested Prefab Resolution

```
Instantiation order (depth-first):
1. Load goblin.uzscene
   1a. If goblin.uzscene itself contains [node_instance] for weapon.uzscene:
       - Load weapon.uzscene (recursion)
       - Apply weapon overrides from goblin.uzscene
   1b. Build full goblin node tree
2. Apply EliteGoblin overrides on top
3. Add added_nodes
4. Suppress suppressed_nodes
5. Sync to ECS

Max nesting depth: 8 (assert if exceeded — prevents circular references)
```

### Complexity: L (2-4 weeks)

### Dependencies

- Scene system (prefab loading)
- Prefab edit mode (override visualization)
- Editor panels (Inspector blue highlights)

---

## Gap 11: Entity Templates / Data-Driven Archetype Definitions

### Why Critical for AAA

Currently, creating an entity requires programmatic component emplacement in C++ or declaring every property in .uzscene. There is no concept of "this is an Enemy archetype with these default components" separate from a scene instance.

Unreal has Blueprint Classes (data-driven actor templates). Unity has ScriptableObject + prefab variants. Bevy has Bundles. Without entity templates, every new enemy type requires either a new .uzscene prefab (heavy) or manual component setup in code (fragile).

### Impact If Absent

- No lightweight way to define "an entity of this type has these components"
- AI cannot generate entity types without creating full .uzscene files
- No entity type palette for the editor ("drag an Enemy into the scene")
- Component composition is implicit in code, not declared in data

### Technical Design

#### .uztemplate Format

```ini
[uztemplate version=1 name="Enemy" category="NPCs"]
description = "Base enemy with health, AI, and collision"

; Components and their defaults
[component CTransform]
; Uses all defaults (position=0, rotation=identity, scale=1)

[component CHealth]
max_health = 100
current_health = 100
invincible = false

[component CRigidBody]
mass = 80.0
friction = 0.6

[component CColliderCapsule]
radius = 0.4
half_height = 0.9

[component CBehaviorTree]
tree = "res://game/ai/enemy_default.uzbt"

[component CTags]
tags = ["enemy", "damageable", "npc"]

[component CAttributeSet]
attributes = { "Health": 100, "MovementSpeed": 4.0, "AttackDamage": 15 }
```

#### C++ Runtime

```cpp
// src/engine/ecs/entity_template.h

struct EntityTemplate {
    StringId                        name;
    StringId                        category;
    std::vector<ComponentTemplate>  components;  // Type + default data
};

struct ComponentTemplate {
    StringId    component_name;
    void*       default_data;       // Heap-allocated default component data
    uint32_t    data_size;
};

class TemplateRegistry {
    std::unordered_map<StringId, EntityTemplate> templates_;

public:
    void load_from_directory(const char* path);  // Scan res://templates/

    // Instantiate: create entity with all template components
    entt::entity instantiate(entt::registry& world, StringId template_name);

    // Instantiate with overrides
    entt::entity instantiate(entt::registry& world, StringId template_name,
                            const std::vector<PropertyOverride>& overrides);
};
```

#### Usage in .uzscene

```ini
; Instead of listing every component property:
[node id=10 type=Node3D name="Goblin1" parent=1]
template = "res://templates/enemy.uztemplate"
position = [10, 0, 5]
; Only override what differs from template
CHealth.max_health = 50    ; Weaker goblin
```

#### Lua API

```lua
-- Spawn from template
local enemy = ctx.templates:instantiate("Enemy", {
    position = Vec3(10, 0, 5),
    overrides = {
        ["CHealth.max_health"] = 50,
        ["CBehaviorTree.tree"] = "res://game/ai/goblin.uzbt",
    }
})
```

### Complexity: M (1-2 weeks)

### Dependencies

- Schema registry (component reflection)
- Scene loader (template reference resolution)
- Editor panels (template palette in Asset Browser)

---

## Gap 12: C++ Component Hot-Reload

### Why Critical for AAA

The current hot-reload story covers:
- Lua scripts: ~60ms (excellent)
- game.dll C++: ~2.5s (acceptable)
- Shaders: ~50ms (excellent)

But there is no mechanism for hot-reloading changes to C++ component structs themselves. If a developer adds a field to `CTransform`, they must restart the engine entirely — the ECS registry's internal memory layout is invalidated.

Unreal handles this with `UObject` property reflection and `LiveCoding`. Unity's DOTS handles component changes via chunk migration. Without this, C++ component iteration is painfully slow during development.

### Impact If Absent

- Adding/removing a component field requires full engine restart (loses all runtime state)
- Rapid C++ iteration impossible for component data (30-60s full rebuild + restart)
- Discourages developers from iterating on component layouts
- game.dll hot-reload preserves state via msgpack but cannot handle changed struct layouts

### Technical Design

#### Reflection-Based Component Migration

```cpp
// During game.dll hot-reload:
// 1. Old game.dll calls on_save_state — serializes all components via SchemaRegistry
// 2. Old game.dll unloaded
// 3. New game.dll loaded (may have changed component structs)
// 4. New game.dll registers new component schemas
// 5. on_load_state deserializes into new layout using field-ID matching

// Migration happens automatically if:
// - New fields have defaults in the schema
// - Removed fields are simply dropped
// - Renamed fields use UZ_FIELD_ALIAS

// Example: CHealth v1 has {max_health, current_health}
//          CHealth v2 adds {shield, regen_rate} with defaults
// Old save data: {max_health=100, current_health=75}
// New struct:    {max_health=100, current_health=75, shield=0, regen_rate=0}
// Migration: field-ID 1 and 2 match; fields 3 and 4 get defaults. No data loss.
```

This depends entirely on Gap 6 (Schema Evolution). If the schema registry is implemented, component hot-reload becomes a natural extension of the save/load pipeline.

### Complexity: M (1-2 weeks, assuming Schema Registry exists)

### Dependencies

- Schema Registry (Gap 6)
- game.dll hot-reload (existing save_state / load_state)
- ECS component system (reflection)

---

## Gap 13: Dependency Injection / Service Locator for ECS

### Why Critical for AAA

Current system functions take `entt::registry& world` and `float dt`. But real game systems need access to many services: physics world, audio engine, navmesh, event bus, timer manager, asset loader, input state. The current approach is to pass everything through `GameContext*` — a God Object.

Unreal uses `UWorld` subsystems. Unity uses dependency injection via `[Inject]` or `SystemBase.World.GetOrCreateSystem<T>()`. Bevy uses `Res<T>` and `ResMut<T>` resource types. Without structured dependency management, system functions accumulate parameters and `GameContext` grows unboundedly.

### Impact If Absent

- `GameContext` becomes a monolithic struct with 50+ pointers (already at ~12 pointers)
- System functions have implicit dependencies (hard to test, hard to parallelize)
- Adding a new subsystem requires modifying GameContext (C ABI change!)
- Unit testing a system requires mocking the entire GameContext

### Technical Design

#### Resource Registry (EnTT Native)

EnTT already supports this via `ctx()` (context variables). UzEngine should use it:

```cpp
// Register subsystems as EnTT context resources
registry.ctx().emplace<PhysicsWorld>(config);
registry.ctx().emplace<AudioEngine>(audio_config);
registry.ctx().emplace<NavMeshManager>(navmesh);
registry.ctx().emplace<EventBusManager>();
registry.ctx().emplace<TimerManager>();

// System functions access resources via registry.ctx()
void system_update_ai(entt::registry& world, float dt) {
    auto& physics = world.ctx().get<PhysicsWorld>();
    auto& navmesh = world.ctx().get<NavMeshManager>();
    auto& events  = world.ctx().get<EventBusManager>();

    auto view = world.view<CBehaviorTree, CTransform, CNavAgent>();
    for (auto [entity, bt, tf, nav] : view.each()) {
        // Use physics for raycasts, navmesh for pathfinding, events for signals
    }
}
```

#### Benefits

- `GameContext` stays lean (only C ABI fields: api_version, scene, world, input, delta_time)
- Systems declare dependencies explicitly by accessing `ctx<T>()`
- Testing: create a minimal registry with only the needed ctx resources
- New subsystems don't require C ABI changes

#### Migration Path

Move non-ABI fields out of `GameContext` into `registry.ctx()`:

```cpp
// Before (current):
struct GameContext {
    uint32_t api_version;
    Scene* scene;
    entt::registry* world;
    const InputState* input;
    float delta_time;
    // ... 8 more pointers that keep growing
};

// After:
struct GameContext {
    uint32_t api_version;
    Scene* scene;
    entt::registry* world;      // All subsystems live in world->ctx()
    const InputState* input;
    float delta_time;
    double time_elapsed;
    uint64_t frame_index;
    void (*log_fn)(const char*);
    uint8_t _reserved[128];     // Never need to grow GameContext again
};
```

### Complexity: M (1-2 weeks)

### Dependencies

- ECS refactoring (move subsystem pointers to registry.ctx())
- game.dll on_start (register resources)
- All existing systems (update to use registry.ctx())

---

## Gap 14: Hierarchical Transform Optimization (Dirty Flag Propagation + Parallel Update)

### Why Critical for AAA

The scene system has a hierarchical transform model: each node has local position/rotation/scale, and the world matrix is computed by multiplying with the parent's world matrix. The current design (`ecs-and-scene.md`) shows a simple DFS traversal of the entire tree every frame via `system_update_transforms()`.

This is O(n) every frame regardless of how many nodes moved. With 10,000+ nodes and deep hierarchies (character rigs can be 100+ bones deep), this is wasteful.

Unreal uses a "dirty flag" propagation scheme. Unity DOTS uses TransformSystemGroup with chunk-based dirty checking. Godot uses `_notification(NOTIFICATION_TRANSFORM_CHANGED)` to propagate changes lazily.

### Impact If Absent

- 10,000 nodes x 2 mat4 multiplications per frame = ~320,000 float ops wasted
- Deep hierarchies (skeleton + attachment points) amplify the cost
- Cannot parallelize transform updates without knowing which subtrees are dirty
- Profile shows transform update as top CPU consumer even in mostly-static scenes

### Technical Design

#### Dirty Flag Component

```cpp
struct CLocalTransform {
    Vec3    position;
    Quat    rotation;
    Vec3    scale;
};

struct CWorldTransform {
    Mat4    matrix;          // Computed from local + parent world
    bool    valid;           // False = needs recomputation
};

struct CHierarchy {
    entt::entity parent;
    entt::entity first_child;
    entt::entity next_sibling;
    uint16_t     depth;      // 0 = root, 1 = child of root, etc.
};
```

#### Optimized Update System

```cpp
void system_update_transforms(entt::registry& world) {
    // Only process entities with invalidated world transforms
    // Sort by depth to ensure parents are processed before children

    auto view = world.view<CLocalTransform, CWorldTransform, CHierarchy>();

    // Phase 1: Collect dirty entities
    thread_local std::vector<entt::entity> dirty;
    dirty.clear();

    for (auto [entity, local, world_tf, hier] : view.each()) {
        if (!world_tf.valid) {
            dirty.push_back(entity);
        }
    }

    // Phase 2: Sort by depth (parents before children)
    std::sort(dirty.begin(), dirty.end(), [&](entt::entity a, entt::entity b) {
        return view.get<CHierarchy>(a).depth < view.get<CHierarchy>(b).depth;
    });

    // Phase 3: Recompute world matrices
    for (entt::entity e : dirty) {
        auto& [local, world_tf, hier] = view.get<CLocalTransform, CWorldTransform, CHierarchy>(e);

        Mat4 local_mat = compose_trs(local.position, local.rotation, local.scale);

        if (hier.parent != entt::null) {
            auto& parent_world = world.get<CWorldTransform>(hier.parent);
            world_tf.matrix = parent_world.matrix * local_mat;
        } else {
            world_tf.matrix = local_mat;
        }

        world_tf.valid = true;

        // Invalidate all children (they need recomputation)
        invalidate_children(world, e);
    }
}

void invalidate_children(entt::registry& world, entt::entity parent) {
    auto& hier = world.get<CHierarchy>(parent);
    entt::entity child = hier.first_child;
    while (child != entt::null) {
        auto& child_world = world.get<CWorldTransform>(child);
        if (child_world.valid) {
            child_world.valid = false;
            invalidate_children(world, child);  // Recursive
        }
        child = world.get<CHierarchy>(child).next_sibling;
    }
}
```

#### Parallel Update (Subtree Parallelism)

If independent subtrees are identified (no parent-child relationship), their transform updates can run on separate threads. The `depth` field in `CHierarchy` enables this: all entities at depth 0 can be processed in parallel, then depth 1, etc.

```cpp
// Parallel update by depth layer
for (uint16_t depth = 0; depth <= max_depth; ++depth) {
    // All entities at this depth are independent — parallel_for
    parallel_for(entities_at_depth[depth], [&](entt::entity e) {
        recompute_world_matrix(world, e);
    });
}
```

### Complexity: M (1-2 weeks)

### Dependencies

- ECS component refactoring (split CTransform into CLocalTransform + CWorldTransform)
- CHierarchy component (explicit parent/child/sibling links in ECS, not just scene graph)

---

## Summary Table

| # | Gap | Complexity | Impact | Phase Recommendation |
|---|-----|-----------|--------|---------------------|
| 1 | World Streaming / Level Streaming | XL | Open-world games impossible | Phase 3 or early Phase 4 |
| 2 | Spatial Partitioning (Octree/BVH) | L | O(n) queries kill performance at scale | Phase 2 (before physics) |
| 3 | Entity Lifecycle / Object Pooling | M | Frame hitches from allocation churn | Phase 2 |
| 4 | Reactive Systems / Change Detection | M | Wasted CPU on idle entities | Phase 2 |
| 5 | System Execution Order / Tick Groups | M | Hardcoded loop does not scale | Phase 1 (foundational) |
| 6 | Component Serialization / Schema Evolution | L | Engine updates break saves and scenes | Phase 2 |
| 7 | Snapshot / Rollback (Netcode Foundation) | L | Phase 5+ networking becomes rewrite | Phase 3 (architecture only) |
| 8 | Gameplay Ability System (GAS) | L | Every RPG reinvents the wheel | Phase 3 |
| 9 | FSM / Behavior Trees (Native) | L | AI code is unstructured spaghetti | Phase 3 (with NavMesh) |
| 10 | Prefab Override System (Deep Nesting) | L | Complex prefab composition impossible | Phase 2 (with prefab edit mode) |
| 11 | Entity Templates / Data-Driven Archetypes | M | No lightweight entity type definitions | Phase 2 |
| 12 | C++ Component Hot-Reload | M | Component changes require full restart | Phase 2 (after Schema Registry) |
| 13 | Dependency Injection (EnTT ctx) | M | GameContext becomes God Object | Phase 1 (foundational) |
| 14 | Hierarchical Transform Optimization | M | Wasted CPU on static scene portions | Phase 2 |

---

## Priority Recommendations

### Immediate (Move to Phase 1)

**Gap 5 (System Execution Order)** and **Gap 13 (Dependency Injection)** are foundational. Every subsequent system depends on having a proper system scheduler and clean resource access pattern. Retrofitting these later requires touching every existing system.

### Phase 2 Additions

**Gap 2 (Spatial Partitioning)**, **Gap 3 (Object Pooling)**, **Gap 4 (Change Detection)**, **Gap 6 (Schema Evolution)**, **Gap 10 (Prefab Overrides)**, **Gap 11 (Entity Templates)**, and **Gap 14 (Transform Optimization)** should be implemented alongside the renderer and asset pipeline. These are the "scaling" features that make the difference between a 500-node demo and a 10,000-node game.

### Phase 3 Additions

**Gap 1 (World Streaming)**, **Gap 7 (Snapshot/Rollback)**, **Gap 8 (GAS)**, and **Gap 9 (FSM/BT)** are gameplay-scale features that depend on the Phase 2 infrastructure.

### Phase 2 (After Schema Registry)

**Gap 12 (C++ Component Hot-Reload)** is a developer experience feature that becomes trivial once schema evolution exists.

---

## Comparison with Existing Roadmap

| Current Roadmap Task | Related Gap | Notes |
|---------------------|------------|-------|
| phase3-event-bus | Gap 4 (Reactive) | Event bus should integrate with change detection |
| phase3-tags-groups | Gap 2 (Spatial) | find_by_tag O(n) needs spatial acceleration for large scenes |
| phase3-physics-system | Gap 2, 3 | Physics queries partially cover spatial needs; pooling needed for body churn |
| phase3-save-system | Gap 6, 7 | Save system needs schema evolution; snapshot is a superset |
| phase2-prefab-edit-mode | Gap 10 | Current spec lacks nested prefabs and override inheritance |
| phase4-navmesh | Gap 9 | NavMesh is useless without BT/FSM to drive agents |
| Phase 5+ networking | Gap 7 | Must architect snapshot/determinism NOW to avoid rewrite |

---

## Conclusion

UzEngine's AI-first philosophy and clean architecture provide an excellent foundation. The 14 gaps identified here represent the delta between "functional engine" and "production-ready engine." The most urgent additions are the system scheduler (Gap 5) and dependency injection (Gap 13) — without these, the engine's internal architecture will accumulate technical debt that compounds with every new subsystem.

The data-driven features (entity templates, GAS, FSM/BT) are what elevate an engine from "I can render things" to "I can ship games." Combined with the spatial and streaming infrastructure, they would put UzEngine on par with the systemic capabilities of Godot 4 while maintaining the performance characteristics of a custom C++ engine.

Every gap in this document has been designed to respect UzEngine's existing constraints: C ABI boundary, EnTT-based ECS, LuaJIT scripting, plain-text formats, and AI-first authoring.
