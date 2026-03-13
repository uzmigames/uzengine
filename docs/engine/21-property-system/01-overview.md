# Property System & Reflection

> **Status**: Done (Phase 3b-3.2b MVP)
> **Source**: `engine/property/`
> **References**: UE5 `UPROPERTY()` + UObject (clean-room), Godot ClassDB

## Purpose

Runtime type information for: editor inspectors (display and edit fields), serialization (save/load scenes), networking (replicate state), and data binding (UI ↔ game state). Unlike Unreal's universal `UObject`, UzEngine's reflection is **opt-in via macros** — only types that need it pay for it.

## Design — No Universal Base Object

```
UObject Feature     │ UzEngine Equivalent
────────────────────┼──────────────────────────────────────
Reflection          │ ClassRegistry + UZ_REFLECT macro
Serialization       │ UZ_COMPONENT auto-serialization
GC                  │ Entity generation check (no GC needed)
CDO                 │ .uzscene default values
Property system     │ UZ_PROPERTY macro (field enumeration)
Networking          │ UZ_REPLICATED macro (future)
Editor metadata     │ UZ_PROPERTY annotations
```

## UZ_COMPONENT — Component Registration

```cpp
/// Component with reflected properties.
/// UZ_COMPONENT registers the component in ClassRegistry and enables
/// property reflection, serialization, and inspector binding.
struct Enemy : public Component {
    UZ_COMPONENT(Enemy);

    float speed = 2.0f;
    float health = 100.0f;
    Vec3 target{};

    /// Declarative property registration (called automatically by UZ_COMPONENT)
    UZ_PROPERTIES_BEGIN(Enemy)
        UZ_PROP(float, speed, "Movement speed in m/s")
        UZ_PROP(float, health, "Current health points")
        UZ_PROP(Vec3, target, "Target patrol location")
    UZ_PROPERTIES_END()
};
```

## Runtime Property Iteration

```cpp
// Get ClassInfo for Enemy component
auto& class_registry = uz::ClassRegistry::Get();
const uz::ClassInfo* enemy_class = class_registry.FindClass("Enemy");

if (enemy_class) {
    enemy_class->ForEachProperty([](const uz::PropertyInfo& prop) {
        std::cout << "Property: " << prop.name
                  << ", Type: " << uz::PropertyTypeName(prop.type)
                  << ", Offset: " << prop.offset
                  << ", Size: " << prop.size << " bytes\n";
    });
}
```

## PropertyInfo Structure

```cpp
struct PropertyInfo {
    const char* name;                      /// Field name (string literal)
    PropertyType type;                     /// Bool, Int, Float, Double, String, Vec2, Vec3, Vec4, Quat, Entity
    size_t offset;                         /// Byte offset from struct start (offsetof)
    size_t size;                           /// sizeof(field) in bytes
    const char* description;               /// Human-readable description for editors
    const char* category;                  /// Editor grouping (e.g., "Movement", "Combat")
    PropertyFlags flags;                   /// Behavioral flags
};

enum class PropertyType : uint8_t {
    None = 0,      /// Sentinel / uninitialized
    Bool,          /// bool
    Int,           /// int (signed 32-bit)
    Float,         /// float (32-bit IEEE 754)
    Double,        /// double (64-bit IEEE 754)
    String,        /// std::string
    Vec2,          /// uz::Vec2
    Vec3,          /// uz::Vec3
    Vec4,          /// uz::Vec4
    Quat,          /// uz::Quat
    Entity,        /// uz::Entity (generational handle)
};

enum class PropertyFlags : uint8_t {
    None       = 0,        /// No special behavior
    ReadOnly   = 1U << 0,  /// Cannot be edited in inspector
    Hidden     = 1U << 1,  /// Hidden from inspector UI
    Replicated = 1U << 2,  /// Replicated over network (future)
    SaveGame   = 1U << 3,  /// Included in save-game serialization
    Transient  = 1U << 4,  /// Excluded from all serialization
};
```

## Supported Property Types

