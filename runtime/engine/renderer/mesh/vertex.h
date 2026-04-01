/// UzEngine Vertex Definitions
///
/// Standard vertex layouts for 3D mesh and debug geometry rendering.
/// All vertex data is uploaded as StructuredBuffers and accessed
/// via SM6.6 bindless `ResourceDescriptorHeap[index]` in shaders.
/// No Input Assembler vertex layout is used — vertex fetch is entirely
/// in the vertex shader.
///
/// # Layouts
/// - `Vertex` (48 bytes): full PBR mesh vertex with position, normal, UV, tangent
/// - `SimpleVertex` (36 bytes): minimal vertex for debug lines, UI, and test geometry
/// - `SkinnedVertex` (56 bytes): skeletal mesh vertex with 4-bone skinning data

#pragma once

#include "vector2.h"
#include "vector3.h"
#include "vector4.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Vertex — Standard mesh vertex
// ─────────────────────────────────────────────────────────────────────────────

/// Standard vertex for lit textured meshes.
///
/// Matches the HLSL struct used by the vertex shader:
/// ```hlsl
/// struct Vertex {
///     float3 position;
///     float3 normal;
///     float2 texcoord;
///     float4 tangent;  // w = handedness (+1 or -1)
/// };
/// ```
struct Vertex {
    Vec3 position;   ///< Object-space position
    Vec3 normal;     ///< Object-space normal (unit length)
    Vec2 texcoord;   ///< UV coordinates
    Vec4 tangent;    ///< Tangent (xyz) + handedness sign (w)
};

static_assert(sizeof(Vertex) == 48, "Vertex must be 48 bytes for GPU struct alignment");

// ─────────────────────────────────────────────────────────────────────────────
// SimpleVertex — Minimal vertex for debug/UI geometry
// ─────────────────────────────────────────────────────────────────────────────

/// Minimal vertex with position, color, and UV.
///
/// Used for debug lines, UI quads, and the initial triangle test.
struct SimpleVertex {
    Vec3  position;  ///< Object-space position
    Vec4  color;     ///< RGBA vertex color
    Vec2  texcoord;  ///< UV coordinates
};

static_assert(sizeof(SimpleVertex) == 36, "SimpleVertex must be 36 bytes");

// ─────────────────────────────────────────────────────────────────────────────
// SkinnedVertex — Skeletal mesh vertex with 4-bone skinning
// ─────────────────────────────────────────────────────────────────────────────

/// Skinned mesh vertex for skeletal animation.
///
/// Extends the standard `Vertex` layout with 4-bone influence skinning data.
/// Each vertex stores indices into the skeleton's bone palette and normalized
/// weights that determine how much each bone affects the vertex position/normal.
///
/// Bone weights are stored as uint8 values that sum to 255. The shader converts
/// to float by dividing by 255.0, yielding weights that sum to ~1.0. Using uint8
/// instead of float saves 24 bytes per vertex (4x uint8 vs 4x float32 for both
/// indices and weights) with negligible quality loss for up to 256-bone skeletons.
///
/// Matches the HLSL struct used by the skinned vertex shader:
/// ```hlsl
/// struct SkinnedVertex {
///     float3   position;      // 12B
///     float3   normal;        // 12B
///     float2   texcoord;      //  8B
///     float4   tangent;       // 16B
///     uint4    boneIndices;   //  4B (packed as 4x uint8 → loaded as uint, unpacked in shader)
///     uint4    boneWeights;   //  4B (packed as 4x uint8 → loaded as uint, unpacked in shader)
/// };
/// ```
struct SkinnedVertex {
    Vec3    position;        ///< Object-space position
    Vec3    normal;          ///< Object-space normal (unit length)
    Vec2    texcoord;        ///< UV coordinates
    Vec4    tangent;         ///< Tangent (xyz) + handedness sign (w)
    uint8_t boneIndices[4];  ///< Indices into the skeleton's bone palette (max 256 bones)
    uint8_t boneWeights[4];  ///< Normalized weights (sum to 255; shader divides by 255.0)
};

static_assert(sizeof(SkinnedVertex) == 56, "SkinnedVertex must be 56 bytes");

} // namespace uz
