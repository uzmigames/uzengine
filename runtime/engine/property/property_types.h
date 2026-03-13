#pragma once

/// UzEngine Property Type System
///
/// Defines the fundamental types that the property/reflection system can represent,
/// along with bitwise flags for controlling property behavior in editors, serialization,
/// and replication.
///
/// # Extending
/// Add new entries to `PropertyType` when the engine gains new reflected value types
/// (e.g., AssetRef, Enum). Update `GetPropertyType<T>()` specializations in
/// `property_macros.h` and serialization in `property_serializer.cpp` accordingly.

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// PropertyType — identifies the concrete C++ type of a reflected property
// ─────────────────────────────────────────────────────────────────────────────

/// Enumerates the value types that the property system can reflect and serialize.
///
/// Each variant maps 1:1 to a concrete C++ type. The mapping is enforced at
/// compile time via `GetPropertyType<T>()` template specializations.
enum class PropertyType : uint8_t {
    None = 0, ///< Sentinel / uninitialized.
    Bool,     ///< `bool`
    Int,      ///< `int` (signed 32-bit)
    Float,    ///< `float` (32-bit IEEE 754)
    Double,   ///< `double` (64-bit IEEE 754)
    String,   ///< `std::string`
    Vec2,     ///< `uz::Vec2`
    Vec3,     ///< `uz::Vec3`
    Vec4,     ///< `uz::Vec4`
    Quat,     ///< `uz::Quat`
    Entity,   ///< `uz::Entity` (generational handle)
};

/// Returns a human-readable name for a PropertyType value.
///
/// # Arguments
/// * `type` - The property type to convert.
///
/// # Returns
/// Static string literal (never null). Returns "Unknown" for unrecognized values.
inline const char* PropertyTypeName(PropertyType type) {
    switch (type) {
    case PropertyType::None:   return "None";
    case PropertyType::Bool:   return "Bool";
    case PropertyType::Int:    return "Int";
    case PropertyType::Float:  return "Float";
    case PropertyType::Double: return "Double";
    case PropertyType::String: return "String";
    case PropertyType::Vec2:   return "Vec2";
    case PropertyType::Vec3:   return "Vec3";
    case PropertyType::Vec4:   return "Vec4";
    case PropertyType::Quat:   return "Quat";
    case PropertyType::Entity: return "Entity";
    }
    return "Unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// PropertyFlags — bitwise flags controlling property behavior
// ─────────────────────────────────────────────────────────────────────────────

/// Bitwise flags that control how a property behaves in editors, serialization,
/// and networking.
///
/// Combine flags with `|`: `PropertyFlags::ReadOnly | PropertyFlags::Hidden`.
enum class PropertyFlags : uint8_t {
    None       = 0,        ///< No special behavior.
    ReadOnly   = 1U << 0,  ///< Property cannot be edited in the inspector.
    Hidden     = 1U << 1,  ///< Property is hidden from the inspector UI.
    Replicated = 1U << 2,  ///< Property is replicated over the network.
    SaveGame   = 1U << 3,  ///< Property is included in save-game serialization.
    Transient  = 1U << 4,  ///< Property is excluded from all serialization.
};

/// Bitwise OR for combining PropertyFlags.
constexpr PropertyFlags operator|(PropertyFlags a, PropertyFlags b) {
    return static_cast<PropertyFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

/// Bitwise AND for testing PropertyFlags.
constexpr PropertyFlags operator&(PropertyFlags a, PropertyFlags b) {
    return static_cast<PropertyFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

/// Bitwise XOR for toggling PropertyFlags.
constexpr PropertyFlags operator^(PropertyFlags a, PropertyFlags b) {
    return static_cast<PropertyFlags>(static_cast<uint8_t>(a) ^ static_cast<uint8_t>(b));
}

/// Bitwise NOT for inverting PropertyFlags.
constexpr PropertyFlags operator~(PropertyFlags a) {
    return static_cast<PropertyFlags>(~static_cast<uint8_t>(a));
}

/// Compound OR assignment.
constexpr PropertyFlags& operator|=(PropertyFlags& a, PropertyFlags b) {
    a = a | b;
    return a;
}

/// Compound AND assignment.
constexpr PropertyFlags& operator&=(PropertyFlags& a, PropertyFlags b) {
    a = a & b;
    return a;
}

/// Tests whether a specific flag is set.
///
/// # Arguments
/// * `flags` - The flags bitfield to test.
/// * `flag` - The specific flag to check for.
///
/// # Returns
/// `true` if `flag` is set in `flags`.
constexpr bool HasFlag(PropertyFlags flags, PropertyFlags flag) {
    return (flags & flag) == flag;
}

} // namespace uz
