/// VFX Frustum Culling — sphere-frustum intersection for emitter visibility.
///
/// Provides frustum plane extraction from a view-projection matrix and
/// sphere-frustum intersection tests for CPU-side emitter culling. Includes
/// per-emitter cull state with 2-frame hysteresis to prevent visual popping
/// when emitters cross frustum boundaries.
///
/// # References
/// - Gribb & Hartmann 2001 "Fast Extraction of Viewing Frustum Planes
///   from the World-View-Projection Matrix"
/// - Assarsson & Moller 2000 "Optimized View Frustum Culling Algorithms"
///
/// # Thread Safety
/// All free functions are stateless and thread-safe.
/// `EmitterCullState` is NOT thread-safe — each emitter owns its own instance.

#pragma once

#include "vector3.h"
#include "vector4.h"
#include "mat4.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// FrustumPlanes — six frustum half-spaces from a view-projection matrix
// ─────────────────────────────────────────────────────────────────────────────

/// Indices into `FrustumPlanes::planes[]`.
enum class FrustumPlane : uint8_t {
    Left   = 0,
    Right  = 1,
    Bottom = 2,
    Top    = 3,
    Near   = 4,
    Far    = 5,
};

/// Six frustum planes extracted from a view-projection matrix.
///
/// Each plane is stored as `(A, B, C, D)` in a Vec4, where the plane equation
/// is `Ax + By + Cz + D >= 0` for points inside the frustum. Planes are
/// normalized (||normal|| = 1) so that signed distance tests produce correct
/// metric distances.
///
/// @ref Gribb & Hartmann 2001 "Fast Extraction of Viewing Frustum Planes"
struct FrustumPlanes {
    /// Frustum planes: Left, Right, Bottom, Top, Near, Far.
    Vec4 planes[6];
};

// ─────────────────────────────────────────────────────────────────────────────
// Frustum Plane Extraction
// ─────────────────────────────────────────────────────────────────────────────

/// Extracts 6 frustum planes from a combined view-projection matrix.
///
/// Uses the Gribb-Hartmann method: for row-vector convention (`clip = point * VP`),
/// planes are derived from columns of the VP matrix. Each plane is normalized so
/// that the normal vector `(A, B, C)` has unit length, enabling correct signed
/// distance tests.
///
/// The resulting planes define the frustum interior via `Ax + By + Cz + D >= 0`.
///
/// # Arguments
/// * `viewProj` — Combined view * projection matrix (row-vector convention).
///
/// # Returns
/// Six normalized frustum planes in the order: Left, Right, Bottom, Top, Near, Far.
///
/// @ref Gribb & Hartmann 2001 "Fast Extraction of Viewing Frustum Planes"
[[nodiscard]] FrustumPlanes ExtractFrustumPlanes(const Mat4& viewProj) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Sphere-Frustum Intersection
// ─────────────────────────────────────────────────────────────────────────────

/// Tests whether a sphere intersects or is inside the frustum.
///
/// A sphere is considered visible if its signed distance to every frustum plane
/// is >= -radius. This catches full containment, partial intersection, and
/// rejects spheres that are entirely outside any single plane.
///
/// # Arguments
/// * `frustum` — Six normalized frustum planes (from `ExtractFrustumPlanes`).
/// * `center`  — World-space center of the bounding sphere.
/// * `radius`  — Radius of the bounding sphere (must be >= 0).
///
/// # Returns
/// `true` if the sphere is at least partially inside the frustum.
///
/// @ref Assarsson & Moller 2000 "Optimized View Frustum Culling Algorithms"
[[nodiscard]] bool IsSphereInsideFrustum(const FrustumPlanes& frustum,
                                         const Vec3& center,
                                         float radius) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Emitter Bounding Sphere
// ─────────────────────────────────────────────────────────────────────────────

/// Computes a conservative bounding sphere radius for a particle emitter.
///
/// The worst-case extent of a particle system is bounded by the spawn region
/// radius plus the maximum distance any particle can travel during its lifetime.
/// This is a deliberately conservative estimate — the actual particle cloud is
/// often smaller, but underestimating causes visible popping.
///
/// # Formula
/// `radius = spawnRadius + maxSpeed * maxLifetime`
///
/// # Arguments
/// * `spawnRadius`  — Radius of the emitter's spawn region (centimetres).
/// * `maxSpeed`     — Maximum initial particle speed (cm/s).
/// * `maxLifetime`  — Maximum particle lifetime (seconds).
///
/// # Returns
/// Conservative bounding sphere radius in centimetres. Always >= 0.
///
/// @ref Assarsson & Moller 2000 "Optimized View Frustum Culling Algorithms"
[[nodiscard]] float ComputeEmitterBoundingSphereRadius(float spawnRadius,
                                                       float maxSpeed,
                                                       float maxLifetime) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// EmitterCullState — per-emitter cull state with 2-frame hysteresis
// ─────────────────────────────────────────────────────────────────────────────

/// Per-emitter culling state with 2-frame hysteresis to prevent popping.
///
/// Emitters that cross frustum boundaries can rapidly alternate between
/// visible and culled states, causing visible pop-in/pop-out artifacts.
/// This struct tracks cull state across two consecutive frames: an emitter
/// is only truly culled when it has been outside the frustum for 2 frames
/// in a row.
///
/// # Usage Pattern (in game loop or VFXParticleSystem)
/// ```cpp
/// FrustumPlanes frustum = ExtractFrustumPlanes(viewProj);
/// float radius = ComputeEmitterBoundingSphereRadius(spawnRadius, maxSpeed, maxLifetime);
/// bool visible = IsSphereInsideFrustum(frustum, emitterPosition, radius);
/// bool shouldRender = cullState.Update(visible);
/// if (shouldRender) { Dispatch(); Draw(); }
/// ```
struct EmitterCullState {
    /// Whether the emitter was culled in the current frame's frustum test.
    bool culledThisFrame = false;
    /// Whether the emitter was culled in the previous frame's frustum test.
    bool culledPrevFrame = false;
    /// Number of consecutive frames the emitter has been culled.
    /// Useful for respawn burst logic when transitioning back to visible.
    uint32_t framesSinceCulled = 0;

    /// Updates cull state for this frame and returns whether the emitter should
    /// be dispatched and rendered.
    ///
    /// The hysteresis rule: an emitter is only suppressed when it has been culled
    /// for 2 consecutive frames. During the transition frame (one culled, one not),
    /// the emitter remains active to hide popping.
    ///
    /// # Arguments
    /// * `isInsideFrustum` — Result of the sphere-frustum test for this frame.
    ///
    /// # Returns
    /// `true` if the emitter should be dispatched/rendered (visible or within
    /// the hysteresis window); `false` if it should be fully suppressed.
    bool Update(bool isInsideFrustum) noexcept;
};

} // namespace uz
