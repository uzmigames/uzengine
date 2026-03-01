# Spec: Tags / Groups

> **Status**: Planned (Phase 3)
> **Owner**: `uz_engine.lib` (ECS `CTags` component + `TagRegistry`)
> **Lua API**: `ctx.scene:find_by_tag`, `node:has_tag`, `node:add_tag`, `node:remove_tag`

---

## Overview

Tags are named labels attached to nodes that allow efficient group-based queries without traversing the full scene graph. A node can have multiple tags. Game systems query all nodes with a given tag in O(n) with n being the total node count — fast enough for gameplay code.

This is equivalent to:
- **Godot Groups** — nearly identical semantics (`add_to_group`, `get_nodes_in_group`)
- **Unity Tags + Layers** — Tags are the string labels; Layers (physics/rendering bitmask) are handled separately in UzEngine and are not part of this spec

---

## `.uzscene` Format

Tags are declared as a space-separated or comma-separated string list in the node block:

```ini
[uzscene version=1 name="Level01"]

[node id=1 type=Node3D name="Root" parent=0]

[node id=5 type=Node3D name="Goblin" parent=1]
position = [10.0, 0.0, 0.0]
tags     = ["enemy", "damageable", "ground_unit"]
script   = "res://game/goblin_ai.lua"

[node id=6 type=Node3D name="HealthPack" parent=1]
tags     = ["pickup", "consumable"]
mesh     = "res://assets/health_pack.uzmesh"

[node id=7 type=StaticMesh name="Wall" parent=1]
tags     = ["environment", "collidable"]
mesh     = "res://assets/wall.uzmesh"
```

Rules:
- `tags` field is optional; omitting it means the node has no tags
- Tag names are case-sensitive ASCII strings, max 32 characters each
- Duplicate tag names on the same node are silently ignored
- Maximum 64 unique tag names per scene (enforced by the `TagRegistry`; see ECS section)

---

## C++ API

```cpp
#include "engine/scene/scene_manager.h"
#include "engine/ecs/components.h"
```

### Scene-Level Queries

```cpp
// Returns all nodes that have the "enemy" tag
Array<NodeHandle> enemies = scene.find_by_tag("enemy");

// Returns true if any node in the scene has this tag
bool any = scene.has_any_with_tag("boss");

// Iterate directly (avoids allocation)
scene.for_each_with_tag("enemy", [](NodeHandle node) {
    apply_area_damage(node, damage, radius);
});
```

- `find_by_tag(tag)` → `Array<NodeHandle>`: scans all entities with `CTags`, filters by bit. O(n) where n = node count. Result is a snapshot — removing nodes during iteration is safe (uses handle validation).
- `for_each_with_tag(tag, fn)` → iterates in-place, no heap allocation. Preferred for hot paths.

### Node-Level Tag API

```cpp
NodeHandle node = scene.find_node("Goblin");

// Query
bool is_enemy = scene.node_has_tag(node, "enemy");

// Mutation (enqueues SceneCommand — applies at frame start)
scene.node_add_tag(node, "stunned");
scene.node_remove_tag(node, "ground_unit");

// Bulk read
TagSet tags = scene.node_get_tags(node);   // copy of the bitset
```

All mutation calls enqueue a `SceneCommand::SET_TAGS` — they do not modify the component directly. This matches the scene mutation policy used for position/rotation/etc.

---

## Lua API

### Scene Queries

```lua
-- Find all enemies
local enemies = ctx.scene:find_by_tag("enemy")
for _, node in ipairs(enemies) do
    deal_damage(node, 10)
end

-- Check if any node has a tag (returns boolean)
local has_boss = ctx.scene:has_any_with_tag("boss")
```

- `ctx.scene:find_by_tag(tag)` → Lua array of `NodeRef` (may be empty table `{}`)
- `ctx.scene:has_any_with_tag(tag)` → `boolean`

### Node Tag Operations

```lua
-- Query
if node:has_tag("damageable") then
    node_health = node_health - damage
end

-- Mutation (enqueued, applied next frame start)
node:add_tag("stunned")
node:remove_tag("ground_unit")

-- Get all tags as a Lua array of strings
local tag_list = node:get_tags()   -- e.g. {"enemy", "damageable", "stunned"}
```

| Method | Returns | Notes |
|--------|---------|-------|
| `node:has_tag(tag)` | `boolean` | Reads current frame's tag state |
| `node:add_tag(tag)` | `nil` | Enqueues SET_TAGS; visible next frame |
| `node:remove_tag(tag)` | `nil` | Enqueues SET_TAGS; visible next frame |
| `node:get_tags()` | `string[]` | Returns copy of all tag names |

---

## ECS Backing: `CTags` Component

```cpp
// src/engine/ecs/components.h

// TagRegistry maps tag string → bit index (0..63)
// One registry per active scene; reset on scene load
class TagRegistry {
    std::unordered_map<std::string, uint8_t> tag_to_bit_;
    std::string                              bit_to_tag_[64];
    uint8_t                                  next_bit_ = 0;

public:
    // Returns bit index for tag, registering it if new.
    // Asserts (debug) / returns 255 (release) if registry is full (64 tags max).
    uint8_t get_or_register(std::string_view tag);

    // Look up bit index; returns 255 if tag is not registered.
    uint8_t find(std::string_view tag) const;

    // Return tag name for a given bit index.
    std::string_view name_of(uint8_t bit) const;

    // Number of registered tags.
    uint8_t count() const { return next_bit_; }
};

// Per-node tag component
struct CTags {
    std::bitset<64> bits;   // bit N = 1 means the tag at registry[N] is active
};
```

### Registration Flow at Scene Load

