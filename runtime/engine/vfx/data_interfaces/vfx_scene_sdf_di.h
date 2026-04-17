#pragma once

/// VFX Scene SDF sampling data interface.
///
/// Particles read the scene's Signed Distance Field to:
///   - Perform coarse collision against walls / meshes (cheaper than
///     raycasting the depth buffer from phase 4.23).
///   - Light up / attenuate when near geometry ("glow-through-walls").
///
/// The atlas aggregates per-mesh SDF bakes into a single bindless
/// `Texture3D<float>` covering an AABB worth of scene. Each mesh
/// contributes its own 3D brick; the atlas build combines them by taking
/// the per-voxel minimum (union of SDFs).
///
/// # Sampling contract
///
/// `SampleSceneSDF(worldPos)` returns `(distance, gradient)`:
///
///   - **distance** is the signed distance to the nearest surface
///     (negative inside, positive outside).
///   - **gradient** is the normalised 3D spatial derivative, pointing
///     *away* from the nearest surface. Normals (outward surface normals)
///     are `normalize(gradient)`; collision push-out uses it verbatim.
///
/// Gradient is computed with a 3-sample central difference:
///
/// ```
/// grad.x = (sample(p + ε·x̂) - sample(p - ε·x̂)) / (2·ε)
/// grad.y = (sample(p + ε·ŷ) - sample(p - ε·ŷ)) / (2·ε)
/// grad.z = (sample(p + ε·ẑ) - sample(p - ε·ẑ)) / (2·ε)
/// ```
///
/// # Collision push-out
///
/// Particles colliding with the scene push out along the gradient:
///
/// ```
/// if (dist < radius) {
///     position += normalize(gradient) * (radius - dist);
///     velocity -= gradient · dot(velocity, gradient);  // reflect tangent
/// }
/// ```
///
/// @ref UE5 UNiagaraDataInterfaceDistanceField
/// @ref docs/engine/25-vfx/39-scene-sdf.md

#include "vector3.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Atlas descriptor
// ─────────────────────────────────────────────────────────────────────────────

/// Atlas bounds + resolution. Each voxel stores one signed-distance value
/// in metres (positive outside, negative inside).
struct VfxSceneSDFAtlasDesc {
    /// Bindless SRV index of the Texture3D<float>.
    uint32_t atlasSrvIndex = 0xFFFFFFFFu;

    /// World-space AABB covered by the atlas.
    Vec3 worldMin{0, 0, 0};
    Vec3 worldMax{0, 0, 0};

    /// Voxel grid resolution per axis. Total voxels = res.x·res.y·res.z.
    uint32_t resX = 0u;
    uint32_t resY = 0u;
    uint32_t resZ = 0u;

    [[nodiscard]] bool IsBound() const noexcept;
};

/// Returns the world-space voxel size (edge length) on each axis.
[[nodiscard]] Vec3 VfxSceneSDFVoxelSize(const VfxSceneSDFAtlasDesc& desc) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Analytic SDF primitives (used for CPU tests, matching shader fallbacks)
// ─────────────────────────────────────────────────────────────────────────────

/// Signed distance from `p` to a sphere at `center` with `radius`.
/// Positive outside, negative inside.
[[nodiscard]] float VfxSDFSphere(const Vec3& p, const Vec3& center, float radius) noexcept;

/// Signed distance from `p` to an axis-aligned box centered at `center`
/// with `halfExtents`. Equivalent to the standard `sdBox`.
[[nodiscard]] float VfxSDFBox(const Vec3& p, const Vec3& center,
                                const Vec3& halfExtents) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Central-difference gradient sampler (takes any sampler functor)
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the gradient of an SDF at `p` via central differences with
/// step `epsilon`. The `sampler` takes a world-space position and returns
/// the signed distance. Works with any analytic SDF or a baked texture
/// sampler.
template <typename Sampler>
[[nodiscard]] Vec3 VfxSceneSDFGradient(const Sampler& sampler,
                                         const Vec3& p,
                                         float epsilon) noexcept {
    const float dx = sampler(Vec3{p.x + epsilon, p.y, p.z}) -
                     sampler(Vec3{p.x - epsilon, p.y, p.z});
    const float dy = sampler(Vec3{p.x, p.y + epsilon, p.z}) -
                     sampler(Vec3{p.x, p.y - epsilon, p.z});
    const float dz = sampler(Vec3{p.x, p.y, p.z + epsilon}) -
                     sampler(Vec3{p.x, p.y, p.z - epsilon});
    const float inv = 1.0f / (2.0f * epsilon);
    return Vec3{dx * inv, dy * inv, dz * inv};
}

// ─────────────────────────────────────────────────────────────────────────────
// Collision response
// ─────────────────────────────────────────────────────────────────────────────

/// Particle-SDF collision response. When the particle is inside the
/// surface (distance < radius) pushes it out along the gradient and
/// reflects the velocity's tangent component.
///
/// # Arguments
/// - `position`   — particle position (in/out).
/// - `velocity`   — particle velocity (in/out).
/// - `distance`   — signed distance from SDF sample.
/// - `gradient`   — 3D gradient from `VfxSceneSDFGradient`.
/// - `radius`     — particle collision radius (metres).
/// - `restitution` — bounce factor in [0, 1]; 0 absorbs, 1 reflects.
///
/// # Returns
/// `true` when a collision was resolved (state modified), `false`
/// otherwise.
[[nodiscard]] bool VfxSceneSDFResolveCollision(Vec3& position, Vec3& velocity,
                                                  float distance,
                                                  const Vec3& gradient,
                                                  float radius,
                                                  float restitution) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// HLSL snippet
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the HLSL snippet that declares `SampleSceneSDF(worldPos)` and
/// a `GradientSceneSDF(worldPos, eps)` central-difference helper.
[[nodiscard]] const char* VfxSceneSDFEmitHLSLSnippet() noexcept;

} // namespace uz
