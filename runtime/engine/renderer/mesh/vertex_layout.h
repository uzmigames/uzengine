/// UzEngine Vertex Layout System
///
/// Provides a single source of truth for vertex input layouts, eliminating
/// hardcoded `RHIInputElementDesc` arrays scattered across demos and passes.
///
/// # Usage
/// ```cpp
/// auto [elements, count, stride] = VertexLayout::Standard();
/// pipelineDesc.inputElements    = elements;
/// pipelineDesc.numInputElements = count;
/// pipelineDesc.vertexStride     = stride;
/// ```
///
/// For SM6.6 bindless paths (no Input Assembler), the stride is still needed
/// for StructuredBuffer element size.

#pragma once

#include "rhi_types.h"
#include "vertex.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// VertexFormatFlags — bitmask for optional vertex channels
// ─────────────────────────────────────────────────────────────────────────────

/// Bitmask flags indicating which vertex channels are present.
///
/// Used by `VertexLayout::FromFlags()` to build dynamic input layouts.
/// `HasPosition` is always implicitly present and does not need to be set.
enum class VertexFormatFlags : uint32_t {
    None       = 0,  ///< No optional channels — position only.
    HasNormal  = 1 << 0,  ///< float3 NORMAL
    HasTangent = 1 << 1,  ///< float4 TANGENT (xyz + handedness w)
    HasUV0     = 1 << 2,  ///< float2 TEXCOORD0 — primary UV
    HasUV1     = 1 << 3,  ///< float2 TEXCOORD1 — lightmap UV
    HasUV2     = 1 << 4,  ///< float2 TEXCOORD2 — detail UV
    HasColor   = 1 << 5,  ///< float4 COLOR0
};

