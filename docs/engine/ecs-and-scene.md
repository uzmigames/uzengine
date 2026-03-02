# UzEngine — ECS Architecture and Scene System

> **Audience**: Engine developers and AI agents generating game code
> **Scope**: Entity-Component-System (EnTT) design, component catalog, scene graph structure, .uzscene format, prefab system
> **Language**: C++23

---

## 1. Overview

The scene system in UzEngine is dual-layered:

1. **Scene Graph Layer** (NodeBase tree) — The editor's hierarchical view of game objects, persisted in `.uzscene` files
2. **ECS Layer** (EnTT registry) — The simulation runtime's data-oriented compute engine

These layers are linked by a 1:1 mapping: each NodeBase has a corresponding `entt::entity`, allowing the editor to show a familiar tree while the engine runs physics, rendering, and logic with cache-friendly data layout.

---

## 2. ECS Architecture with EnTT

### 2.1 Why EnTT

| Property | Benefit |
|----------|---------|
| **Header-only** | No build/link dependency; zero-cost in release builds |
| **Cache-friendly** | SoA (struct-of-arrays) layout; tight loops iterate dense component storage |
| **Type-safe** | Compile-time component types; no type erasure overhead |
| **No macros** | Clean C++; easy IDE tooling support |
| **Flexible queries** | Single-component views, multi-component groups, sparse iteration |

EnTT is used in Unreal Engine 5's Cesium plug-in and Godot 4.2+ experimental systems.

### 2.2 Component Design Rules

All ECS components **must** follow strict POD (Plain Old Data) rules:

| Rule | Why |
|------|-----|
| **No virtual methods** | Virtual tables break SoA layout cache efficiency |
| **No side-effect constructors** | Components are raw data; construction is placement-new |
| **No smart pointers** | Use `StringId` for identity, raw pointers for borrowing, `Handle<T>` for cross-lifetime refs |
| **No std containers** | Use Arena-backed `array<T>`, `string`, `hash_map<K,V>` |
| **POD-only fields** | Struct fields are primitives, vectors, matrices, enums, or other PODs |

```cpp
// ✅ CORRECT
struct CTransform {
    Vec3 position;
    Quat rotation;
    Vec3 scale;
};

// ❌ WRONG
struct CTransform {
    virtual ~CTransform() {}  // Virtual — breaks layout
    std::string name;         // std container — use StringId
    std::unique_ptr<Data> d;  // Smart pointer — use Handle<Data>
    void init() {}            // Side-effect constructor
};
```

### 2.3 Component Naming Convention

All components use the **`C` prefix** followed by PascalCase:

- `CTransform` — position, rotation, scale
- `CMeshRenderer` — mesh + material identity
- `CRigidBody` — Jolt physics body reference
- `CScriptRef` — Lua or C++ script instance
- `CCamera` — projection settings and viewport state

This convention makes component types instantly recognizable in code: `registry.emplace<CTransform>(entity, ...)`

### 2.4 System Design Rules

Systems are **free functions**, not class methods. They operate on queries (views/groups) of components:

```cpp
// ✅ CORRECT — free function system
void update_physics(entt::registry& world, float dt) {
    auto view = world.view<CRigidBody, CTransform>();
    for (auto entity : view) {
        auto [rb, tf] = view.get<CRigidBody, CTransform>(entity);
        // Update rigid body; sync to transform
    }
}

// Run this system each frame:
// update_physics(world, delta_time);

// ❌ WRONG — class-based system
class PhysicsSystem {
    void update(float dt) { /* ... */ }
};
```

Benefits:
- No virtual calls in tight loops
- Easy to parallelize with thread pools
- No hidden state or dependencies
- Testable with just a registry

### 2.5 Query Patterns

#### Single-Component View (Iteration-Heavy)
Use `entt::view` when iterating many entities with one component:

```cpp
auto view = registry.view<CTransform>();  // ~1000 entities
for (auto entity : view) {
    auto& tf = view.get<CTransform>(entity);
    // Fast cache iteration
}
```

#### Multi-Component Group (Hot-Path Cache)
Use `entt::group` when you need multiple components and iteration frequency is high:

```cpp
// Pre-build at scene load
auto group = registry.group<CTransform>(entt::get<CMeshRenderer, CRigidBody>);
// group.size() is cached; iteration is SoA-optimized
for (auto entity : group) {
    auto [tf, mr, rb] = group.get<CTransform, CMeshRenderer, CRigidBody>(entity);
    // Tight render/physics loop
}
```

#### Sparse Single Query (Rare Component)
Use direct lookup for unique components:

```cpp
auto cameras = registry.view<CCamera>();
for (auto entity : cameras) {
    // Usually just 1 or 2 entities (active camera + preview camera)
}
```

