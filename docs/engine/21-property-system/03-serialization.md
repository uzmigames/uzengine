# Property Serialization — JSON and Binary

> **Status**: Partially Implemented (Phase 3b-3.2b MVP — JSON only)
> **Source**: `engine/property/`
> **References**: UE5 FArchive + FProperty serialization (clean-room)

## Purpose

Automatic serialization/deserialization of components to JSON (`.uzscene` files) and binary (save files). Uses reflection (§21.2) to discover properties and encode their values. Supports versioning and migration for evolved schemas.

## Serialization Pipeline

```
Component Instance
  ├── Read (Runtime → File)
  │   ├── Iterate properties via ClassRegistry
  │   ├── Read value from component memory (offset-based)
  │   └── Encode value to JSON/msgpack
  │
  └── Write (File → Runtime)
      ├── Load JSON/msgpack from file
      ├── Create component instance
      ├── For each property:
      │   ├── Find property in ClassRegistry
      │   ├── Decode value
      │   └── Write to component memory (offset-based)
      └── Validate schema (default for missing fields)
```

## JSON Format (`.uzscene` Scene Files)

Scenes are JSON — human-editable and mergeable:

```json
{
    "uz_format": "scene",
    "uz_version": 1,
    "entities": [
        {
            "id": 1,
            "name": "Player",
            "components": [
                {
                    "type": "Transform",
                    "properties": {
                        "position": [0.0, 100.0, 0.0],
                        "rotation": [0.0, 0.0, 0.0, 1.0],
                        "scale": [1.0, 1.0, 1.0]
                    }
                },
                {
                    "type": "Rigidbody",
                    "properties": {
                        "body_type": "Dynamic",
                        "mass": 80.0,
                        "gravity_enabled": true
                    }
                },
                {
                    "type": "Player",
                    "properties": {
                        "health": 100.0,
                        "mana": 50.0,
                        "level": 1,
                        "inventory": ["sword_0001", "shield_0003"]
                    }
                }
            ]
        }
    ]
}
```

## Binary Format (Save Files)

Saves use msgpack — compact and fast:

```
[entity_1_data]
  ├── entity_id: uint64
  ├── component_1: {type: "Transform", properties: {...}}
  ├── component_2: {type: "Rigidbody", properties: {...}}
  └── ...
[entity_2_data]
  └── ...
```

## PropertySerializer API

```cpp
namespace uz {

/// Serializes and deserializes reflected component instances to/from JSON.
/// Uses ClassInfo property metadata and raw byte offsets to read/write fields
/// without knowing the concrete type. All methods are static.
///
/// # Thread Safety
/// Thread-safe for concurrent serialization of different instances.
/// Not safe to serialize the same instance from multiple threads without sync.
class PropertySerializer {
public:
    // ✅ IMPLEMENTED (Phase 3b-3.2b MVP)
    // ─────────────────────────────────────────────────────────────

    /// Serializes all non-transient properties of an instance to a JSON object.
    ///
    /// # Arguments
    /// * instance - Pointer to component/struct instance (must not be null)
    /// * info - ClassInfo describing the instance's reflected properties
    ///
    /// # Returns
    /// JSON object with property names as keys and serialized values.
    /// Properties with PropertyFlags::Transient are excluded.
    /// Properties with unknown types are skipped with a warning log.
    [[nodiscard]] static nlohmann::json SerializeToJSON(
        const void* instance,
        const ClassInfo& info
    );

    /// Deserializes a JSON object into an existing instance.
    ///
    /// Missing JSON keys are silently skipped (instance retains current/default value).
    /// Type mismatches are logged as warnings and the field is left unchanged.
    ///
    /// # Arguments
    /// * instance - Pointer to component/struct instance (must not be null)
    /// * info - ClassInfo describing the instance's reflected properties
    /// * data - JSON object with property values (must be object, not array/scalar)
    ///
    /// # Returns
    /// true if all present fields deserialized successfully.
    /// false if any field had a type mismatch (partial results still applied).
    static bool DeserializeFromJSON(
        void* instance,
        const ClassInfo& info,
        const nlohmann::json& data
    );

    // 📋 PLANNED (Phase 3b-3.2c)
    // ─────────────────────────────────────────────────────────────

    /// Serialize component to msgpack binary (planned 3b-3.2c)
    // static std::vector<uint8_t> SerializeToMsgpack(...)

    /// Deserialize component from msgpack binary (planned 3b-3.2c)
    // static bool DeserializeFromMsgpack(...)

    // 📋 PLANNED (Phase 3b-3.4)
    // ─────────────────────────────────────────────────────────────

    /// Only serialize properties that differ from defaults (planned 3b-3.4)
    // static nlohmann::json SerializeDelta(...)

    /// Apply delta (missing properties use defaults) (planned 3b-3.4)
    // static bool DeserializeDelta(...)

private:
    /// Serializes a single property value to JSON.
    [[nodiscard]] static nlohmann::json PropertyToJSON(
        const void* instance,
        const PropertyInfo& prop
    );

    /// Deserializes a single property value from JSON into the instance.
    /// Returns true on success, false on type mismatch or unsupported type.
    static bool PropertyFromJSON(
        void* instance,
        const PropertyInfo& prop,
        const nlohmann::json& value
    );
};

}  // namespace uz
```

