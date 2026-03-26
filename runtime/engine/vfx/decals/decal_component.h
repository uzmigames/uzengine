#pragma once

/// VfxDecal -- ECS component for projected decals.
///
/// Decals are projected onto scene geometry using an oriented bounding box (OBB).
/// The decal's local -Z axis is the projection direction. World-space points inside
/// the OBB are mapped to normalized [-1,1] coordinates for texture sampling.
///
/// # Lifecycle
/// 1. Attach VfxDecal to an entity with position/rotation
/// 2. DecalSystem ticks all decals each frame (age, fade, expiry)
/// 3. Expired decals are reclaimed by the decal pool
///
/// # Thread Safety
/// Not thread-safe. All methods must be called on the main thread.
///
/// # Example
/// ```cpp
/// VfxDecal decal;
/// decal.position = impactPoint;
/// decal.rotation = Quat::FromAxisAngle(surfaceNormal, 0.0f);
/// decal.materialPath = "materials/decals/bullet_hole.uzmat";
/// decal.lifetime = 15.0f;
/// ```

#include "mat4.h"
#include "quaternion.h"
#include "vector3.h"

#include <cstdint>
#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// DecalBlendMode -- How the decal composites onto the surface
// ─────────────────────────────────────────────────────────────────────────────

/// Blend mode for decal compositing onto the underlying surface.
///
/// Controls how the decal's color output is combined with the GBuffer values
/// beneath it. The default (Opaque) replaces the surface albedo/normal/roughness
/// within the projection volume.
enum class DecalBlendMode : uint8_t {
    Opaque   = 0, ///< Replaces surface properties (standard decal behavior).
    Additive = 1, ///< Adds decal color to surface (glow, scorch marks).
    Multiply = 2, ///< Multiplies with surface color (dirt, stains).
    Screen   = 3, ///< Inverse multiply -- brightens (wet surfaces, highlights).
};

/// Converts a DecalBlendMode to its string representation.
///
/// # Returns
/// Human-readable name (e.g. "Opaque", "Additive"). Returns "Unknown" for
/// invalid enum values.
[[nodiscard]] const char* DecalBlendModeToString(DecalBlendMode mode) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// VfxDecal -- Projected decal ECS component (pure data + methods)
// ─────────────────────────────────────────────────────────────────────────────

/// Projected decal attached to scene entities.
///
/// Defines an oriented bounding box (OBB) that projects a material (albedo,
/// normal, roughness) onto intersecting scene geometry. The projection axis
/// is the decal's local -Z direction, determined by `rotation`.
///
/// The component carries both configuration (material, blend mode, sort order)
/// and runtime state (age, fadeAlpha, isActive). `Tick()` advances the age
/// and computes a linear fade; curve-based fade is a planned future enhancement.
///
/// # Projection Matrix
/// `GetProjectionMatrix()` builds a world-to-decal-local transform that maps
/// world positions inside the OBB to normalized [-1,1] coordinates suitable
/// for texture sampling. The transform chain is:
/// 1. Translate by -position (center at origin)
/// 2. Rotate by inverse rotation (align OBB axes to world axes)
/// 3. Scale by 1/halfExtents (normalize to [-1,1] cube)
///
/// # Fade & Lifetime
/// Decals fade linearly from full opacity to invisible over their lifetime.
/// `FadeOut(duration)` can override the remaining lifetime to trigger an
/// early fade from the current state. Once `age >= lifetime`, `isActive`
/// is set to false and the decal pool can reclaim the slot.
struct VfxDecal {
    // -- Configuration --------------------------------------------------------

    /// World-space position of the decal's OBB center.
    Vec3 position;

    /// Orientation of the decal. The local -Z axis is the projection direction.
    /// Default is identity (projects along world -Z).
    Quat rotation;

    /// Half-extents of the projection OBB (local X, Y, Z).
    /// X and Y control the decal's surface footprint; Z controls projection depth.
    /// Default: 1m x 1m footprint, 0.5m projection depth.
    Vec3 halfExtents{1.0f, 1.0f, 0.5f};

    /// Path to the decal material asset (albedo, normal map, roughness).
    /// Resolved through the engine's virtual filesystem.
    std::string materialPath;