### 2.6 Entity Lifecycle

**Creation** (on scene load):
```cpp
// For each NodeBase in scene tree:
entt::entity e = registry.create();
registry.emplace<CTransform>(e, node->position, node->rotation, node->scale);
if (node->has_mesh) registry.emplace<CMeshRenderer>(e, mesh_id, material_id);
if (node->has_script) registry.emplace<CScriptRef>(e, script_id, SCRIPT_LUA);
```

**Destruction** (on node remove or scene unload):
```cpp
// Never store entt::entity across frame boundaries without validation
// Always fetch fresh from node reference:
entt::entity e = find_entity_for_node(node_id);
if (registry.valid(e)) {
    registry.destroy(e);  // Destroys all components; entity ID becomes invalid
}
```

**Validation** (multi-frame references):
```cpp
// If you must cache an entity across frames:
struct EntityHandle {
    entt::entity id;
    uint32_t generation;  // Not native to EnTT; add if needed
};

// Before using: registry.valid(cached_entity)
// Or store node reference instead: uint32_t node_id (stable)
```

---

## 3. Full Component Catalog

All components are in `src/ecs/components.h`. Each is a POD struct.

| Component | Description | Fields |
|-----------|-------------|--------|
| **CTransform** | Node position, rotation, scale in world space | `Vec3 position; Quat rotation; Vec3 scale;` |
| **CMeshRenderer** | Static mesh rendering | `StringId mesh_id; StringId material_id; bool cast_shadow; uint32_t layer_mask;` |
| **CDirLight** | Directional light (sun) | `Vec3 color; float intensity; bool cast_shadow; float shadow_distance;` |
| **CPointLight** | Omnidirectional point light | `Vec3 color; float intensity; float radius; bool cast_shadow;` |
| **CSpotLight** | Cone-shaped spotlight | `Vec3 color; float intensity; float inner_angle; float outer_angle; bool cast_shadow;` |
| **CCamera** | Projection and viewport state | `float fov_deg; float near_z; float far_z; bool is_active; ProjectionType projection;` |
| **CScriptRef** | Node script binding (Lua or C++) | `StringId script_id; ScriptKind kind; void* state; uint32_t instance_ref;` |
| **CRigidBody** | Jolt Physics rigid body | `JPH::BodyID body_id; float mass; float restitution; float friction; bool is_static; bool is_kinematic;` |
| **CColliderBox** | Axis-aligned box collider | `Vec3 half_extents; Vec3 offset;` |
| **CColliderSphere** | Sphere collider | `float radius; Vec3 offset;` |
| **CColliderCapsule** | Capsule collider (cylinder + hemispheres) | `float radius; float half_height; Vec3 offset;` |
| **CColliderMesh** | Convex mesh collider | `StringId mesh_id;` |
| **CTrigger** | Collision volume (no physics response) | `JPH::BodyID body_id;` |
| **CCharacterController** | Jolt CharacterVirtual wrapper | `JPH::CharacterVirtual* controller; float ground_speed;` |
| **CAudioSource** | Spatial audio playback | `StringId clip_id; float volume; float pitch; float min_dist; float max_dist; uint8_t bus_index; bool loop; bool spatial;` |
| **CAudioListener** | Audio listener (tag-only) | *(No fields — position from CTransform)* |
| **CAnimator** | Skeletal animation state machine | `StringId state_machine_id; uint32_t current_state; float blend_factor;` |
| **CSkeletalMesh** | Skinned mesh with skeleton | `StringId mesh_id; StringId skeleton_id; uint32_t bone_palette_offset;` |
| **CParticleEmitter** | GPU particle effect | `StringId effect_id; bool is_playing; float emit_rate;` |
| **CNavAgent** | Navigation AI agent | `DetourIdx agent_id; Vec3 destination; float speed; float radius;` |
| **CTags** | Node tagging system | `std::bitset<64> bits;` |
| **CUIRoot** | UI document root | `StringId document_path; uint16_t sort_key; bool auto_show; bool modal;` |

---

## 4. Scene Graph Design

The scene graph is the **editor's persistent representation**. It is built from `.uzscene` files.

### 4.1 NodeBase Structure

All nodes derive from a single POD base:

```cpp
struct NodeBase {
    uint32_t id;              // Stable node ID (persistent across hot-reload)
    uint32_t parent_id;       // Parent node (0 = root)
    uint32_t first_child_id;  // First child in linked list
    uint32_t next_sibling_id; // Next sibling in linked list
    StringId name;            // Unique within parent
    NodeType type;            // Discriminant for node type

    entt::entity entity;      // Linked ECS entity (1:1 mapping)
};

enum class NodeType : uint8_t {
    Node3D = 0,
    StaticMesh,
    Camera,
    DirectionalLight,
    PointLight,
    SpotLight,
    AudioSource,
    ParticleEmitter,
    NavMeshAgent,
    UIRoot,
    // ... expand as needed
};
```

