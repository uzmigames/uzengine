#pragma once

/// UzEngine Property Macros
///
/// Provides compile-time type-to-PropertyType mapping and registration macros
/// for the reflection/property system.
///
/// # Macro API
/// - `UZ_COMPONENT(ClassName)` — declares a reflected class with auto-registration
/// - `UZ_PROPERTIES_BEGIN(ClassName)` — opens the property list for a class
/// - `UZ_PROP(type, name, desc)` — adds a property entry
/// - `UZ_PROP_EX(type, name, desc, cat, flags)` — adds a property with category/flags
/// - `UZ_PROPERTIES_END()` — closes the property list
///
/// # Usage
/// ```cpp
/// struct Enemy : public Component {
///     UZ_COMPONENT(Enemy);
///
///     float speed = 2.0f;
///     float health = 100.0f;
///     Vec3 target{};
///
///     static void RegisterProperties(ClassInfo& info) {
///         info.AddProperty<float>("speed", offsetof(Enemy, speed), "Movement speed");
///         info.AddProperty<float>("health", offsetof(Enemy, health), "Hit points");
///         info.AddProperty<Vec3>("target", offsetof(Enemy, target), "Target position");
///     }
/// };
/// ```

#include "class_info.h"
#include "class_registry.h"

#include <string>
#include <type_traits>

namespace uz {

struct Vec2;
struct Vec3;
struct Vec4;
struct Quat;
struct Entity;

// ─────────────────────────────────────────────────────────────────────────────
// GetPropertyType<T> — compile-time type-to-PropertyType mapping
// ─────────────────────────────────────────────────────────────────────────────

/// Maps a C++ type to its corresponding `PropertyType` enum value.
///
/// # Returns
/// The `PropertyType` for the given type. Unspecialized types cause a compile error.
template <typename T>
constexpr PropertyType GetPropertyType() {
    static_assert(sizeof(T) == 0, "GetPropertyType not specialized for this type");
    return PropertyType::None;
}

/// bool -> PropertyType::Bool
template <> inline constexpr PropertyType GetPropertyType<bool>() { return PropertyType::Bool; }
/// int -> PropertyType::Int
template <> inline constexpr PropertyType GetPropertyType<int>() { return PropertyType::Int; }
/// float -> PropertyType::Float
template <> inline constexpr PropertyType GetPropertyType<float>() { return PropertyType::Float; }
/// double -> PropertyType::Double
template <> inline constexpr PropertyType GetPropertyType<double>() { return PropertyType::Double; }
/// std::string -> PropertyType::String
template <> inline constexpr PropertyType GetPropertyType<std::string>() { return PropertyType::String; }
/// Vec2 -> PropertyType::Vec2
template <> inline constexpr PropertyType GetPropertyType<Vec2>() { return PropertyType::Vec2; }
/// Vec3 -> PropertyType::Vec3
template <> inline constexpr PropertyType GetPropertyType<Vec3>() { return PropertyType::Vec3; }
/// Vec4 -> PropertyType::Vec4
template <> inline constexpr PropertyType GetPropertyType<Vec4>() { return PropertyType::Vec4; }
/// Quat -> PropertyType::Quat
template <> inline constexpr PropertyType GetPropertyType<Quat>() { return PropertyType::Quat; }
/// Entity -> PropertyType::Entity
template <> inline constexpr PropertyType GetPropertyType<Entity>() { return PropertyType::Entity; }

// ─────────────────────────────────────────────────────────────────────────────
// ClassInfo::AddProperty<T> — template implementation (needs GetPropertyType)
// ─────────────────────────────────────────────────────────────────────────────

template <typename T>
void ClassInfo::AddProperty(const char* prop_name, size_t byte_offset, const char* desc,
                            const char* cat, PropertyFlags flags) {
    PropertyInfo info;
    info.name = prop_name;
    info.type = GetPropertyType<T>();
    info.offset = byte_offset;
    info.size = sizeof(T);
    info.description = desc;
    info.category = cat;
    info.flags = flags;
    properties.push_back(info);
}

} // namespace uz

// ─────────────────────────────────────────────────────────────────────────────
// UZ_COMPONENT — declares a reflected class with auto-registration
// ─────────────────────────────────────────────────────────────────────────────

/// Declares a reflected class and registers it in the global ClassRegistry.
///
/// Must appear inside the class body. The class must define a static
/// `RegisterProperties(ClassInfo&)` method to populate property metadata.
///
/// Expands to:
/// - `ThisClass` typedef for use with `offsetof`
/// - Static `GetStaticClassInfo()` that returns and lazily initializes the ClassInfo
/// - Static auto-registration variable that registers the class before `main()`
///
/// # Arguments
/// * `ClassName` - The unqualified class name (e.g., `Enemy`, `HealthComponent`)
#define UZ_COMPONENT(ClassName) /* NOLINT(cppcoreguidelines-macro-usage) */                        \
public:                                                                                            \
    using ThisClass = ClassName;                                                                   \
    static ::uz::ClassInfo& GetStaticClassInfo() {                                                 \
        static ::uz::ClassInfo s_info;                                                             \
        static bool s_initialized = false;                                                         \
        if (!s_initialized) {                                                                      \
            s_info.name = #ClassName;                                                              \
            s_info.instance_size = sizeof(ClassName);                                              \
            s_info.factory = [](void* p) -> void* { return new (p) ClassName(); };                 \
            ClassName::RegisterProperties(s_info);                                                 \
            s_initialized = true;                                                                  \
        }                                                                                          \
        return s_info;                                                                             \
    }                                                                                              \
                                                                                                   \
