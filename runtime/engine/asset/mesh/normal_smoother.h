/// UzEngine Smooth Normal Generator
///
/// Computes area+angle weighted smooth vertex normals with hard edge
/// detection at a configurable smoothing angle threshold. Replaces flat
/// per-face normals with averaged per-vertex normals for smooth shading.
///
/// @ref Max 1999 "Weights for Computing Vertex Normals from Facet Normals"
/// @ref Thürey 2000 "Weighted Vertex Normals"
///
/// # Algorithm
/// For each vertex, accumulate the normal of every adjacent face weighted
/// by `triangleArea * cornerAngle`. Edges where the dihedral angle exceeds
/// the smoothing threshold are treated as hard edges — the vertex is split
/// so each side gets its own normal.
///
/// # Usage
/// ```cpp
/// std::vector<VertexPosNormUV> verts = ...;
/// std::vector<uint32_t> indices = ...;
/// ComputeSmoothNormals(verts, indices, 60.0f); // 60° threshold
/// ```

#pragma once

#include <cstdint>
#include <vector>

namespace uz {

struct VertexPosNormUV;

/// Compute smooth vertex normals with area+angle weighting.
///
/// Modifies `vertices` in-place, replacing flat normals with smooth averaged
/// normals. Vertices at hard edges (dihedral angle > `smoothingAngleDeg`)
/// are split — new vertices are appended and indices updated.
///
/// @param vertices        Vertex array (position + normal + UV). May grow if hard edges cause splits.
/// @param indices         Index array. Updated in-place for split vertices.
/// @param smoothingAngleDeg  Dihedral angle threshold in degrees. Edges sharper than this are hard.
///                           Default 60° (UE5/Unity convention). 180° = all smooth, 0° = all flat.
void ComputeSmoothNormals(
    std::vector<VertexPosNormUV>& vertices,
    std::vector<uint32_t>& indices,
    float smoothingAngleDeg = 60.0f);

/// Overload with per-face smoothing flags (from FBX `face_smoothing`).
///
/// When `faceSmoothFlags` is provided, faces marked as smooth (true) will
/// average normals with adjacent smooth faces regardless of angle. Faces
/// marked as flat (false) always get hard edges. The angle threshold still
/// applies between smooth faces to detect sharp creases.
///
/// @param faceSmoothFlags  One bool per triangle. Empty = use angle threshold only.
void ComputeSmoothNormals(
    std::vector<VertexPosNormUV>& vertices,
    std::vector<uint32_t>& indices,
    float smoothingAngleDeg,
    const std::vector<bool>& faceSmoothFlags);

} // namespace uz
