# Property Serialization — JSON and Binary

> **Status**: Planned (Phase 2–3)
> **Source**: `engine/property/serialization/`
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

/// Serializes/deserializes properties via reflection
class PropertySerializer {
public:
    // ─────────────────────────────────────────────────────────────
    // JSON Serialization (scenes, debug output)
    // ─────────────────────────────────────────────────────────────

    /// Serialize component to JSON object
    static json SerializeToJSON(
        const void* component_instance,
        const ClassInfo& class_info
    );

    /// Deserialize component from JSON
    static bool DeserializeFromJSON(
        void* component_instance,
        const ClassInfo& class_info,
        const json& json_data
    );

    // ─────────────────────────────────────────────────────────────
    // Binary Serialization (saves, network)
    // ─────────────────────────────────────────────────────────────

    /// Serialize component to msgpack binary
    static std::vector<uint8_t> SerializeToMsgpack(
        const void* component_instance,
        const ClassInfo& class_info
    );

    /// Deserialize component from msgpack binary
    static bool DeserializeFromMsgpack(
        void* component_instance,
        const ClassInfo& class_info,
        const std::vector<uint8_t>& data
    );

    // ─────────────────────────────────────────────────────────────
    // Delta Encoding (for replication and patch efficiency)
    // ─────────────────────────────────────────────────────────────

    /// Only serialize properties that differ from defaults
    /// (reduces file size and makes diffs readable)
    static json SerializeDelta(
        const void* component_instance,
        const ClassInfo& class_info
    );

    /// Apply delta (missing properties use defaults)
    static bool DeserializeDelta(
        void* component_instance,
        const ClassInfo& class_info,
        const json& delta
    );

private:
    static json PropertyToJSON(const void* obj, const PropertyInfo& prop);
    static bool PropertyFromJSON(void* obj, const PropertyInfo& prop, const json& value);
};

}  // namespace uz
```

## Type Encoding

### Basic Types

```json
{
    "bool_value": true,
    "int_value": 42,
    "float_value": 3.14,
    "string_value": "hello"
}
```

### Math Types

```json
{
    "vec2": [1.0, 2.0],
    "vec3": [1.0, 2.0, 3.0],
    "vec4": [1.0, 2.0, 3.0, 4.0],
    "quat": [0.0, 0.0, 0.7071, 0.7071],
    "color": [1.0, 0.0, 0.0, 1.0]
}
```

### References

```json
{
    "entity_ref": 123,           // Entity ID
    "asset_ref": "res://mesh/cube.uzmesh",
    "behavior_tree": "ai/goblin.uzbt"
}
```

### Arrays and Maps

```json
{
    "inventory": ["item_001", "item_002", "item_003"],
    "stats": {
        "fire_resistance": 0.2,
        "cold_resistance": 0.1
    }
}
```

## Scene Loading

When a scene is loaded, components are deserialized:

```cpp
// In SceneLoader
bool SceneLoader::Load(const std::string& path, World& world) {
    auto json = LoadJSON(path);

    for (auto& entity_json : json["entities"]) {
        auto entity = world.Create();
        world.SetName(entity, entity_json["name"].as<std::string>());

        for (auto& comp_json : entity_json["components"]) {
            std::string component_type = comp_json["type"].as<std::string>();
            auto class_info = uz::ClassRegistry::FindClass(component_type);

            if (!class_info) {
                UZ_LOG(Scene, Warning, "Unknown component type: {}", component_type);
                continue;
            }

            // Create component
            void* comp_instance = AllocateComponent(class_info);

            // Deserialize properties from JSON
            PropertySerializer::DeserializeFromJSON(
                comp_instance,
                *class_info,
                comp_json["properties"]
            );

            world.AttachComponent(entity, comp_instance, class_info);
        }
    }

    return true;
}
```

## Scene Saving

When saving, components are serialized to JSON:

```cpp
// In SceneSaver
bool SceneSaver::Save(const std::string& path, const World& world) {
    json scene_json;
    scene_json["uz_format"] = "scene";
    scene_json["uz_version"] = 1;

    auto& entities_array = scene_json["entities"];

    world.ForEachEntity([&entities_array](Entity entity) {
        json entity_json;
        entity_json["id"] = entity.id;
        entity_json["name"] = entity.name;

        auto& components_array = entity_json["components"];

        world.ForEachComponent(entity, [&components_array](void* comp, const ClassInfo& class_info) {
            json comp_json;
            comp_json["type"] = class_info.name;
            comp_json["properties"] = PropertySerializer::SerializeDelta(comp, class_info);

            components_array.push_back(comp_json);
        });

        entities_array.push_back(entity_json);
    });

    return WriteJSON(path, scene_json);
}
```

## Default Value Diffing

Only properties that differ from their default value are serialized (reduces file size):

```cpp
json SerializeDelta(const void* obj, const ClassInfo& class_info) {
    json delta;

    class_info.ForEachProperty([obj, &delta](const PropertyInfo& prop) {
        if (prop.flags & PropertyFlags::Transient) {
            return;  // Skip transient properties
        }

        // Compare current value to default
        const void* current_ptr = reinterpret_cast<const uint8_t*>(obj) + prop.offset;
        bool is_default = std::memcmp(current_ptr, prop.default_value, prop.size) == 0;

        if (!is_default) {
            // Only serialize non-default values
            delta[prop.name] = PropertyToJSON(obj, prop);
        }
    });

    return delta;
}
```

## Schema Versioning

Components can evolve — serializer handles old/new schemas:

```cpp
struct Enemy {
    UZ_COMPONENT(Enemy);

