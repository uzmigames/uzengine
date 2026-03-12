# Type Reflection & Runtime Metadata

> **Status**: Planned (Phase 2)
> **Source**: `engine/property/reflection/`
> **References**: UE5 Reflection system (clean-room), std::type_info, Boost.Reflect

## Purpose

Runtime type information (RTTI) for C++ types. Enables dynamic field iteration, type-safe property access, and runtime type validation. Only types annotated with `UZ_PROPERTY` macros register — no universal base class required.

## Type Registry

```cpp
namespace uz {

/// Central registry of all reflected types
class ClassRegistry {
public:
    /// Register a reflected type (called by UZ_PROPERTY macro)
    static void Register(StringView type_name, ClassInfo* class_info);

    /// Find type by name
    static ClassInfo* FindClass(StringView type_name);

    /// Iterate all registered types
    static void ForEachClass(std::function<void(StringView name, ClassInfo*)> fn);

    /// Check if type is registered
    static bool IsRegistered(StringView type_name);

private:
    static std::unordered_map<std::string, ClassInfo*> registry_;
};

}  // namespace uz
```

## ClassInfo Structure

```cpp
namespace uz {

/// Metadata about a single reflected class
struct ClassInfo {
    std::string name;                          ///< Type name (e.g., "Enemy")
    std::string display_name;                  ///< Human-readable (e.g., "Enemy Component")
    std::vector<std::unique_ptr<PropertyInfo>> properties;  ///< All properties

    /// Iterate properties
    void ForEachProperty(std::function<void(const PropertyInfo&)> fn) const {
        for (auto& prop : properties) {
            fn(*prop);
        }
    }

    /// Find property by name
    const PropertyInfo* FindProperty(StringView prop_name) const {
        for (auto& prop : properties) {
            if (prop->name == prop_name) {
                return prop.get();
            }
        }
        return nullptr;
    }

    /// Property count
    int PropertyCount() const { return properties.size(); }
};

}  // namespace uz
```

## PropertyInfo Structure

```cpp
namespace uz {

/// Metadata about a single property
struct PropertyInfo {
    std::string name;               ///< Property name
    PropertyType type;              ///< int, float, bool, Vec3, String, EntityRef, etc.
    size_t offset;                  ///< Byte offset from struct start
    size_t size;                    ///< sizeof(field)

    std::string display_name;       ///< Human-readable property name
    std::string description;        ///< Tooltip/help text
    std::string category;           ///< Editor grouping (e.g., "Movement", "Combat")

    // Constraints (for editor sliders/pickers)
    float min_value = -FLT_MAX;    ///< Min value (for numeric types)
    float max_value = FLT_MAX;     ///< Max value (for numeric types)
    float step = 0.0f;             ///< Slider step (0 = auto)

    PropertyFlags flags = PropertyFlags::None;
    const void* default_value = nullptr;  ///< Pointer to default value
};

enum class PropertyType : uint32_t {
    None,
    Bool,
    Int,
    Float,
    Double,
    String,
    Vec2,
    Vec3,
    Vec4,
    Quat,
    Color,
    Entity,        ///< Entity reference
    AssetRef,      ///< Asset reference (with template type)
    Enum,          ///< Enum type
    Struct,        ///< Nested struct
    Array,         ///< Array of elements
    Map,           ///< Key-value pairs
};

enum class PropertyFlags : uint32_t {
    None        = 0,
    ReadOnly    = 1 << 0,   ///< Cannot be edited
    Hidden      = 1 << 1,   ///< Not shown in inspector
    Replicated  = 1 << 2,   ///< Sent over network (Phase 5+)
    SaveGame    = 1 << 3,   ///< Persisted to save files
    Transient   = 1 << 4,   ///< Runtime-only, not serialized
};

}  // namespace uz
```

## UZ_PROPERTY Macro

The macro registers properties at class definition time:

```cpp
#define UZ_PROPERTY(Type, Name, DefaultValue, Description) \
    Type Name = DefaultValue; \
    static constexpr const char* __uz_prop_##Name##_name = #Name; \
    static constexpr PropertyInfo __uz_prop_##Name##_info = { \
        /* name */ #Name, \
        /* type */ GetPropertyType<Type>(), \
        /* offset */ offsetof(ThisClass, Name), \
        /* size */ sizeof(Type), \
        /* description */ Description, \
        /* default */ &DefaultValue \
    };

// Usage:
struct Enemy {
    UZ_COMPONENT(Enemy);

    UZ_PROPERTY(float, speed, 2.0f, "Movement speed in m/s");
    UZ_PROPERTY(int, health, 30, "Current health");
    UZ_PROPERTY(Vec3, patrol_point, Vec3::Zero(), "Patrol destination");
};
```

## Field Iteration at Runtime

Iterate all properties of a type without knowing the type in advance:

```cpp
// Get ClassInfo for a type
ClassInfo* enemy_class = uz::ClassRegistry::FindClass("Enemy");

if (enemy_class) {
    // Iterate properties
    enemy_class->ForEachProperty([](const PropertyInfo& prop) {
        std::cout << "Property: " << prop.name
                  << ", Type: " << (int)prop.type
                  << ", Size: " << prop.size
                  << " bytes\n";
    });
}
```

## Type-Safe Property Access

Read/write properties by name with type checking:

