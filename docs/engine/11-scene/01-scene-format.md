# Scene — `.uzscene` & `.uzprefab` Format

> **Status**: Planned (Milestone 2.0)
> **Source**: `runtime/engine/scene/`
> **Depends on**: Entity/Component (§10), Property System (§21)
> **References**: UE5 `.umap` (UAsset), Unity `.unity` (YAML), Godot `.tscn` (text)

## Purpose

JSON-based scene format designed for human readability and LLM editability. Supports component hierarchy, prefab instancing with overrides, entity cross-references, and hot-reload.

## `.uzscene` Format

```json
{
    "format": "uzscene",
    "version": 1,

    "settings": {
        "subsystems": ["PhysicsWorld", "NavigationServer"],
        "gravity": [0, 0, -981],
        "ambient_light": [0.1, 0.1, 0.15]
    },

    "entities": [
        {
            "id": "player",
            "components": {
                "SceneComponent": { "position": [0, 0, 1.7] },
                "SkeletalMeshComponent": {
                    "parent": "SceneComponent",
                    "mesh": "content/meshes/character.uzmesh"
                },
                "CameraComponent": {
                    "parent": "SceneComponent",
                    "fov": 90,
                    "is_active": true
                },
                "CharacterMovementComponent": {
                    "max_speed": 600,
                    "jump_velocity": 420
                }
            }
        },
        {
            "id": "enemy_01",
            "prefab": "content/prefabs/grunt.uzprefab",
            "overrides": {
                "SceneComponent": { "position": [10, 5, 0] },
                "Health": { "max": 150 }
            }
        }
    ]
}
```

## Component Hierarchy via `parent` Field

SceneComponents declare their parent attachment inline. The first `SceneComponent` without a `parent` field is the entity root:

```json
"components": {
    "SceneComponent": { "position": [0, 0, 0] },
    "StaticMeshComponent": { "parent": "SceneComponent", "mesh": "..." },
    "CapsuleCollision": { "parent": "SceneComponent", "radius": 0.5 },
    "CharacterBody": { "parent": "CapsuleCollision" }
}
```

Produces:
```
SceneComponent (root)
├── StaticMeshComponent
└── CapsuleCollision
    └── CharacterBody
```

## Multiple Components of the Same Type

Use a suffix after `#`:

```json
"BoxCollision#body": { "parent": "SceneComponent", "half_extents": [0.5, 1.0, 0.3] },
"BoxCollision#head": { "parent": "SceneComponent", "half_extents": [0.3, 0.3, 0.3] }
```

Part before `#` = type name (resolved via Property System). Part after `#` = instance name (for `parent` references and debugging).

## ActorComponents vs SceneComponents

- **ActorComponents**: no `parent` field, flat, non-spatial
- **SceneComponents**: optional `parent` field, inherit transform from parent

The SceneLoader distinguishes them by checking the type's inheritance chain in the Property System registry.

## Entity References by ID

Cross-entity references use the `id` string:

```json
{ "id": "player", "components": { ... } },
{ "id": "controller", "components": {
    "FPSController": { "initial_pawn": "player" }
}}
```

Resolved at load-time. Properties marked with `meta(entity_ref)` are auto-resolved. In shipping builds, string IDs are stripped and replaced by integer indices.

## Transform Fields

SceneComponents carry transform as direct fields — there is no separate "TransformComponent":

```json
"SceneComponent": { "position": [0, 0, 1.7], "rotation": [0, 90, 0], "scale": [1, 1, 1] }
```

- `position`: Vec3, local if has parent. Default `[0, 0, 0]`
- `rotation`: Vec3 Euler angles in degrees (pitch, yaw, roll). Default `[0, 0, 0]`
- `scale`: Vec3. Default `[1, 1, 1]`

## `.uzprefab` Format

A prefab is a reusable entity template:

```json
{
    "format": "uzprefab",
    "version": 1,
    "entity": {
        "components": {
            "SceneComponent": {},
            "StaticMeshComponent": {
                "parent": "SceneComponent",
                "mesh": "content/meshes/crate.uzmesh"
            },
            "RigidBody": { "parent": "SceneComponent", "mass": 10.0 },
            "Health": { "max": 50 }
        }
    }
}
```

### Prefab Instancing with Overrides

```json
{
    "id": "crate_01",
    "prefab": "content/prefabs/crate.uzprefab",
    "overrides": {
        "SceneComponent": { "position": [5, 3, 0] },
        "RigidBody": { "mass": 25.0 }
    }
}
```

Override rules:
- Only listed fields are overridden
- Unlisted component types are **added** (extend the prefab)
- A component set to `null` is **removed**
- Overrides are shallow-merged per component (field-level)

### Nested Prefabs

A prefab can reference another prefab. Resolution is recursive with max depth 8 (prevents cycles).

### Runtime Spawning

```cpp
uz::Entity crate = GetWorld().SpawnPrefab("content/prefabs/crate.uzprefab", {
    .position = {5, 0, 2},
    .rotation = Quat::Identity(),
});
```

## Scene Loading Pipeline

```
.uzscene file (JSON on disk)
    │
    ▼
SceneLoader::Load(path)
    ├── 1. Parse JSON (rapidjson / simdjson)
    ├── 2. Validate format + version
    ├── 3. Apply scene settings (subsystems, gravity)
    ├── 4. For each entity:
    │   ├── If "prefab" → load prefab recursively, apply overrides
    │   ├── For each component:
    │   │   ├── Resolve type name via Property System
    │   │   ├── Allocate component in World
    │   │   ├── Deserialize JSON fields → UZ_PROPERTY fields
    │   │   └── If SceneComponent with "parent" → attach to parent
    │   └── Register entity in World
    ├── 5. Resolve entity references (string ID → Entity handle)
    ├── 6. Call OnRegister() on all components
    ├── 7. Call OnInitialize() on all components
    └── 8. Call OnBeginPlay() on all components
```

## Hot-Reload

When a `.uzscene` or `.uzprefab` file changes on disk (FileWatcher):
1. **Editor mode**: destroy all entities, re-load from updated JSON
2. **Prefab instances**: if a `.uzprefab` changes, all instances are re-created with updated base + overrides re-applied
3. **No hot-reload in shipping** — scenes are baked into `.uzpak`

## Engine Comparison

| Feature | Unreal 5 | Unity | Godot | **UzEngine** |
|---------|----------|-------|-------|--------------|
| Format | Binary `.umap` | Binary YAML | Text `.tscn` | **JSON `.uzscene`** |
| Human-readable | No | Barely | Yes | **Yes** |
| LLM-editable | No | No | Partially | **Yes (pure JSON)** |
| Prefabs | Blueprint (heavy) | `.prefab` (nested) | PackedScene | **`.uzprefab` (JSON + overrides)** |
| Nested prefabs | Yes (complex) | Yes | Yes | **Yes (recursive, max depth 8)** |
| Entity references | GUIDs | fileID (integer) | NodePath | **String ID → Entity handle** |
| Hot-reload | Editor only | Editor only | Yes | **Yes (editor mode)** |

## Rules

1. **ALWAYS use JSON** — no binary scene formats, no custom syntax
2. **ALWAYS use `parent` field** for SceneComponent hierarchy — never nest entities inside entities
3. **ALWAYS register component types** via Property System — unregistered types can't be deserialized
4. **NEVER exceed prefab nesting depth 8** — indicates a design problem, not a technical need