### 4.2 Tree Traversal

The linked-list approach (sibling chain) is cache-friendly for culling and batching:

```cpp
// Iterate children of a node (breadth-first one level)
void iterate_children(const NodeBase& parent, function<void(NodeBase&)> fn) {
    uint32_t child_id = parent.first_child_id;
    while (child_id != 0) {
        NodeBase& child = node_pool.get(child_id);
        fn(child);
        child_id = child.next_sibling_id;
    }
}

// Depth-first traversal (useful for culling, rendering order)
void dfs_traverse(const NodeBase& node, function<void(const NodeBase&)> fn) {
    fn(node);
    uint32_t child_id = node.first_child_id;
    while (child_id != 0) {
        NodeBase& child = node_pool.get(child_id);
        dfs_traverse(child, fn);
        child_id = child.next_sibling_id;
    }
}
```

### 4.3 ECS Mapping

When a node is added or loaded:

1. Create an `entt::entity` in the registry
2. Populate components based on NodeType (transform, renderer, script, etc.)
3. Store `entity` in `NodeBase::entity` for O(1) lookup
4. On node delete: destroy the entity

```cpp
void NodeBase::sync_to_ecs(entt::registry& world) {
    if (!world.valid(entity)) {
        entity = world.create();
    }
    world.emplace_or_replace<CTransform>(entity, position, rotation, scale);
    // Populate other components based on type
}

void NodeBase::destroy_in_ecs(entt::registry& world) {
    if (world.valid(entity)) {
        world.destroy(entity);
    }
}
```

---

## 5. .uzscene Format Reference

UzEngine scenes are stored as **INI-like plain-text files** for AI readability and diffing.

### 5.1 Scene Header

```ini
[scene]
name = "Level1"
version = 1
skybox = "res://materials/skybox.uzmat"
physics_gravity = 0, -9.81, 0
```

### 5.2 Node Definition

Each node is a section `[node id=<id> type=<type> name="<name>" parent=<parent_id>]`:

```ini
[node id=1 type=Node3D name="Root" parent=0]
position = 0, 0, 0
rotation = 0, 0, 0, 1
scale = 1, 1, 1

[node id=2 type=StaticMesh name="Floor" parent=1]
position = 0, -0.5, 0
rotation = 0, 0, 0, 1
scale = 1, 1, 1
mesh = "res://assets/floor.uzmesh"
material = "res://materials/concrete.uzmat"
cast_shadow = true
layer_mask = 0xFFFFFFFF

[node id=3 type=Node3D name="Player" parent=1]
position = 0, 1, 0
rotation = 0, 0, 0, 1
scale = 1, 1, 1
script = "res://game/player.lua"
tags = "player,npc"
collider_type = "capsule"
collider_radius = 0.4
collider_half_height = 0.9
rigid_body_mass = 1.0
rigid_body_layer = "Player"
rigid_body_kinematic = true

[node id=4 type=DirectionalLight name="Sun" parent=1]
position = 0, 10, 0
rotation = -0.707, 0, 0, 0.707
rotation_quat = true
light_color = 1.0, 0.95, 0.8
light_intensity = 3.0
light_cast_shadow = true

[node id=5 type=AudioSource name="BgMusic" parent=1]
position = 0, 0, 0
audio_clip = "res://audio/ambient.ogg"
audio_loop = true
audio_play_on_start = true
audio_bus = "Music"
audio_volume = 0.7
audio_spatial = false

[node id=10 type=UIRoot name="HUD" parent=0]
ui_document = "res://ui/hud.html"
ui_layer = 500
ui_auto_show = true
ui_modal = false
```

### 5.3 Property Parsing Rules

- **Vectors**: `x, y, z` or `x, y, z, w` (space-trimmed)
- **Quaternions**: `x, y, z, w` (stored as `rotation` unless marked `rotation_quat = true`)
- **Colors**: RGB or RGBA as floats `0.0 - 1.0`
- **Booleans**: `true` / `false` (case-insensitive)
- **Numbers**: Decimal or hex (prefix `0x`)
- **Strings**: Quoted with double quotes; escape `"` as `\"`
- **Comments**: Lines starting with `;` are ignored

### 5.4 Prefab Instantiation

Prefabs allow reusing scene hierarchies. A prefab reference looks like:

```ini
[node id=20 type=Node3D name="Enemy1" parent=1]
prefab = "res://prefabs/goblin.uzscene"
prefab_variant = "elite"

[override node_path="Enemy1/Body" property="material"]
value = "res://materials/elite_goblin.uzmat"

[override node_path="Enemy1/Head" property="light_intensity"]
value = 2.0
```

