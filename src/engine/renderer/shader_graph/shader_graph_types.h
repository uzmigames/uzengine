// shader_graph_types.h -- Bitmask-based value type system for shader graph pins.
//
// Types are bitmasks enabling automatic scalar-to-vector promotion.
// Compatibility: (a & b) != 0 means "a" pin can accept "b" connection.

#pragma once

#include <cstdint>

namespace uz::sg {

enum class ValueType : uint32_t {
    None        = 0,
    Float1      = 1 << 0,
    Float2      = 1 << 1,
    Float3      = 1 << 2,
    Float4      = 1 << 3,
    Texture2D   = 1 << 4,
    TextureCube = 1 << 5,
    Sampler     = 1 << 6,
    Bool        = 1 << 7,
    Int         = 1 << 8,

    // Composite masks
    Scalar     = Float1 | Float2 | Float3 | Float4,
    AnyTexture = Texture2D | TextureCube,
};

inline constexpr ValueType operator|(ValueType a, ValueType b) {
    return static_cast<ValueType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr ValueType operator&(ValueType a, ValueType b) {
    return static_cast<ValueType>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline constexpr bool operator!(ValueType v) {
    return static_cast<uint32_t>(v) == 0;
}

// Check if two types are compatible (one can connect to the other).
// For composite types (Scalar), any overlap means compatible.
inline constexpr bool types_compatible(ValueType a, ValueType b) {
    return static_cast<uint32_t>(a & b) != 0;
}

// Returns true if the type is a single concrete type (exactly one bit set).
inline constexpr bool is_concrete(ValueType t) {
    auto v = static_cast<uint32_t>(t);
    return v != 0 && (v & (v - 1)) == 0;
}

// Promote two types to a common type for binary operations.
// Float1 + Float3 -> Float3 (scalar broadcast)
// Float3 + Float3 -> Float3 (same)
// Float2 + Float4 -> None (ambiguous, error)
// None + any      -> None (error)
ValueType promote_type(ValueType a, ValueType b);

// Number of float components: Float1->1, Float2->2, Float3->3, Float4->4.
// Returns 0 for non-float types or composite types.
int component_count(ValueType t);

// HLSL type name: Float1->"float", Float2->"float2", etc.
// Returns nullptr for non-float/non-concrete types.
const char* hlsl_type_name(ValueType t);

} // namespace uz::sg