    static constexpr int SCHEMA_VERSION = 3;  // Tracks changes

    UZ_PROPERTY(float, health, 30.0f, "");
    UZ_PROPERTY(float, speed, 2.0f, "");
    UZ_PROPERTY(int, level, 1, "");
    // NEW in v3: was added in a previous version
};

// Old save has only v2 properties (no 'level')
// Deserializer:
// 1. Read 'health' and 'speed' from JSON
// 2. 'level' not in JSON → use default (1)
// 3. No errors or data loss
```

## Migration Example

If a property is removed or renamed, migration can be registered:

```cpp
// Register migration for Enemy component
uz::PropertyMigration::Register("Enemy", 2, 3, [](json& old_data) {
    // V2 → V3: 'speed' renamed to 'movement_speed'
    if (old_data.contains("speed")) {
        old_data["movement_speed"] = old_data["speed"];
        old_data.erase("speed");
    }
});
```

## Transient vs SaveGame Properties

Properties can be marked to control serialization:

```cpp
struct Player {
    UZ_COMPONENT(Player);

    // Saved to .uzscene and save files
    UZ_PROPERTY(float, health, 100.0f, "", PropertyFlags::SaveGame);
    UZ_PROPERTY(int, level, 1, "", PropertyFlags::SaveGame);

    // Saved to .uzscene but NOT save files (runtime data)
    UZ_PROPERTY(bool, is_aiming, false, "", PropertyFlags::None);

    // NOT saved anywhere (debug/temporary)
    UZ_PROPERTY(int, debug_index, 0, "", PropertyFlags::Transient);
};
```

## Comparison

| Feature | UE5 Serialization | JSON.NET | Protocol Buffers | **UzEngine** |
|---------|------------------|----------|------------------|--------------|
| Format | Binary (Unreal) | JSON | Binary | JSON + msgpack |
| Human-readable | No | Yes | No | Yes (JSON) |
| Version aware | Yes (versioning) | Manual | Yes | Yes (schema versions) |
| Default diffing | Partial (CDO) | No | Manual | Yes (delta) |
| Extensible | Limited | Yes | Yes | Yes (migrations) |
| Performance | Fast (binary) | Moderate (JSON) | Fast | Fast (msgpack) |
| Type-safe | Yes | Weak | Yes | Yes (reflection) |

## Implementation Phase

- **Phase 2**: JSON serialization, basic types, component → JSON/JSON → component
- **Phase 3**: msgpack binary format, delta encoding, transient property filtering
- **Phase 4**: Schema versioning, migration framework, scene versioning
- **Phase 5**: Streaming serialization, incremental save chunks, cloud sync