On instantiation:
1. Load prefab scene
2. Clone node tree under Enemy1
3. Apply overrides (property patches)
4. Sync all nodes to ECS

---

## 6. Scene Lifecycle

### 6.1 Load Phase

```cpp
// SceneManager::load("res://levels/level1.uzscene")
SceneLoadResult load_scene(StringView path) {
    // 1. Parse .uzscene INI file
    INIDocument doc = parse_ini(path);

    // 2. Build NodeBase tree
    NodePool nodes;
    for (const auto& section : doc.sections) {
        if (section.name.starts_with("node")) {
            NodeBase nb = parse_node_section(section);
            nodes.add(nb);
        }
    }

    // 3. Populate ECS from node tree (DFS)
    entt::registry world;
    for (const auto& node : nodes.all()) {
        entt::entity e = world.create();
        node.entity = e;

        world.emplace<CTransform>(e, node.position, node.rotation, node.scale);

        if (node.type == NodeType::StaticMesh) {
            world.emplace<CMeshRenderer>(e, ...);
        } else if (node.type == NodeType::DirectionalLight) {
            world.emplace<CDirLight>(e, ...);
        }
        // ... match all node types
    }

    return { nodes, world };
}
```

### 6.2 Init Phase

```cpp
// LuaVM::on_start()
void init_scene(GameContext& ctx) {
    // 1. Create LuaVM for script execution
    ctx.lua = LuaVM::create(ctx);

    // 2. Load and instantiate all scripts
    for (auto entity : ctx.world.view<CScriptRef>()) {
        auto& script = ctx.world.get<CScriptRef>(entity);
        if (script.kind == SCRIPT_LUA) {
            LuaScriptRunner::on_start(ctx.lua, entity, script);
        }
    }

    // 3. Trigger game on_start
    game_api.on_start(&ctx);
}
```

### 6.3 Per-Frame Loop

```cpp
void game_loop(GameContext& ctx, float delta_time) {
    // 1. Input (platform-native)
    InputState input = platform_read_input();
    ctx.input = input;

    // 2. Physics step (fixed 60 Hz with accumulator)
    ctx.physics_accum += delta_time;
    while (ctx.physics_accum >= PHYSICS_DT) {
        update_physics(ctx.world, PHYSICS_DT);
        ctx.physics_accum -= PHYSICS_DT;
    }

    // 3. Drain event queues (collision, timer, Lua events)
    ctx.event_bus.drain();

    // 4. Scripts: on_update
    for (auto entity : ctx.world.view<CScriptRef>()) {
        auto& script = ctx.world.get<CScriptRef>(entity);
        if (script.kind == SCRIPT_LUA) {
            LuaScriptRunner::on_update(ctx.lua, entity, script, delta_time);
        }
    }

    // 5. Render
    render_frame(ctx);
}
```

### 6.4 Shutdown Phase

```cpp
void unload_scene(GameContext& ctx) {
    // 1. Script on_stop
    for (auto entity : ctx.world.view<CScriptRef>()) {
        auto& script = ctx.world.get<CScriptRef>(entity);
        if (script.kind == SCRIPT_LUA) {
            LuaScriptRunner::on_stop(ctx.lua, entity, script);
        }
    }

    // 2. LuaVM destroy
    LuaVM::destroy(ctx.lua);

    // 3. ECS cleanup
    ctx.world.clear();

    // 4. NodeBase tree reset
    ctx.nodes.clear();

    // 5. Memory arenas (frame, level) reset
    ctx.level_arena.reset();
}
```

---

## 7. Related Documentation

- [Scene System Specification](../specs/scene-system.md) — `.uzscene` format, SceneManager API, IPC handlers
- [Lua Scripting System](scripting-system.md) — Script lifecycle, Lua VM, binding APIs
- [C++ Architecture](cpp-architecture.md) — Arena allocators, Handle system, StringId
- [Design Decisions](design-decisions.md) — Why EnTT, why dual-layer design, ECS trade-offs

---

## 8. Key Takeaways

1. **Dual-layer design**: Scene graph for editing, ECS for simulation
2. **Components are POD**: No virtuals, side effects, or smart pointers
3. **Systems are free functions**: Cache-friendly, testable, parallelizable
4. **Node identity is stable**: NodeBase.id survives reload; use it for cross-frame refs
5. **.uzscene is plain text**: Human and AI readable; diffs cleanly in git
6. **Prefabs are instances**: Not prototypes; changes don't propagate back
7. **Queries are typed**: EnTT compile-time safety eliminates runtime type checking
