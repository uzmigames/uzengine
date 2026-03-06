#pragma once

// mesh_primitives.h -- Procedural mesh generation for common primitives.
//
// Generates CPU-side vertex data (MeshData) ready for GPU upload.

#include "engine/renderer/resources/mesh.h"

namespace uz {

/// Generate a UV sphere with the given number of slices (longitude),
/// stacks (latitude), and radius. Returns MeshData with PBR-ready vertices
/// (position, normal, texcoord, tangent) and triangle indices.
[[nodiscard]] MeshData generate_sphere(uint32_t slices, uint32_t stacks, float radius);

/// Generate a unit cube (side = 2, centered at origin) with normals and UVs.
[[nodiscard]] MeshData generate_cube(float half_extent = 1.0f);

/// Generate a flat grid plane on XZ, centered at origin.
[[nodiscard]] MeshData generate_plane(float width, float depth, uint32_t segments = 1);

} // namespace uz