```
SceneParser reads [node ... tags=["enemy","damageable"]]
  ↓
For each tag string:
  bit = TagRegistry::get_or_register(tag_name)
  CTags.bits.set(bit)
  ↓
If registry is full (64 tags already):
  → log warning: "[tags] registry full, tag 'flying' ignored on node 'Harpy'"
  → tag is silently dropped (does not crash)
```

### Query Implementation

```cpp
// SceneManager::find_by_tag — O(n) scan over all entities with CTags
Array<NodeHandle> SceneManager::find_by_tag(std::string_view tag) {
    uint8_t bit = tag_registry_.find(tag);
    if (bit == 255) return {};  // tag never registered → no matches

    Array<NodeHandle> result;
    ecs_.for_each<CTags>([&](EntityId id, CTags& tags) {
        if (tags.bits.test(bit)) {
            result.push_back(NodeHandle{ id });
        }
    });
    return result;
}
```

The `bitset<64>` test is a single 64-bit AND + compare — essentially free.

---

## IPC Handler

```
scene.findByTag
```

| Command | Args | Returns | Purpose |
|---------|------|---------|---------|
| `scene.findByTag` | `{tag: string}` | `{nodes: [{id, name}]}` | Query nodes by tag from editor/AI |
| `scene.getNodeTags` | `{node_id: number}` | `{tags: string[]}` | Get all tags of a specific node |
| `scene.setNodeTags` | `{node_id: number, tags: string[]}` | `{ok: bool}` | Set node tags from editor (replaces all) |

Example MCP usage:

```json
// Request
{"cmd": "scene.findByTag", "args": {"tag": "enemy"}}

// Response
{"nodes": [
  {"id": 5, "name": "Goblin"},
  {"id": 9, "name": "Orc"},
  {"id": 14, "name": "Troll"}
]}
```

These handlers are available in both `--editor` and `--play` modes.

---

## Constraints and Limits

| Constraint | Value | Reason |
|---|---|---|
| Max unique tags per scene | 64 | `std::bitset<64>` fits in one 64-bit register |
| Max tag name length | 32 ASCII characters | `StringId` backing limit |
| Tag name charset | `[a-zA-Z0-9_-]` | Enforced at parse time; invalid chars → warning + skip |
| Tags added at runtime | Supported | `node:add_tag` enqueues; tag registered in `TagRegistry` |
| Tags survive scene save | YES | Saved as `tags = [...]` in `.uzscene` if authored via editor |
| Tags survive hot-reload | YES | Scene not torn down on shader/Lua hot-reload |

---

## Runtime Tag Addition

Tags can be added at runtime (from Lua or C++) — they are registered in the `TagRegistry` when first seen, just like scene-load tags:

```lua
-- At runtime: tag the player as "invincible" during an item effect
player_node:add_tag("invincible")
-- ... 5 seconds later
player_node:remove_tag("invincible")
```

This uses the same `SceneCommand::SET_TAGS` queue. The tag `"invincible"` will consume a bit slot in the registry on first use. If the registry is full, the command is silently dropped with a warning.

---

## Comparison with Unity and Godot

### vs Unity Tags

Unity allows only **one** tag per object (`gameObject.tag`). UzEngine allows **many** tags (up to 64) — this matches the most common use pattern where objects belong to multiple categories.

Unity Layers (bitmask, 32 bits) handle physics filtering and rendering culling. UzEngine separates these concerns: Tags are for gameplay queries; physics/render layers are a separate bitmask on the `CPhysicsBody` / `CMeshRenderer` components (not part of this spec).

### vs Godot Groups

Godot Groups are nearly identical to UzEngine Tags. Key differences:

| Feature | Godot Groups | UzEngine Tags |
|---------|--------------|---------------|
| Max groups | Unlimited (dictionary) | 64 per scene (bitset) |
| Query speed | O(group size) per group | O(node count) flat scan |
| Backing | Dictionary<String, Array> | bitset<64> + TagRegistry |
| Per-node storage | Group membership in Node | CTags ECS component |
| IPC/AI access | No built-in | `scene.findByTag` IPC |

The bitset limit (64) is intentional: it makes membership testing a single CPU instruction and is large enough for any real game's tag taxonomy.

---

## C++ Source Files

```
src/engine/
├── scene/
│   ├── tag_registry.h/cpp      — TagRegistry: string → bit index, max 64
│   ├── scene_manager.h/cpp     — find_by_tag, for_each_with_tag, node_has_tag,
│                                  node_add_tag, node_remove_tag, node_get_tags
├── ecs/
│   └── components.h            — CTags { std::bitset<64> bits }
src/engine/scripting/
└── lua_binding.h/cpp           — ctx.scene:find_by_tag, ctx.scene:has_any_with_tag,
                                   node:has_tag, node:add_tag, node:remove_tag, node:get_tags
src/runtime/ipc/handlers/
└── scene_handlers.cpp          — scene.findByTag, scene.getNodeTags, scene.setNodeTags
```

---

## Phase Roadmap

| Phase | Deliverable |
|-------|-------------|
| **Phase 3** | `CTags` component, `TagRegistry`, C++ query API, Lua binding, `.uzscene` parser support, IPC handlers |
| **Phase 4** | Tags serialized into `.uzpak`; tag-based culling hints for renderer (optional optimization) |

---

## Related Docs

- [specs/scene-system.md](scene-system.md) — `.uzscene` format, `SceneCommand` queue, SceneManager
- [specs/lua-scripting.md](lua-scripting.md) — `NodeRef` API, `ctx.scene` bindings
- [specs/event-bus.md](event-bus.md) — `NodeAdded`/`NodeRemoved` events carry node handle (tags can be queried on newly added nodes)
- [engine/cpp-architecture.md](../engine/cpp-architecture.md) — ECS component system, `Handle<T>`
