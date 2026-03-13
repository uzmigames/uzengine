# Type Reflection & Runtime Metadata

> **Status**: Done (Phase 3b-3.2b MVP)
> **Source**: `engine/property/`
> **References**: UE5 Reflection system (clean-room), std::type_info, Boost.Reflect

## Purpose

Runtime type information (RTTI) for C++ types. Enables dynamic field iteration, type-safe property access, and runtime type validation. Only types annotated with `UZ_PROPERTY` macros register — no universal base class required.

## Type Registry (ClassRegistry)

```cpp
namespace uz {

/// Global registry of all reflected classes (singleton).
/// Classes auto-register via UZ_COMPONENT macro during static initialization.
/// After main() starts, the registry is read-only and safe for concurrent reads.
class ClassRegistry {
public:
    /// Returns the global singleton instance (Meyers singleton).
    static ClassRegistry& Get();

    /// Registers a class by name.
    /// Overwrites any previous registration with the same name.
    /// Logs Property::Info on success, Property::Warning if overwriting.
    ///
    /// # Arguments
    /// * type_name - Unique class name (typically from #ClassName macro)
    /// * info - Reference to ClassInfo (must have static lifetime)
    void Register(std::string_view type_name, ClassInfo& info);

    /// Finds a class by name (mutable access).
    /// Returns nullptr if not found.
    ClassInfo* FindClass(std::string_view type_name);

    /// Finds a class by name (const access).
    const ClassInfo* FindClass(std::string_view type_name) const;

    /// Checks whether a class is registered.
    bool IsRegistered(std::string_view type_name) const;

    /// Iterates all registered classes, calling fn(name, class_info).
    /// Iteration order is unspecified.
    void ForEachClass(const std::function<void(std::string_view, const ClassInfo&)>& fn) const;

    /// Returns the number of registered classes.
    size_t ClassCount() const noexcept;

    // Non-copyable, non-movable singleton
    ClassRegistry(const ClassRegistry&) = delete;
    ClassRegistry& operator=(const ClassRegistry&) = delete;

private:
    ClassRegistry() = default;
    std::unordered_map<std::string, ClassInfo*> registry_;
};

}  // namespace uz
```

## Usage Example

```cpp
// Find a registered class
auto& reg = uz::ClassRegistry::Get();
const uz::ClassInfo* enemy_class = reg.FindClass("Enemy");

if (!enemy_class) {
    UZ_LOG(Engine, Warning, "Enemy class not registered");
    return;
}

// Check if a class exists
if (reg.IsRegistered("Player")) {
    // Iterate all registered classes
    reg.ForEachClass([](std::string_view name, const uz::ClassInfo& info) {
        UZ_LOG(Engine, Info, "Registered class: {}, {} properties",
               name, info.PropertyCount());
    });
}

// Count total registered classes
size_t total = reg.ClassCount();
```

## ClassInfo Structure

```cpp
namespace uz {

/// Metadata descriptor for a reflected class registered in ClassRegistry.
/// Holds the class name, instance size, property list, and factory function.
/// Immutable after registration — thread-safe for concurrent reads.
struct ClassInfo {
    /// Fully-qualified class name (e.g., "Enemy", "HealthComponent")
    std::string name;

    /// Flat list of reflected properties belonging to this class
    std::vector<PropertyInfo> properties;

    /// Placement-new factory function for constructing instances.
    /// Signature: void* factory(void* placement_ptr)
    /// Returns: pointer to constructed object (same as placement_ptr)
    /// May be nullptr if no factory is registered.
    using FactoryFn = void* (*)(void* placement);
    FactoryFn factory = nullptr;

    /// Size of one instance in bytes (from sizeof(T))
    size_t instance_size = 0;

    /// Searches for a property by name (linear scan, case-sensitive).
    /// Returns pointer to PropertyInfo or nullptr if not found.
    const PropertyInfo* FindProperty(std::string_view prop_name) const;

    /// Iterates over all properties, calling fn for each one.
    /// Callback signature: void(const PropertyInfo&)
    void ForEachProperty(const std::function<void(const PropertyInfo&)>& fn) const;

    /// Returns the number of reflected properties
    size_t PropertyCount() const noexcept;
};

}  // namespace uz
```