    /// How strongly the decal overrides the surface normal.
    /// 0 = no normal override (surface normal preserved), 1 = full override.
    /// Clamped to [0, 1] during rendering.
    float normalIntensity = 0.8f;

    /// Surface roughness override value.
    /// -1.0 = no override (surface roughness preserved).
    ///  0.0-1.0 = overrides the surface roughness within the decal footprint.
    float roughnessOverride = -1.0f;

    /// How long (seconds) the decal lives before fully fading out.
    /// After `age >= lifetime`, the decal is considered expired and inactive.
    float lifetime = 10.0f;

    /// How the decal composites onto the surface beneath it.
    DecalBlendMode blendMode = DecalBlendMode::Opaque;

    /// Rendering sort order. Higher values render on top of lower values.
    /// Decals with the same sort order have undefined relative ordering.
    int32_t sortOrder = 0;

    // -- Runtime State (managed by DecalSystem, do not modify directly) -------

    /// Current age in seconds. Incremented by `Tick(dt)` each frame.
    float age = 0.0f;

    /// Current fade alpha (1.0 = fully opaque, 0.0 = fully invisible).
    /// Computed by `Tick()` as a linear fade: `1.0 - (age / lifetime)`.
    float fadeAlpha = 1.0f;

    /// Whether this decal should be rendered. Set to false when expired
    /// or explicitly marked for destruction.
    bool isActive = true;

    // -- Methods --------------------------------------------------------------

    /// Builds the world-to-decal-local projection matrix.
    ///
    /// Transforms world-space points into normalized [-1,1] decal-local
    /// coordinates. Points inside the OBB map to the [-1,1] cube; points
    /// outside are clipped by the decal shader.
    ///
    /// The transform chain is:
    /// 1. Translate world point by -position (center OBB at origin)
    /// 2. Rotate by inverse of `rotation` (align OBB to axis-aligned cube)
    /// 3. Scale by (1/halfExtents.x, 1/halfExtents.y, 1/halfExtents.z)
    ///
    /// # Returns
    /// A 4x4 matrix that transforms world-space positions into decal-local
    /// normalized coordinates. The matrix is valid as long as no halfExtent
    /// component is zero.
    ///
    /// # Preconditions
    /// - `halfExtents` components must be > 0 (zero causes division by zero).
    /// - `rotation` should be normalized for correct results.
    [[nodiscard]] Mat4 GetProjectionMatrix() const;

    /// Advances the decal's age and updates fade state.
    ///
    /// Increments `age` by `dt`. If `age >= lifetime`, sets `fadeAlpha` to 0
    /// and `isActive` to false (marking the decal as expired). Otherwise
    /// computes a linear fade: `fadeAlpha = 1.0 - (age / lifetime)`.
    ///
    /// # Arguments
    /// * `dt` -- Frame delta time in seconds. Must be >= 0.
    ///
    /// # Side Effects
    /// - Modifies `age`, `fadeAlpha`, and potentially `isActive`.
    /// - Logs VFX/Debug when a decal expires (includes materialPath).
    void Tick(float dt);

    /// Begins an early fade-out from the current state.
    ///
    /// Adjusts `lifetime` so that the decal will finish fading after
    /// `duration` more seconds from its current age. If `duration <= 0`,
    /// the decal is immediately expired.
    ///
    /// # Arguments
    /// * `duration` -- Seconds over which to fade from current alpha to zero.
    ///                 Clamped to >= 0.
    ///
    /// # Side Effects
    /// - Modifies `lifetime`. May modify `isActive` and `fadeAlpha` if
    ///   duration is zero or negative.
    void FadeOut(float duration);

    /// Marks this decal for immediate destruction.
    ///
    /// Sets `isActive` to false, signaling the decal pool to reclaim
    /// this slot on its next cleanup pass. Does not modify age or lifetime.
    void MarkForDestruction() noexcept;

    /// Checks whether this decal has exceeded its lifetime.
    ///
    /// # Returns
    /// `true` if `age >= lifetime`, `false` otherwise.
    [[nodiscard]] bool IsExpired() const noexcept;
};

} // namespace uz