private:                                                                                           \
    static inline const bool s_uz_registered_##ClassName = []() {                                  \
        ::uz::ClassRegistry::Get().Register(#ClassName, ClassName::GetStaticClassInfo());           \
        return true;                                                                               \
    }();                                                                                           \
public:

// ─────────────────────────────────────────────────────────────────────────────
// UZ_BEHAVIOR — declares a reflected behavior (tickable) class with auto-registration
// ─────────────────────────────────────────────────────────────────────────────

/// Declares a reflected behavior class and registers it in the global ClassRegistry.
///
/// Identical to `UZ_COMPONENT` but additionally sets `is_behavior = true` on the
/// `ClassInfo`, marking this class as a tickable behavior for runtime queries via
/// `ClassRegistry::ForEachBehavior`.
///
/// The class must derive from `TickableComponent` and define a static
/// `RegisterProperties(ClassInfo&)` method.
///
/// # Arguments
/// * `ClassName` - The unqualified class name (e.g., `PatrolBehavior`, `FollowTarget`)
#define UZ_BEHAVIOR(ClassName) /* NOLINT(cppcoreguidelines-macro-usage) */                        \
public:                                                                                            \
    using ThisClass = ClassName;                                                                   \
    static ::uz::ClassInfo& GetStaticClassInfo() {                                                 \
        static ::uz::ClassInfo s_info;                                                             \
        static bool s_initialized = false;                                                         \
        if (!s_initialized) {                                                                      \
            s_info.name = #ClassName;                                                              \
            s_info.instance_size = sizeof(ClassName);                                              \
            s_info.is_behavior = true;                                                             \
            s_info.factory = [](void* p) -> void* { return new (p) ClassName(); };                 \
            ClassName::RegisterProperties(s_info);                                                 \
            s_initialized = true;                                                                  \
        }                                                                                          \
        return s_info;                                                                             \
    }                                                                                              \
                                                                                                   \
private:                                                                                           \
    static inline const bool s_uz_registered_##ClassName = []() {                                  \
        ::uz::ClassRegistry::Get().Register(#ClassName, ClassName::GetStaticClassInfo());           \
        return true;                                                                               \
    }();                                                                                           \
public:

// ─────────────────────────────────────────────────────────────────────────────
// UZ_PROPERTIES_BEGIN / UZ_PROP / UZ_PROPERTIES_END — declarative alternative
// ─────────────────────────────────────────────────────────────────────────────

/// Opens a declarative property registration block for `ClassName`.
///
/// Use between `UZ_COMPONENT(ClassName)` and the closing brace. Each `UZ_PROP`
/// inside the block adds one `PropertyInfo` to the class's `ClassInfo`.
///
/// # Arguments
/// * `ClassName` - Must match the `UZ_COMPONENT(ClassName)` in the same class.
#define UZ_PROPERTIES_BEGIN(ClassName) /* NOLINT(cppcoreguidelines-macro-usage) */                 \
    static void RegisterProperties(::uz::ClassInfo& _uz_info) {                                   \
        (void)_uz_info;

/// Adds a property entry inside a `UZ_PROPERTIES_BEGIN` / `UZ_PROPERTIES_END` block.
///
/// # Arguments
/// * `type` - C++ type (e.g., `float`, `Vec3`, `std::string`)
/// * `name` - Field name (must be a member of `ThisClass`)
/// * `desc` - Description string literal
#define UZ_PROP(type, name, desc) /* NOLINT(cppcoreguidelines-macro-usage) */                      \
    _uz_info.AddProperty<type>(#name, offsetof(ThisClass, name), desc);

/// Adds a property entry with category and flags.
///
/// # Arguments
/// * `type` - C++ type
/// * `name` - Field name (must be a member of `ThisClass`)
/// * `desc` - Description string literal
/// * `cat` - Category string literal (e.g., "Physics", "Rendering")
/// * `flags` - `PropertyFlags` value or combination
#define UZ_PROP_EX(type, name, desc, cat, flags) /* NOLINT(cppcoreguidelines-macro-usage) */       \
    _uz_info.AddProperty<type>(#name, offsetof(ThisClass, name), desc, cat, flags);

/// Closes a `UZ_PROPERTIES_BEGIN` block.
#define UZ_PROPERTIES_END() /* NOLINT(cppcoreguidelines-macro-usage) */                            \
    }