## ClassInfo Usage Example

```cpp
const uz::ClassInfo* enemy_class = uz::ClassRegistry::Get().FindClass("Enemy");

if (enemy_class) {
    // Find a specific property
    const uz::PropertyInfo* speed_prop = enemy_class->FindProperty("speed");
    if (speed_prop) {
        std::cout << "Speed property found: type=" << uz::PropertyTypeName(speed_prop->type)
                  << ", offset=" << speed_prop->offset << "\n";
    }

    // Iterate all properties
    enemy_class->ForEachProperty([](const uz::PropertyInfo& prop) {
        std::cout << "  - " << prop.name << " (" << prop.size << " bytes)\n";
    });

    // Get class metadata
    std::cout << "Total properties: " << enemy_class->PropertyCount() << "\n";
    std::cout << "Instance size: " << enemy_class->instance_size << " bytes\n";

    // Create an instance using factory
    if (enemy_class->factory) {
        void* memory = operator new(enemy_class->instance_size);
        void* instance = enemy_class->factory(memory);
        // ... use instance ...
        operator delete(memory);
    }
}
```

## PropertyInfo Structure

```cpp
namespace uz {

/// Metadata descriptor for one reflected property of a class.
/// All string pointers must have static lifetime (typically string literals).
/// This is a lightweight POD-like descriptor — it does not own memory.
struct PropertyInfo {
    /// Property name (string literal, must be static lifetime)
    const char* name;

    /// Reflected type of this property
    PropertyType type;

    /// Byte offset from start of owning struct (result of offsetof)
    size_t offset;

    /// Size in bytes of the property's C++ type (result of sizeof)
    size_t size;

    /// Human-readable description for editors and documentation
    const char* description;

    /// Editor grouping category (e.g., "Movement", "Combat", "Rendering")
    const char* category;

    /// Behavioral flags controlling serialization and UI behavior
    PropertyFlags flags;
};

/// Enumerates the value types that the property system can reflect.
/// Each variant maps 1:1 to a concrete C++ type via GetPropertyType<T>().
enum class PropertyType : uint8_t {
    None = 0,   ///< Sentinel / uninitialized
    Bool,       ///< bool
    Int,        ///< int (signed 32-bit)
    Float,      ///< float (32-bit IEEE 754)
    Double,     ///< double (64-bit IEEE 754)
    String,     ///< std::string
    Vec2,       ///< uz::Vec2
    Vec3,       ///< uz::Vec3
    Vec4,       ///< uz::Vec4
    Quat,       ///< uz::Quat
    Entity,     ///< uz::Entity (generational handle)
    // Future: AssetRef, Enum, Struct, Array, Map (planned for future phases)
};

/// Bitwise flags controlling property behavior in editors and serialization.
enum class PropertyFlags : uint8_t {
    None       = 0,        ///< No special behavior
    ReadOnly   = 1U << 0,  ///< Cannot be edited in inspector
    Hidden     = 1U << 1,  ///< Hidden from inspector UI
    Replicated = 1U << 2,  ///< Replicated over network (Phase 5+)
    SaveGame   = 1U << 3,  ///< Included in save-game serialization
    Transient  = 1U << 4,  ///< Excluded from all serialization
};

/// Returns a human-readable name for a PropertyType.
const char* PropertyTypeName(PropertyType type);

/// Tests whether a specific flag is set.
constexpr bool HasFlag(PropertyFlags flags, PropertyFlags flag) {
    return (flags & flag) == flag;
}

}  // namespace uz
```

