// shader_graph_types.cpp -- ValueType helpers implementation.

#include "engine/renderer/shader_graph/shader_graph_types.h"

namespace uz::sg {

ValueType promote_type(ValueType a, ValueType b) {
    if (a == ValueType::None || b == ValueType::None)
        return ValueType::None;

    // If both are the same concrete float type, return it.
    if (a == b && is_concrete(a)) return a;

    // Extract float bits only.
    constexpr uint32_t float_mask = static_cast<uint32_t>(ValueType::Scalar);
    uint32_t fa = static_cast<uint32_t>(a) & float_mask;
    uint32_t fb = static_cast<uint32_t>(b) & float_mask;

    // Both must have float bits to be promotable.
    if (fa == 0 || fb == 0)
        return ValueType::None;

    // If one is Float1 (scalar), promote to the other's width.
    if (fa == static_cast<uint32_t>(ValueType::Float1))
        return is_concrete(b) ? b : ValueType::None;
    if (fb == static_cast<uint32_t>(ValueType::Float1))
        return is_concrete(a) ? a : ValueType::None;

    // Both are concrete non-Float1 floats — must match.
    if (is_concrete(a) && is_concrete(b) && a == b)
        return a;

    // Mismatch (e.g., Float2 + Float4).
    return ValueType::None;
}

int component_count(ValueType t) {
    switch (t) {
        case ValueType::Float1: return 1;
        case ValueType::Float2: return 2;
        case ValueType::Float3: return 3;
        case ValueType::Float4: return 4;
        default: return 0;
    }
}

const char* hlsl_type_name(ValueType t) {
    switch (t) {
        case ValueType::Float1:      return "float";
        case ValueType::Float2:      return "float2";
        case ValueType::Float3:      return "float3";
        case ValueType::Float4:      return "float4";
        case ValueType::Int:         return "int";
        case ValueType::Bool:        return "bool";
        case ValueType::Texture2D:   return "Texture2D";
        case ValueType::TextureCube: return "TextureCube";
        case ValueType::Sampler:     return "SamplerState";
        default: return nullptr;
    }
}

} // namespace uz::sg
