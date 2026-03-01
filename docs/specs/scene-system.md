# Scene System Spec

## Overview

The scene system is the core abstraction of UzEngine. A scene is a tree of **nodes**, each with typed properties and optional children. Scenes are stored as plain-text `.uzscene` files designed to be generated and modified by AI agents without any GUI interaction.

## Node Types

### Base Types

| Type | Description |
|------|-------------|
| `Node` | Base node — no transform, just a named container |
| `Node3D` | 3D node with a local Transform (position, rotation, scale) |
| `Node2D` | 2D node with a 2D transform (future) |

### Renderable

| Type | Properties |
|------|-----------|
| `StaticMesh` | `mesh`, `material`, `cast_shadow`, `receive_shadow`, `lod_bias` |
| `SkinnedMesh` | `mesh`, `material`, `skeleton` (Phase 2) |
| `Sprite3D` | `texture`, `pixel_size`, `billboard` (Phase 2) |

### Lights

| Type | Properties |
|------|-----------|
| `DirectionalLight` | `color`, `intensity`, `shadow_enabled`, `shadow_cascades` |
| `PointLight` | `color`, `intensity`, `radius`, `shadow_enabled` |
| `SpotLight` | `color`, `intensity`, `radius`, `angle_inner`, `angle_outer` |

### Camera

| Type | Properties |
|------|-----------|
| `Camera` | `fov`, `near`, `far`, `projection` (perspective/orthographic), `is_main` |

### Collision (Phase 3)

| Type | Properties |
|------|-----------|
| `StaticBody` | `material_physics` |
| `RigidBody` | `mass`, `linear_damping`, `angular_damping` |
| `CapsuleCollider` | `radius`, `height`, `offset` |
| `BoxCollider` | `half_extents`, `offset` |
| `MeshCollider` | `mesh`, `convex` |

### Audio (Phase 3)

| Type | Properties |
|------|-----------|
| `AudioSource` | `clip`, `autoplay`, `loop`, `volume`, `spatial` |
| `AudioListener` | (marks position of audio listener) |

---

## `.uzscene` File Format

```ini
[uzscene version=1 name="SceneName"]

; Root node — parent=0 means root
[node id=1 type=Node3D name="Root" parent=0]
; optional transform, defaults to identity

; Child node
[node id=2 type=StaticMesh name="Ground" parent=1]
mesh             = "res://assets/terrain.glb"
material         = "res://materials/grass.uzmat"
cast_shadow      = true
receive_shadow   = true

; Transform uses compact mat3x4 (column-major)
[node id=3 type=Node3D name="PropGroup" parent=1]
position = [10.0, 0.0, -5.0]
rotation = [0.0, 45.0, 0.0]
scale    = [1.0, 1.0, 1.0]

[node id=4 type=DirectionalLight name="Sun" parent=1]
color            = [1.0, 0.96, 0.87]
intensity        = 1.5
shadow_enabled   = true
shadow_cascades  = 3
position         = [0.0, 50.0, 0.0]
rotation         = [-45.0, 30.0, 0.0]

[node id=5 type=Camera name="MainCamera" parent=1]
fov      = 75.0
near     = 0.1
far      = 1000.0
is_main  = true
position = [0.0, 1.7, 5.0]
```

### Format Rules

- `parent=0` is the implicit scene root (no actual node with id=0)
- IDs are stable integers assigned on node creation, never reused (generation handles map to them internally)
- Properties not listed use type defaults
- `res://` prefix = path relative to project root
- Rotation is Euler degrees (XYZ order) for human/AI readability; internally stored as quaternion
- Comments start with `;`

---

## C++ Data Model

```cpp
// scene/node.h
struct NodeId    { uint32_t index; uint32_t gen; };  // stable handle

enum class NodeType : uint16_t {
    Node, Node3D, StaticMesh, DirectionalLight,
    PointLight, SpotLight, Camera,
    // ...
};

struct NodeBase {
    NodeId      id;
    NodeType    type;
    std::string name;
    NodeId      parent;
    std::vector<NodeId> children;
};

struct Transform3D {
    glm::vec3 position = {0,0,0};
    glm::quat rotation = {1,0,0,0};
    glm::vec3 scale    = {1,1,1};
    glm::mat4 local_matrix() const;
    glm::mat4 world_matrix;  // updated by system_update_transforms()
};

struct StaticMeshData {
    NodeBase        base;
    Transform3D     transform;
    ResourceHandle  mesh;
    ResourceHandle  material;
    bool            cast_shadow   = true;
    bool            receive_shadow = true;
};
```

---

## Scene Manager API

```cpp
class SceneManager {
public:
    // Loading / saving
    bool        load(const std::string& path, Arena& arena);
    bool        save(const std::string& path) const;
    void        unload();

    // Node access
    NodeId      add_node(NodeType type, NodeId parent);
    void        remove_node(NodeId id);
    NodeBase*   get(NodeId id);             // null if stale
    template<typename T> T* get_as(NodeId id);

    // Property modification (used by Inspector + IPC)
    bool        set_property(NodeId id, std::string_view key, const JsonValue& val);
    JsonValue   get_property(NodeId id, std::string_view key) const;

    // Traversal
    void        walk(std::function<void(NodeBase&)> fn);
    JsonValue   serialize_tree() const;  // returns full tree for MCP
};
```

---

## IPC Handlers (scene.*)

```json
scene.load     { "path": "res://scenes/main.uzscene" }
scene.save     { "path": "res://scenes/main.uzscene" }
scene.getTree  {}  → { "nodes": [...] }
scene.addNode  { "type": "StaticMesh", "parent_id": 2, "name": "Barrel" }
scene.removeNode { "id": 5 }
scene.setProperty { "id": 3, "key": "intensity", "value": 2.0 }
scene.getProperty { "id": 3, "key": "intensity" } → { "value": 2.0 }
scene.selectNode  { "id": 3 }  → editor highlights node in Scene Tree panel
```
