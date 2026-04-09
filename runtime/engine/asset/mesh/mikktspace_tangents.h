/// UzEngine MikkTSpace Tangent Generator
///
/// Industry-standard tangent space generation using Morten Mikkelsen's
/// mikktspace library. Produces tangents that match UE5, Unity, Blender,
/// and other DCC tools — eliminates normal map seams on mirrored UVs.
///
/// @ref Mikkelsen 2010 "Simulation of Wrinkled Surfaces Revisited"

#pragma once

#include <cstdint>
#include <vector>

namespace uz {

struct VertexPosNormUV;

/// Generate MikkTSpace tangents for a triangle mesh.
///
/// Computes per-vertex tangent frames that are compatible with normal maps
/// baked in Blender, Substance, xNormal, etc. Output is 4 floats per vertex:
/// tangent xyz + bitangent sign w (+1 or -1).
///
/// @param vertices   Vertex array with valid positions, normals, and UVs.
/// @param indices    Triangle index array.
/// @param tangentsOut  Output: 4 floats per vertex (T.x, T.y, T.z, sign).
///                     Resized to vertices.size() * 4.
void ComputeMikkTSpaceTangents(
    const std::vector<VertexPosNormUV>& vertices,
    const std::vector<uint32_t>& indices,
    std::vector<float>& tangentsOut);

} // namespace uz