**Planned Features for Future Phases:**
- Min/max constraints (for numeric slider editor controls) — Phase 3b-3.2d
- Default value storage and reset functionality — Phase 3b-3.3
- AssetRef type for asset references — Phase 3b-3.2c
- Enum type support with dropdown generation — Phase 3b-3.2d
- Struct nesting and flattening — Phase 3b-3.4
- Array and Map container support — Phase 3b-3.4

## Property Registration Macros

UzEngine provides two mechanisms for registering properties:

### Method 1: Declarative Macro Approach (Recommended)

```cpp
struct Enemy : public Component {
    UZ_COMPONENT(Enemy);

    // Member variables (fields)
    float speed = 2.0f;
    int health = 30;
    Vec3 patrol_point{};

    // Declarative property registration
    UZ_PROPERTIES_BEGIN(Enemy)
        UZ_PROP(float, speed, "Movement speed in m/s")
        UZ_PROP(int, health, "Current health")
        UZ_PROP(Vec3, patrol_point, "Patrol destination")
    UZ_PROPERTIES_END()
};
```

The macros expand to:

```cpp
// UZ_COMPONENT expands to:
// - Typedef ThisClass
// - Static GetStaticClassInfo() method (lazy initialization)
// - Static s_uz_registered_Enemy variable (auto-registers in ClassRegistry)

// UZ_PROPERTIES_BEGIN / UZ_PROPERTIES_END expand to:
// - Static RegisterProperties(ClassInfo& _uz_info) method
// - UZ_PROP expands to: _uz_info.AddProperty<Type>(name, offsetof(...), desc)
// - UZ_PROP_EX expands to: _uz_info.AddProperty<Type>(name, offsetof(...), desc, cat, flags)
```

### Method 2: Programmatic Approach

```cpp
// Manually create and register a ClassInfo
static uz::ClassInfo& CreateEnemyInfo() {
    static uz::ClassInfo info;
    info.name = "Enemy";
    info.instance_size = sizeof(Enemy);
    info.factory = [](void* p) -> void* { return new (p) Enemy(); };

    // Add properties programmatically
    info.AddProperty<float>("speed", offsetof(Enemy, speed), "Movement speed in m/s");
    info.AddProperty<int>("health", offsetof(Enemy, health), "Current health");
    info.AddProperty<Vec3>("patrol_point", offsetof(Enemy, patrol_point), "Patrol destination");

    return info;
}

// Register in ClassRegistry during startup
uz::ClassRegistry::Get().Register("Enemy", CreateEnemyInfo());
```

## Field Iteration at Runtime

Iterate all properties of a type without knowing the type in advance:

```cpp
// Get ClassInfo for a type
auto& registry = uz::ClassRegistry::Get();
const uz::ClassInfo* enemy_class = registry.FindClass("Enemy");

if (enemy_class) {
    // Iterate properties
    enemy_class->ForEachProperty([](const uz::PropertyInfo& prop) {
        std::cout << "Property: " << prop.name
                  << ", Type: " << uz::PropertyTypeName(prop.type)
                  << ", Offset: " << prop.offset
                  << ", Size: " << prop.size << " bytes\n";
        if (uz::HasFlag(prop.flags, uz::PropertyFlags::ReadOnly)) {
            std::cout << "  (read-only)\n";
        }
    });

    // Common patterns
    size_t count = enemy_class->PropertyCount();
    std::cout << "Total properties: " << count << "\n";

    // Find by name
    const uz::PropertyInfo* speed = enemy_class->FindProperty("speed");
    if (speed) {
        std::cout << "Found speed property at offset " << speed->offset << "\n";
    }
}
```

## Type-Safe Property Access

Access properties by name using offset-based pointer arithmetic and type information:

