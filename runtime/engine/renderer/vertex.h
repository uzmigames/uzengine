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

#pragma once

#include "engine_math.h"

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

} // namespace uz