The property system currently supports these C++ types (Phase 3b-3.2b MVP):

```cpp
struct Weapon : public Component {
    UZ_COMPONENT(Weapon);

    float damage = 25.0f;
    int ammo = 30;
    bool automatic = false;
    Vec3 muzzle_offset{0, 0, 1};
    Quat barrel_rotation{};
    std::string name;
    Entity owner{};

    UZ_PROPERTIES_BEGIN(Weapon)
        UZ_PROP(float, damage, "Damage per hit")
        UZ_PROP(int, ammo, "Current ammo count")
        UZ_PROP(bool, automatic, "Full-auto fire mode")
        UZ_PROP(Vec3, muzzle_offset, "Local-space muzzle position")
        UZ_PROP(Quat, barrel_rotation, "Barrel rotation offset")
        UZ_PROP(std::string, name, "Weapon name")
        UZ_PROP_EX(Entity, owner, "Entity that owns this weapon", "References", PropertyFlags::Replicated)
    UZ_PROPERTIES_END()
};
```

**Planned for Future Phases:**
- AssetRef (asset references) — Phase 3b-3.2c
- Enum types (auto-generates dropdown) — Phase 3b-3.2d
- Min/max constraints (slider editor) — Phase 3b-3.3
- Array/container types — Phase 3b-3.3
- Custom struct serialization — Phase 3b-3.4

## Consumers

The Property System is consumed by multiple engine systems:

| Consumer | Status | How It Uses Properties |
|----------|--------|----------------------|
| **JSON Serialization** | ✅ Implemented | Serializes/deserializes component fields ↔ JSON via ClassInfo metadata |
| **Scene Loader** | ✅ Implemented | Loads `.uzscene` files, deserializes JSON → component fields |
| **Scene Saver** | ✅ Implemented | Saves `.uzscene` files, serializes component fields → JSON |
| **Editor Inspector** | 📋 Planned (3b-3.3) | Generates UI from ClassInfo, type-appropriate widgets (slider, color picker) |
| **Save System** | 📋 Planned (3b-3.2c) | Serializes fields with `SaveGame` flag to msgpack format |
| **Hot-Reload** | 📋 Planned (3b-3.4) | Preserves property values across DLL reload by name+type matching |
| **Networking** | 📋 Planned (Phase 5+) | Replicates fields with `Replicated` flag |

## Comparison

| Engine | Mechanism | Weight | GC Required | Opt-In |
|--------|-----------|--------|-------------|--------|
| Unreal 5 | `UPROPERTY()` on `UObject` (mandatory base) | Heavy | Yes | No |
| Unity | `[SerializeField]` (C# reflection) | Medium | Yes (C# GC) | Yes |
| Godot | `@export` + Variant system | Medium | RefCounted | Yes |
| **UzEngine** | `UZ_COMPONENT()` + `UZ_PROP()` macros (opt-in) | **Light** | **No** | **Yes** |

## Implementation Status

**Phase 3b-3.2b MVP** ✅ Completed:
- ClassRegistry singleton with Register(), FindClass(), IsRegistered(), ForEachClass()
- PropertyInfo struct with name, type, offset, size, description, category, flags
- PropertyType enum: Bool, Int, Float, Double, String, Vec2, Vec3, Vec4, Quat, Entity
- PropertyFlags enum: ReadOnly, Hidden, Replicated, SaveGame, Transient
- UZ_COMPONENT() macro for auto-registration and factory function
- UZ_PROPERTIES_BEGIN/END, UZ_PROP, UZ_PROP_EX declarative macros
- PropertySerializer with JSON serialization/deserialization
- Full type-safe property access via GetPropertyType<T>() specializations

**Future Phases:**
- 3b-3.2c: Binary msgpack serialization, SaveGame flag support
- 3b-3.2d: Enum types, ranges/constraints (min/max sliders)
- 3b-3.3: Editor UI generation, reactive binding, hot-reload integration
- 3b-3.4: Custom struct nesting, migration/versioning framework
- Phase 5+: Network replication, delta encoding, change tracking