```cpp
// Pointer-based property access (used by PropertySerializer)
void* instance = ...; // Pointer to Enemy instance

const uz::PropertyInfo* speed_prop = enemy_class->FindProperty("speed");
if (speed_prop && speed_prop->type == uz::PropertyType::Float) {
    // Read property value using offset
    const float* value_ptr = reinterpret_cast<const float*>(
        reinterpret_cast<const uint8_t*>(instance) + speed_prop->offset
    );
    float current_speed = *value_ptr;

    // Write property value (if not read-only)
    if (!uz::HasFlag(speed_prop->flags, uz::PropertyFlags::ReadOnly)) {
        float* write_ptr = reinterpret_cast<float*>(
            reinterpret_cast<uint8_t*>(instance) + speed_prop->offset
        );
        *write_ptr = current_speed * 1.5f;
    }
}
```

**Planned:** Type-safe accessor class (PropertyAccessor<T>) will be added in a future phase to wrap this pattern with compile-time type checking.

## Default Values

Component field initializers serve as the default values. When deserializing from JSON, missing properties retain their default values:

```cpp
struct Enemy : public Component {
    UZ_COMPONENT(Enemy);

    float speed = 2.0f;          // Default: 2.0f
    int health = 30;             // Default: 30
    Vec3 patrol_point{};         // Default: zero-initialized

    UZ_PROPERTIES_BEGIN(Enemy)
        UZ_PROP(float, speed, "Movement speed")
        UZ_PROP(int, health, "Hit points")
        UZ_PROP(Vec3, patrol_point, "Patrol location")
    UZ_PROPERTIES_END()
};

// When deserializing:
// { "speed": 5.0 }  // Missing 'health' and 'patrol_point' use defaults
// Result: speed=5.0, health=30 (default), patrol_point={0,0,0} (default)
```

**Planned:** Explicit default value storage and reset-to-defaults functionality will be added in Phase 3b-3.3 if needed by editor/serialization systems.

## Auto-Registration via UZ_COMPONENT

Components automatically register when the class is defined, via a static initializer:

```cpp
// UZ_COMPONENT macro implementation:
#define UZ_COMPONENT(ClassName)                                     \
public:                                                              \
    using ThisClass = ClassName;                                    \
    static ::uz::ClassInfo& GetStaticClassInfo() {                  \
        static ::uz::ClassInfo s_info;                              \
        static bool s_initialized = false;                          \
        if (!s_initialized) {                                        \
            s_info.name = #ClassName;                               \
            s_info.instance_size = sizeof(ClassName);               \
            s_info.factory = [](void* p) -> void* {                 \
                return new (p) ClassName();                         \
            };                                                       \
            ClassName::RegisterProperties(s_info);                  \
            s_initialized = true;                                   \
        }                                                            \
        return s_info;                                              \
    }                                                               \
private:                                                             \
    static inline const bool s_uz_registered_##ClassName = []() {  \
        ::uz::ClassRegistry::Get().Register(                        \
            #ClassName,                                              \
            ClassName::GetStaticClassInfo()                         \
        );                                                           \
        return true;                                                 \
    }();

// Results in:
// 1. ThisClass typedef (enables offsetof in UZ_PROP macros)
// 2. GetStaticClassInfo() method with lazy initialization
// 3. Static variable s_uz_registered_ClassName that runs lambda at startup
//    - The lambda calls ClassRegistry::Get().Register(...)
//    - This happens BEFORE main() runs (static initialization phase)
```

## Advanced: Custom Struct Nesting

**Planned for Phase 3b-3.4:** Support for custom struct types (Transform, Color, etc.) with flattened property iteration.

Currently, structs like Transform must be represented as separate fields:

```cpp
struct Enemy : public Component {
    UZ_COMPONENT(Enemy);

    // Instead of nesting Transform:
    Vec3 position{};
    Quat rotation{};
    Vec3 scale{1, 1, 1};

    UZ_PROPERTIES_BEGIN(Enemy)
        UZ_PROP(Vec3, position, "World position")
        UZ_PROP(Quat, rotation, "World rotation")
        UZ_PROP(Vec3, scale, "World scale")
    UZ_PROPERTIES_END()
};

// Future (3b-3.4): Support for nested Transform struct:
// struct Transform {
//     Vec3 position;
//     Quat rotation;
//     Vec3 scale;
// };
//
// struct Enemy {
//     Transform transform;  // Properties flattened at registration time
// };
```

