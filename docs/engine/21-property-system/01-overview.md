# Property System & Reflection

> **Status**: Planned (Phase 2)
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

## UZ_PROPERTY — Field Declaration

```cpp
/// Component with reflected properties.
/// UZ_PROPERTY generates: field storage, name string, default value,
/// tooltip, type info, and enables iteration over all properties at runtime.
struct Enemy {
    UZ_COMPONENT(Enemy);

    UZ_PROPERTY(float, speed, 2.0f, "Movement speed in m/s");
    UZ_PROPERTY(float, health, 30.0f, "Current health points");
    UZ_PROPERTY(int, path_index, 0, "Current waypoint index");
};
```

## Runtime Property Iteration

```cpp
uz::ClassRegistry::ForEachProperty<Enemy>([](const uz::PropertyInfo& prop) {
    // prop.name     = "speed"
    // prop.type     = uz::PropertyType::Float
    // prop.offset   = offsetof(Enemy, speed)
    // prop.default_value = 2.0f
    // prop.tooltip  = "Movement speed in m/s"
});
```

## PropertyInfo Structure

```cpp
struct PropertyInfo {
    const char* name;           /// Field name
    PropertyType type;          /// Float, Int, Bool, Vec3, String, EntityRef, Asset, Enum
    size_t offset;              /// Byte offset from struct start
    size_t size;                /// sizeof(field)
    const char* tooltip;        /// Human-readable description
    const void* default_value;  /// Pointer to default value (for reset)

    const char* category = "";  /// Editor grouping (optional)
    float min = -FLT_MAX;      /// Numeric range min (optional)
    float max = FLT_MAX;       /// Numeric range max (optional)

    PropertyFlags flags = PropertyFlags::None;
};

enum class PropertyFlags : uint32_t {
    None        = 0,
    ReadOnly    = 1 << 0,   /// Cannot be edited in inspector
    Hidden      = 1 << 1,   /// Not shown in inspector
    Replicated  = 1 << 2,   /// Sent over network (future)
    SaveGame    = 1 << 3,   /// Included in save files
    Transient   = 1 << 4,   /// Not serialized to .uzscene
};
```

## Advanced Property Types

```cpp
struct Weapon {
    UZ_COMPONENT(Weapon);

    // Basic types
    UZ_PROPERTY(float, damage, 25.0f, "Damage per hit");
    UZ_PROPERTY(int, ammo, 30, "Current ammo count");
    UZ_PROPERTY(bool, automatic, false, "Full-auto fire mode");

    // Math types
    UZ_PROPERTY(Vec3, muzzle_offset, Vec3(0, 0, 1), "Local-space muzzle position");
    UZ_PROPERTY(Color, tracer_color, Color::Yellow(), "Bullet tracer color");

    // Enum types (auto-generates dropdown in editor)
    UZ_PROPERTY(WeaponType, type, WeaponType::Rifle, "Weapon category");

    // Asset references (shows asset picker in editor)
    UZ_PROPERTY(uz::AssetRef<uz::Mesh>, mesh, {}, "Weapon mesh");
    UZ_PROPERTY(uz::AssetRef<uz::Sound>, fire_sound, {}, "Fire sound effect");

    // Entity references (shows entity picker in editor)
    UZ_PROPERTY(uz::EntityRef, owner, {}, "Entity that owns this weapon");

    // Range-constrained (editor shows slider)
    UZ_PROPERTY_RANGE(float, spread, 0.05f, 0.0f, 1.0f, "Bullet spread angle");
};
```

## Consumers

The Property System is consumed by multiple engine systems:

| Consumer | How It Uses Properties |
|----------|----------------------|
| **Scene Loader** | Deserializes JSON → component fields via `offset` + `type` |
| **Scene Saver** | Serializes component fields → JSON via iteration |
| **Editor Inspector** | Displays fields with type-appropriate widgets (slider, color picker, asset browser) |
| **Save System** | Serializes fields with `SaveGame` flag to msgpack |
| **Networking** | Replicates fields with `Replicated` flag (future) |
| **Data Binding** | UI observes property changes for reactive updates |
| **Hot-Reload** | Preserves property values across DLL reload by name+type matching |

## Comparison

| Engine | Mechanism | Weight | GC Required |
|--------|-----------|--------|-------------|
| Unreal 5 | `UPROPERTY()` on `UObject` (mandatory base) | Heavy | Yes |
| Unity | `[SerializeField]` (C# reflection) | Medium | Yes (C# GC) |
| Godot | `@export` + Variant system | Medium | RefCounted |
| **UzEngine** | `UZ_PROPERTY()` (opt-in macro, no base class) | **Light** | **No** |

## Implementation Phase

**MVP Phase 2** — required for editor inspector and scene serialization.