## Type Encoding (Phase 3b-3.2b MVP)

### Basic Types ✅ Implemented

```json
{
    "bool_value": true,
    "int_value": 42,
    "float_value": 3.14,
    "double_value": 3.14159,
    "string_value": "hello"
}
```

### Math Types ✅ Implemented

```json
{
    "vec2": [1.0, 2.0],
    "vec3": [1.0, 2.0, 3.0],
    "vec4": [1.0, 2.0, 3.0, 4.0],
    "quat": [0.0, 0.0, 0.7071, 0.7071]
}
```

### References — Partial ⚠️

Entity references are supported (serialized as ID + generation):

```json
{
    "owner_entity": {
        "id": 123,
        "generation": 0
    }
}
```

**Planned for future phases (not in Phase 3b-3.2b MVP):**

```json
{
    "asset_ref": "res://mesh/cube.uzmesh",      // Phase 3b-3.2c
    "behavior_tree": "ai/goblin.uzbt"           // Phase 3b-3.2c
}
```

### Arrays and Maps 📋 Planned

```json
{
    "inventory": ["item_001", "item_002", "item_003"],  // Phase 3b-3.4
    "stats": {
        "fire_resistance": 0.2,
        "cold_resistance": 0.1
    }
}
```

## Scene Loading ✅ Implemented

When a scene is loaded, components are deserialized from JSON:

```cpp
// In SceneLoader (Phase 3b-3.2b MVP)
bool SceneLoader::Load(const std::string& path, World& world) {
    auto json = LoadJSON(path);

    for (auto& entity_json : json["entities"]) {
        // Create entity
        auto entity = world.CreateEntity();

        // Iterate components
        for (auto& comp_json : entity_json["components"]) {
            std::string component_type = comp_json["type"].as<std::string>();

            // Look up class metadata
            auto& registry = uz::ClassRegistry::Get();
            const uz::ClassInfo* class_info = registry.FindClass(component_type);

            if (!class_info) {
                UZ_LOG(Scene, Warning, "Unknown component type: {}", component_type);
                continue;
            }

            // Allocate memory for component instance
            void* comp_instance = operator new(class_info->instance_size);

            // Call constructor via factory function
            if (class_info->factory) {
                class_info->factory(comp_instance);
            }

            // Deserialize properties from JSON
            uz::PropertySerializer::DeserializeFromJSON(
                comp_instance,
                *class_info,
                comp_json["properties"]
            );

            // Attach component to entity
            world.AddComponent(entity, class_info, comp_instance);
        }
    }

    return true;
}
```

## Scene Saving ✅ Implemented

When saving, components are serialized to JSON:

```cpp
// In SceneSaver (Phase 3b-3.2b MVP)
bool SceneSaver::Save(const std::string& path, const World& world) {
    json scene_json;
    scene_json["uz_format"] = "scene";
    scene_json["uz_version"] = 1;

    auto& entities_array = scene_json["entities"];

    // Iterate all entities
    world.ForEachEntity([&entities_array](Entity entity) {
        json entity_json;
        entity_json["id"] = entity.id;
        entity_json["name"] = world.GetEntityName(entity);

        auto& components_array = entity_json["components"];

        // Iterate all components on entity
        world.ForEachComponent(entity,
            [&components_array](const uz::ClassInfo& class_info, void* comp_instance) {
                json comp_json;
                comp_json["type"] = class_info.name;

                // Serialize all non-transient properties to JSON
                comp_json["properties"] = uz::PropertySerializer::SerializeToJSON(
                    comp_instance,
                    class_info
                );

                components_array.push_back(comp_json);
            }
        );

        entities_array.push_back(entity_json);
    });

    return WriteJSON(path, scene_json);
}
```

**Note:** Phase 3b-3.2b MVP serializes all non-transient properties.
Phase 3b-3.4 will add delta encoding (only serialize non-default values).

## Default Value Diffing 📋 Planned (Phase 3b-3.4)

**Planned feature:** Only serialize properties that differ from their default values (reduces file size and makes diffs readable).

```cpp
// Planned for Phase 3b-3.4:
json SerializeDelta(const void* obj, const ClassInfo& class_info) {
    json delta;

    class_info.ForEachProperty([obj, &delta](const PropertyInfo& prop) {
        if (uz::HasFlag(prop.flags, uz::PropertyFlags::Transient)) {
            return;  // Skip transient properties
        }

        // Get default value from component initialization
        // (stored during component construction)
        // const void* default_ptr = GetDefaultValue(class_info, prop);

        // Compare current value to default
        // if (std::memcmp(current_ptr, default_ptr, prop.size) != 0) {
        //     delta[prop.name] = PropertyToJSON(obj, prop);
        // }
    });

    return delta;
}
```

