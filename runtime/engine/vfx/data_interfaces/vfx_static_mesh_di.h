#pragma once

/// Static-mesh surface-sampling data interface for VFX simulation shaders.
///
/// Niagara exposes `UNiagaraDataInterfaceStaticMesh` with random-triangle
/// sampling, closest-point queries, and UV-driven spawn. The most common
/// use case — uniform-by-area surface scatter — drives this header's API.
///
/// # Representation
///
/// Three parallel structured buffers:
///   - `positions`: `Vec3[vertexCount]` in mesh-local space.
///   - `normals`:   `Vec3[vertexCount]` mesh-local unit normals.
///   - `uvs`:       `Vec2[vertexCount]` UV coords.
///   - `indices`:   `uint32[triangleCount * 3]` triangle index list.
///   - `triangleAreaCdf`: `float[triangleCount]` cumulative normalised
///     triangle areas in `[0, 1]`. Built by `BuildAreaCdf`.
///
/// # Sampling algorithm
///
/// `SampleSurface(rng)` takes three uniform `[0, 1)` random floats and
/// returns `{position, normal, uv}`:
///
/// 1. Binary-search `triangleAreaCdf` with `rng.x` to pick a triangle
///    weighted by its area.
/// 2. Generate barycentric coords from `rng.y, rng.z` via
///    `(1 - sqrt(u), sqrt(u) * (1 - v), sqrt(u) * v)` — the standard
///    uniform-triangle distribution from Turk 1990.
/// 3. Interpolate position / normal / UV with the barycentrics.
///
/// The CPU reference here is what the HLSL sampler port (deferred
/// follow-up) must match.
///
/// # Example
///
/// ```cpp
/// VfxStaticMeshDI mesh;
/// mesh.positions = {...};
/// mesh.normals   = {...};
/// mesh.uvs       = {...};
/// mesh.indices   = {...};
/// mesh.BuildAreaCdf();
///
/// VfxMeshSurfaceSample s = mesh.SampleSurface(0.2f, 0.7f, 0.1f);
/// ```
///
/// @ref UE5 UNiagaraDataInterfaceStaticMesh
/// @ref Turk 1990 "Generating Random Points in Triangles"

#include "vector2.h"
#include "vector3.h"

#include <cstdint>
#include <vector>

namespace uz {

/// Result of one surface sample.
struct VfxMeshSurfaceSample {
    Vec3 position {};   ///< Mesh-local point on the surface.
    Vec3 normal   {};   ///< Mesh-local unit normal (interpolated, re-normalised).
    Vec2 uv       {};   ///< UV coord interpolated from the three vertices.
    uint32_t triangleIndex = 0;  ///< Which triangle the sample came from.
};

/// CPU-side static-mesh surface sampler. Uploaded as bindless SRVs to
/// the GPU by the follow-up binding task; the CPU reference here is the
/// contract those SRVs must honour.
struct VfxStaticMeshDI {
    std::vector<Vec3>     positions;        ///< Per-vertex mesh-local position.
    std::vector<Vec3>     normals;          ///< Per-vertex mesh-local normal.
    std::vector<Vec2>     uvs;              ///< Per-vertex UV.
    std::vector<uint32_t> indices;          ///< Triangle indices (size % 3 == 0).

    /// Prefix-sum of triangle areas normalised to `[0, 1]`. Populated by
    /// `BuildAreaCdf`; last entry is 1.0.
    mutable std::vector<float> triangleAreaCdf;

    /// Sum of all triangle areas. Valid after `BuildAreaCdf`.
    mutable float totalArea = 0.0f;

    /// Number of triangles (indices.size() / 3).
    [[nodiscard]] uint32_t GetTriangleCount() const noexcept {
        return static_cast<uint32_t>(indices.size() / 3u);
    }

    /// Builds the area CDF from the current vertex/index arrays. Must be
    /// called after mutating `positions` or `indices` for `SampleSurface`
    /// to produce valid results. Cheap — O(triangleCount).
    void BuildAreaCdf() const;

    /// Returns a uniformly-area-weighted random sample on the mesh surface.
    ///
    /// `rng0`, `rng1`, `rng2` must be in `[0, 1)`. Values outside that
    /// range are clamped. Returns an empty sample when the mesh has no
    /// triangles or `BuildAreaCdf` has not run (or produced zero area).
    [[nodiscard]] VfxMeshSurfaceSample SampleSurface(float rng0, float rng1,
                                                     float rng2) const;
};

/// Generates the three barycentric weights `(a, b, c)` with `a + b + c == 1`
/// from two uniform `[0, 1)` samples, producing a uniform distribution over
/// the triangle interior. Exposed separately so HLSL tests and future
/// debug tooling can call the same helper without the CDF lookup.
///
/// @ref Turk 1990 "Generating Random Points in Triangles"
struct VfxBarycentric { float a = 0.0f; float b = 0.0f; float c = 0.0f; };
[[nodiscard]] VfxBarycentric VfxUniformBarycentric(float u, float v) noexcept;

} // namespace uz