/// Bitwise OR for combining VertexFormatFlags.
inline constexpr VertexFormatFlags operator|(VertexFormatFlags a, VertexFormatFlags b) {
    return static_cast<VertexFormatFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

/// Bitwise AND for testing VertexFormatFlags.
inline constexpr VertexFormatFlags operator&(VertexFormatFlags a, VertexFormatFlags b) {
    return static_cast<VertexFormatFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/// Test if a specific flag is set.
inline constexpr bool HasFlag(VertexFormatFlags flags, VertexFormatFlags test) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// VertexLayout — input layout descriptors
// ─────────────────────────────────────────────────────────────────────────────

/// Maximum number of input elements in a vertex layout.
static constexpr uint32_t kMaxVertexElements = 8;

/// Result of a vertex layout query: element array, count, and stride.
struct VertexLayoutResult {
    RHIInputElementDesc elements[kMaxVertexElements]; ///< Input element descriptors.
    uint32_t count  = 0;  ///< Number of valid elements in the array.
    uint32_t stride = 0;  ///< Total vertex stride in bytes.
};

/// Vertex input layout factory.
///
/// Generates `RHIInputElementDesc` arrays from vertex struct definitions
/// or format flags. Eliminates the need to hardcode input layouts in
/// pipeline creation code.
namespace VertexLayout {

/// Returns the input layout for the standard PBR `Vertex` struct.
///
/// Layout: Position(float3, 0) + Normal(float3, 12) + Texcoord(float2, 24) + Tangent(float4, 32)
/// Stride: 48 bytes (sizeof(Vertex)).
///
/// # Returns
/// A `VertexLayoutResult` with 4 elements and stride 48.
[[nodiscard]] inline VertexLayoutResult Standard() {
    VertexLayoutResult result{};
    result.count  = 4;
    result.stride = static_cast<uint32_t>(sizeof(Vertex));

    result.elements[0] = { RHIInputSemantic::Position, 0, RHIFormat::RGB32Float,  0, 0  };
    result.elements[1] = { RHIInputSemantic::Normal,   0, RHIFormat::RGB32Float,  0, 12 };
    result.elements[2] = { RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float,   0, 24 };
    result.elements[3] = { RHIInputSemantic::Tangent,  0, RHIFormat::RGBA32Float, 0, 32 };

    return result;
}

/// Returns the input layout for the `SimpleVertex` struct.
///
/// Layout: Position(float3, 0) + Color(float4, 12) + Texcoord(float2, 28)
/// Stride: 36 bytes (sizeof(SimpleVertex)).
///
/// # Returns
/// A `VertexLayoutResult` with 3 elements and stride 36.
[[nodiscard]] inline VertexLayoutResult Simple() {
    VertexLayoutResult result{};
    result.count  = 3;
    result.stride = static_cast<uint32_t>(sizeof(SimpleVertex));

    result.elements[0] = { RHIInputSemantic::Position, 0, RHIFormat::RGB32Float,  0, 0  };
    result.elements[1] = { RHIInputSemantic::Color,    0, RHIFormat::RGBA32Float, 0, 12 };
    result.elements[2] = { RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float,   0, 28 };

    return result;
}

/// Builds a dynamic vertex layout from format flags.
///
/// Position (float3) is always included as the first element.
/// Optional channels are appended in order: Normal, UV0, Tangent, UV1, UV2, Color.
/// Byte offsets are computed automatically from the accumulated stride.
///
/// # Arguments
/// * `flags` — Bitmask of `VertexFormatFlags` indicating which channels are present.
///
/// # Returns
/// A `VertexLayoutResult` with elements and stride matching the flags.
///
/// # Examples
/// ```cpp
/// auto layout = VertexLayout::FromFlags(
///     VertexFormatFlags::HasNormal | VertexFormatFlags::HasUV0);
/// // Elements: Position(0), Normal(12), Texcoord0(24)
/// // Stride: 32 bytes
/// ```
[[nodiscard]] inline VertexLayoutResult FromFlags(VertexFormatFlags flags) {
    VertexLayoutResult result{};
    uint32_t offset = 0;

    // Position is always present (float3 = 12 bytes).
    result.elements[result.count++] = { RHIInputSemantic::Position, 0, RHIFormat::RGB32Float, 0, offset };
    offset += 12;

    if (HasFlag(flags, VertexFormatFlags::HasNormal)) {
        result.elements[result.count++] = { RHIInputSemantic::Normal, 0, RHIFormat::RGB32Float, 0, offset };
        offset += 12;
    }

    if (HasFlag(flags, VertexFormatFlags::HasUV0)) {
        result.elements[result.count++] = { RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float, 0, offset };
        offset += 8;
    }

    if (HasFlag(flags, VertexFormatFlags::HasTangent)) {
        result.elements[result.count++] = { RHIInputSemantic::Tangent, 0, RHIFormat::RGBA32Float, 0, offset };
        offset += 16;
    }

    if (HasFlag(flags, VertexFormatFlags::HasUV1)) {
        result.elements[result.count++] = { RHIInputSemantic::Texcoord, 1, RHIFormat::RG32Float, 0, offset };
        offset += 8;
    }

    if (HasFlag(flags, VertexFormatFlags::HasUV2)) {
        result.elements[result.count++] = { RHIInputSemantic::Texcoord, 2, RHIFormat::RG32Float, 0, offset };
        offset += 8;
    }

    if (HasFlag(flags, VertexFormatFlags::HasColor)) {
        result.elements[result.count++] = { RHIInputSemantic::Color, 0, RHIFormat::RGBA32Float, 0, offset };
        offset += 16;
    }

    result.stride = offset;
    return result;
}

/// Computes the vertex stride in bytes for the given format flags.
///
/// Equivalent to `FromFlags(flags).stride` but avoids building the full layout.
///
/// # Arguments
/// * `flags` — Bitmask of `VertexFormatFlags`.
///
/// # Returns
/// Total stride in bytes (minimum 12 for position-only).
[[nodiscard]] inline uint32_t GetVertexStride(VertexFormatFlags flags) {
    uint32_t stride = 12; // Position always present
    if (HasFlag(flags, VertexFormatFlags::HasNormal))  stride += 12;
    if (HasFlag(flags, VertexFormatFlags::HasUV0))     stride += 8;
    if (HasFlag(flags, VertexFormatFlags::HasTangent)) stride += 16;
    if (HasFlag(flags, VertexFormatFlags::HasUV1))     stride += 8;
    if (HasFlag(flags, VertexFormatFlags::HasUV2))     stride += 8;
    if (HasFlag(flags, VertexFormatFlags::HasColor))   stride += 16;
    return stride;
}

/// Returns the `VertexFormatFlags` matching the standard PBR `Vertex` struct.
///
/// Equivalent to `HasNormal | HasUV0 | HasTangent`.
[[nodiscard]] inline constexpr VertexFormatFlags StandardFlags() {
    return VertexFormatFlags::HasNormal | VertexFormatFlags::HasUV0 | VertexFormatFlags::HasTangent;
}

} // namespace VertexLayout
} // namespace uz