**Benefits:**
- Smaller .uzscene files (only non-default values serialized)
- Human-readable diffs (fewer unchanged properties)
- Automatic schema evolution (new properties with defaults not required)

## Schema Versioning 📋 Planned (Phase 3b-3.4)

Components can evolve — the serializer handles old/new schemas gracefully:

```cpp
struct Enemy : public Component {
    UZ_COMPONENT(Enemy);

    float health = 30.0f;    // Original property
    float speed = 2.0f;      // Original property
    int level = 1;           // NEW property added in schema v2

    UZ_PROPERTIES_BEGIN(Enemy)
        UZ_PROP(float, health, "Hit points")
        UZ_PROP(float, speed, "Movement speed")
        UZ_PROP(int, level, "Character level")
    UZ_PROPERTIES_END()
};

// Deserialization behavior:
// Old .uzscene: { "health": 50.0, "speed": 3.0 }
//   Result: health=50.0, speed=3.0, level=1 (default)
// New .uzscene: { "health": 50.0, "speed": 3.0, "level": 5 }
//   Result: health=50.0, speed=3.0, level=5
// No errors or data loss
```

**Planned migration framework (Phase 3b-3.4):**

```cpp
// Register migration for renamed/removed properties
uz::PropertyMigration::Register("Enemy", 1, 2, [](json& old_data) {
    // V1 → V2: 'speed' renamed to 'movement_speed'
    if (old_data.contains("speed")) {
        old_data["movement_speed"] = old_data["speed"];
        old_data.erase("speed");
    }
});
```

## Transient vs SaveGame Properties ✅ Implemented

Properties can be marked with flags to control serialization behavior:

```cpp
struct Player : public Component {
    UZ_COMPONENT(Player);

    float health = 100.0f;
    int level = 1;
    bool is_aiming = false;
    int debug_index = 0;
    Entity target_entity{};

    UZ_PROPERTIES_BEGIN(Player)
        // Serialized in .uzscene files (JSON)
        // Planned: will be saved to .uzpak save files (Phase 3b-3.2c)
        UZ_PROP_EX(float, health, "Current health", "Stats", uz::PropertyFlags::SaveGame)
        UZ_PROP_EX(int, level, "Character level", "Stats", uz::PropertyFlags::SaveGame)

        // Serialized in .uzscene only (runtime data, editor state)
        UZ_PROP(bool, is_aiming, "Is player aiming")

        // NOT serialized anywhere (debug/temporary data)
        UZ_PROP_EX(int, debug_index, "Debug counter", "", uz::PropertyFlags::Transient)

        // Replicated over network (Phase 5+)
        UZ_PROP_EX(Entity, target_entity, "Current target", "Combat", uz::PropertyFlags::Replicated)
    UZ_PROPERTIES_END()
};
```

**Property Flag Semantics:**
- `SaveGame`: Serialized to .uzscene JSON (always) and .uzpak save files (when Phase 3b-3.2c is complete)
- `Transient`: Completely excluded from all serialization (runtime-only)
- `Replicated`: Will be replicated over network in Phase 5+
- `ReadOnly`: Cannot be edited in editor (Phase 3b-3.3)
- `Hidden`: Not shown in editor UI (Phase 3b-3.3)

## Comparison

| Feature | UE5 Serialization | JSON.NET | Protocol Buffers | **UzEngine** |
|---------|------------------|----------|------------------|--------------|
| Format | Binary (Unreal) | JSON | Binary | ✅ JSON (3b-3.2b), 📋 msgpack planned |
| Human-readable | No | Yes | No | ✅ Yes (JSON) |
| Version aware | Yes | Manual | Yes | 📋 Yes (planned 3b-3.4) |
| Default diffing | Partial (CDO) | No | Manual | 📋 Yes (delta, planned 3b-3.4) |
| Extensible | Limited | Yes | Yes | ✅ Yes (future migrations) |
| Performance | Fast (binary) | Moderate (JSON) | Fast | ✅ Fast (JSON), 📋 msgpack fast (3b-3.2c) |
| Type-safe | Yes | Weak | Yes | ✅ Yes (reflection) |

## Implementation Status

**Phase 3b-3.2b MVP** ✅ Completed:
- JSON serialization via PropertySerializer::SerializeToJSON()
- JSON deserialization via PropertySerializer::DeserializeFromJSON()
- Support for all basic types: bool, int, float, double, std::string
- Support for all math types: Vec2, Vec3, Vec4, Quat
- Support for Entity references (id + generation)
- Transient flag support (skipped during serialization)
- Missing JSON keys handled gracefully (retain default/current value)
- Type mismatch detection (logged as warning)

**Future Phases:**
- 3b-3.2c: msgpack binary format, SaveGame flag support for save files
- 3b-3.2d: Enum types, AssetRef support
- 3b-3.4: Delta encoding (only non-default values), schema versioning, migration framework
- Phase 5+: Streaming serialization, incremental saves, cloud sync, network replication