```cpp
namespace uz {

/// Type-safe property getter/setter
template<typename T>
class PropertyAccessor {
public:
    /// Get property value from object
    static T Get(const void* obj, const PropertyInfo& prop) {
        if (prop.type != GetPropertyType<T>()) {
            throw std::runtime_error("Type mismatch");
        }
        return *reinterpret_cast<const T*>(
            reinterpret_cast<const uint8_t*>(obj) + prop.offset
        );
    }

    /// Set property value on object
    static void Set(void* obj, const PropertyInfo& prop, const T& value) {
        if (prop.type != GetPropertyType<T>()) {
            throw std::runtime_error("Type mismatch");
        }
        if (prop.flags & PropertyFlags::ReadOnly) {
            throw std::runtime_error("Property is read-only");
        }
        *reinterpret_cast<T*>(
            reinterpret_cast<uint8_t*>(obj) + prop.offset
        ) = value;
    }
};

// Usage:
Enemy enemy;
auto speed_prop = enemy_class->FindProperty("speed");
float current_speed = PropertyAccessor<float>::Get(&enemy, *speed_prop);
PropertyAccessor<float>::Set(&enemy, *speed_prop, current_speed * 1.5f);

}  // namespace uz
```

## Default Values

Each property stores a pointer to its default value:

```cpp
void ResetToDefaults(void* obj, const ClassInfo& class_info) {
    class_info.ForEachProperty([obj](const PropertyInfo& prop) {
        if (!prop.default_value) return;

        // Copy default value to object
        std::memcpy(
            reinterpret_cast<uint8_t*>(obj) + prop.offset,
            prop.default_value,
            prop.size
        );
    });
}
```

## Component Registry Extension

Components automatically register their properties when `UZ_COMPONENT(Name)` is called:

```cpp
#define UZ_COMPONENT(Name) \
    static_assert(std::is_base_of_v<uz::Component, Name>); \
    static bool __uz_component_##Name##_registered = ([]() { \
        uz::ClassRegistry::Register(#Name, CreateClassInfo<Name>()); \
        return true; \
    }());

// When Enemy component is defined, it auto-registers in ClassRegistry
```

## Advanced: Nested Structs

Structs can contain other structs — all properties are flattened:

```cpp
struct Transform {
    Vec3 position;
    Quat rotation;
    Vec3 scale;
};

struct GameObject {
    UZ_COMPONENT(GameObject);

    UZ_PROPERTY(std::string, name, "Object", "Display name");
    UZ_PROPERTY(Transform, transform, Transform{}, "Transform data");  // Nested
};

// Iteration shows:
// - name: string
// - transform.position: Vec3
// - transform.rotation: Quat
// - transform.scale: Vec3
```

## Editor Bindings

Inspector UI uses reflection to generate controls:

```cpp
// Pseudocode: editor inspector generation
void GenerateInspectorUI(const ClassInfo& class_info, void* obj) {
    ImGui::Columns(2);

    class_info.ForEachProperty([obj](const PropertyInfo& prop) {
        // Column 1: property name
        ImGui::Text("%s", prop.name);
        ImGui::NextColumn();

        // Column 2: type-appropriate widget
        switch (prop.type) {
            case PropertyType::Float: {
                float value = PropertyAccessor<float>::Get(obj, prop);
                if (ImGui::SliderFloat("", &value, prop.min_value, prop.max_value)) {
                    PropertyAccessor<float>::Set(obj, prop, value);
                }
                break;
            }
            case PropertyType::Bool: {
                bool value = PropertyAccessor<bool>::Get(obj, prop);
                if (ImGui::Checkbox("", &value)) {
                    PropertyAccessor<bool>::Set(obj, prop, value);
                }
                break;
            }
            case PropertyType::Vec3: {
                Vec3 value = PropertyAccessor<Vec3>::Get(obj, prop);
                if (ImGui::DragFloat3("", &value.x)) {
                    PropertyAccessor<Vec3>::Set(obj, prop, value);
                }
                break;
            }
            // ... other types
        }
        ImGui::NextColumn();
    });

    ImGui::Columns(1);
}
```

## Comparison

| Feature | UE5 Reflection | C++ std::type_info | Qt Meta-Object | **UzEngine** |
|---------|----------------|-------------------|-----------------|--------------|
| Opt-in | No (UObject required) | No (all types) | Yes (Q_OBJECT) | Yes (macros) |
| Field names | Yes (PropertyInfo) | No | Yes | Yes |
| Offsets | Yes | No | No | Yes |
| Default values | Yes (CDO) | No | Yes | Yes |
| Editor metadata | Yes (extensive) | No | Yes | Yes (categories, ranges, flags) |
| Property iteration | Yes | No | Yes | Yes |
| Type-safe access | Weak (property handles) | No | Yes (QVariant) | Yes (templates) |
| Runtime overhead | Moderate | Minimal | Moderate | Minimal (register only) |

## Implementation Phase

- **Phase 2**: Core reflection (ClassRegistry, PropertyInfo), UZ_PROPERTY macro, property iteration, default values
- **Phase 3**: Nested struct support, property constraints (min/max), property flags (SaveGame, ReadOnly)
- **Phase 4**: Editor UI generation from reflection, type-safe accessors, hot-reload property preservation
- **Phase 5**: Network replication via reflection, property change tracking