## Editor Bindings

**Planned for Phase 3b-3.3:** Inspector UI generation from ClassInfo.

The foundation for editor bindings is now in place. The editor can use ClassInfo metadata to generate type-appropriate controls:

```cpp
// Future editor inspector generation (Phase 3b-3.3)
void GenerateInspectorUI(const uz::ClassInfo& class_info, void* obj) {
    // For each property in the class
    class_info.ForEachProperty([obj](const uz::PropertyInfo& prop) {
        // Generate UI based on property type and flags
        if (uz::HasFlag(prop.flags, uz::PropertyFlags::Hidden)) {
            return;  // Skip hidden properties
        }

        bool read_only = uz::HasFlag(prop.flags, uz::PropertyFlags::ReadOnly);

        ImGui::Text("%s", prop.name);
        if (!prop.description[0] == '\0') {
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", prop.description);
            }
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(read_only);

        // Type-specific UI widget
        switch (prop.type) {
            case uz::PropertyType::Bool: {
                bool* value = reinterpret_cast<bool*>(
                    reinterpret_cast<uint8_t*>(obj) + prop.offset
                );
                ImGui::Checkbox("##", value);
                break;
            }
            case uz::PropertyType::Float: {
                float* value = reinterpret_cast<float*>(
                    reinterpret_cast<uint8_t*>(obj) + prop.offset
                );
                ImGui::DragFloat("##", value);
                break;
            }
            case uz::PropertyType::Vec3: {
                Vec3* value = reinterpret_cast<Vec3*>(
                    reinterpret_cast<uint8_t*>(obj) + prop.offset
                );
                ImGui::DragFloat3("##", &value->x);
                break;
            }
            // ... other types
        }

        ImGui::EndDisabled();
    });
}
```

## Comparison

| Feature | UE5 Reflection | C++ std::type_info | Qt Meta-Object | **UzEngine** |
|---------|----------------|-------------------|-----------------|--------------|
| Opt-in | No (UObject required) | No (all types) | Yes (Q_OBJECT) | ✅ Yes (UZ_COMPONENT) |
| Field names | Yes | No | Yes | ✅ Yes |
| Offsets | Yes | No | No | ✅ Yes |
| Default values | Yes (CDO) | No | Yes | Partial (field init) |
| Editor metadata | Yes (extensive) | No | Yes | ✅ Yes (description, category, flags) |
| Property iteration | Yes | No | Yes | ✅ Yes |
| Type-safe access | Weak (handles) | No | Yes (QVariant) | Via offset arithmetic |
| Factory function | Yes | No | No | ✅ Yes |
| Runtime overhead | Moderate | Minimal | Moderate | **✅ Minimal** (register-only) |

## Implementation Status

**Phase 3b-3.2b MVP** ✅ Completed:
- ClassRegistry singleton with thread-safe registration
- ClassInfo with flat PropertyInfo list and factory function
- UZ_COMPONENT macro for auto-registration
- UZ_PROPERTIES_BEGIN/END, UZ_PROP, UZ_PROP_EX declarative macros
- GetPropertyType<T>() template specializations for all supported types
- Property iteration via ForEachProperty() callback
- Offset-based property access
- Basic PropertyFlags (ReadOnly, Hidden, Replicated, SaveGame, Transient)

**Future Phases:**
- 3b-3.2c: Default value storage and reset functionality
- 3b-3.2d: Enum types, property constraints (min/max), specialized UI generation
- 3b-3.3: Editor integration, reactive data binding, hot-reload
- 3b-3.4: Custom struct nesting, array/map support, migration framework
- Phase 5+: Network replication, delta encoding, change tracking
