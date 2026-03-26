/// DecalManager -- Central manager for the decal subsystem.
///
/// Owns the DecalPool and per-channel DecalAtlases (albedo, normal, roughness).
/// Provides a high-level API for spawning, updating, frustum-culling, and
/// sort-ordering decals for rendering.
///
/// # Lifecycle
/// 1. Construct (default)
/// 2. `Initialize(device, poolSize, atlasSize)` -- allocates pool and atlases
/// 3. Per-frame: `Update(dt)` then `FrustumCull(viewProj)` then `GetSortedDecals(cameraPos)`
/// 4. `Shutdown(device)` -- releases all resources
///
/// # Thread Safety
/// NOT thread-safe. All methods must be called from the main/render thread.
///
/// # Example
/// ```cpp
/// DecalManager mgr;
/// mgr.Initialize(device, 1000, 2048);
///
/// // On bullet impact:
/// auto asset = DecalAsset::Load("decals/bullet_hole.uzdecal");
/// VfxDecal* d = mgr.SpawnDecal(*asset, hitPos, hitNormal);
///
/// // Per frame:
/// mgr.Update(dt);
/// mgr.FrustumCull(viewProj);
/// const auto& visible = mgr.GetSortedDecals(camera.position);
/// for (const auto* decal : visible) { /* render */ }
///
/// mgr.Shutdown(device);
/// ```

#pragma once

#include "engine/math/quaternion.h"
#include "engine/math/vector3.h"

#include <cstdint>
#include <memory>
#include <random>
#include <vector>

namespace uz {

// Forward declarations -- avoid pulling in full headers.
class IRHIDevice;
class DecalPool;
class DecalAtlas;
struct DecalAsset;
struct VfxDecal;
struct Mat4;

// ---------------------------------------------------------------------------
// DecalManager -- high-level decal system facade
// ---------------------------------------------------------------------------

/// Central manager that owns the decal pool and texture atlases.
///
/// Provides the public API for spawning, ticking, culling, and retrieving
/// render-ready decals. Non-copyable; movable for container storage.
///
/// # Ownership
/// - Owns `DecalPool` (unique_ptr) -- all VfxDecal memory lives here.
/// - Owns three `DecalAtlas` instances (albedo, normal, roughness).
/// - Sorted decal list is a cached view into the pool; pointers are non-owning.
///
/// # Rendering Integration
/// Call `GetSortedDecals(cameraPos)` after `FrustumCull()` to obtain a
/// back-to-front ordered list suitable for the deferred decal pass.
class DecalManager {
public:
    // -- Lifecycle ------------------------------------------------------------

    /// Default constructor. The manager is in an uninitialized state until
    /// `Initialize()` is called.
    DecalManager();
    ~DecalManager();

    /// Non-copyable (owns pool and atlas resources).
    DecalManager(const DecalManager&) = delete;
    DecalManager& operator=(const DecalManager&) = delete;

    /// Movable (transfers ownership of pool, atlases, and RNG state).
    DecalManager(DecalManager&& other) noexcept;
    DecalManager& operator=(DecalManager&& other) noexcept;

    /// Creates the decal pool and atlas resources.
    ///
    /// Must be called exactly once before any other method. Calling on an
    /// already-initialized manager logs a warning and returns without effect.
    ///
    /// # Arguments
    /// * `device`    -- RHI device for GPU resource creation (may be nullptr
    ///                  if atlases are CPU-only bookkeeping, as in the current
    ///                  implementation).
    /// * `poolSize`  -- Maximum simultaneous decals. Default: 1000.
    /// * `atlasSize` -- Square atlas dimension in pixels. Default: 2048.
    ///
    /// # Side Effects
    /// - Allocates pool backing store (`poolSize` * sizeof(VfxDecal)).
    /// - Creates three atlas instances (albedo, normal, roughness).
    /// - Logs `VFX/Info` on success.
    void Initialize(IRHIDevice* device,
                    uint32_t poolSize = 1000,
                    uint32_t atlasSize = 2048);

    /// Releases all resources owned by the manager.
    ///
    /// Returns all active decals to the pool, resets atlases, and destroys
    /// internal storage. Safe to call on an uninitialized manager (no-op).
    ///
    /// # Arguments
    /// * `device` -- RHI device (reserved for future GPU resource teardown).
    ///
    /// # Side Effects
    /// - Logs `VFX/Info` on shutdown with active decal count at time of release.
    void Shutdown(IRHIDevice* device);

    /// Returns true if `Initialize()` has been called and `Shutdown()` has not.
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }

    // -- Spawn API ------------------------------------------------------------

    /// Spawns a decal from an asset template at the given surface hit point.
    ///
    /// Acquires a slot from the pool, copies configuration from the asset,
    /// positions the decal slightly off the surface (0.01 units along normal),
    /// aligns the decal's projection axis to the surface normal, and applies
    /// a random rotation around the normal for visual variety.
    ///
    /// # Arguments
    /// * `asset`            -- Decal asset defining material, blend mode, etc.
    /// * `position`         -- World-space hit point on the surface.
    /// * `surfaceNormal`    -- Unit normal of the surface at the hit point.
    /// * `lifetimeOverride` -- If > 0, overrides the asset's lifetime (seconds).
    ///                         Default: -1 (use asset lifetime).
    ///
    /// # Returns
    /// - Non-owning pointer to the configured VfxDecal on success.
    /// - `nullptr` if the pool is exhausted or the manager is uninitialized.
    ///
    /// # Side Effects
    /// - Logs `VFX/Debug` on successful spawn with material path.
    /// - Logs `VFX/Warning` if pool is exhausted (via DecalPool).
    [[nodiscard]] VfxDecal* SpawnDecal(const DecalAsset& asset,
                                             const Vec3& position,
                                             const Vec3& surfaceNormal,
                                             float lifetimeOverride = -1.0f);

    // -- Per-frame Update -----------------------------------------------------

    /// Ticks all active decals (advances age, updates fade) and removes expired ones.
    ///
    /// Should be called once per frame before rendering.
    ///
    /// # Arguments
    /// * `dt` -- Frame delta time in seconds. Must be >= 0.
    ///
    /// # Side Effects
    /// - Calls `VfxDecal::Tick(dt)` on every active decal.
    /// - Returns expired/destroyed decals to the pool via `Cleanup()`.
    void Update(float dt);

    // -- Rendering ------------------------------------------------------------

    /// Returns active decals sorted for rendering.
    ///
    /// Sorts by `sortOrder` (ascending, lower renders first) as primary key,
    /// then by distance to camera (descending, back-to-front) as secondary key.
    /// The sorted list is rebuilt on each call and cached until the next call.
    ///
    /// Decals marked as culled by `FrustumCull()` are excluded from the result.
    ///
    /// # Arguments
    /// * `cameraPos` -- World-space camera position for distance sorting.
    ///
    /// # Returns
    /// Const reference to the internal sorted decal pointer vector. Valid until
    /// the next call to `GetSortedDecals()`, `Update()`, or `Shutdown()`.
    [[nodiscard]] const std::vector<VfxDecal*>& GetSortedDecals(
        const Vec3& cameraPos);

    /// Marks decals outside the view frustum for culling.
    ///
    /// Uses a conservative sphere test: each decal's position is tested against
    /// the six frustum planes, with the sphere radius set to the maximum
    /// half-extent component. Culled decals are excluded from `GetSortedDecals()`.
    ///
    /// # Arguments
    /// * `viewProjection` -- Combined view-projection matrix.
    ///
    /// # Side Effects
    /// - Sets/clears internal culling flags on each active decal.
    void FrustumCull(const Mat4& viewProjection);

    // -- Stats ----------------------------------------------------------------

    /// Number of decals currently active (spawned and not yet expired/returned).
    [[nodiscard]] uint32_t GetActiveCount() const noexcept;

    /// Number of decal slots available in the pool.
    [[nodiscard]] uint32_t GetPoolAvailable() const noexcept;

    /// Average occupancy ratio across all three atlases (albedo, normal, roughness).
    ///
    /// # Returns
    /// Value in [0.0, 1.0]. Returns 0.0 if not initialized.
    [[nodiscard]] float GetAtlasOccupancy() const noexcept;

private:
    // -- Helpers --------------------------------------------------------------

    /// Returns expired or destroyed decals to the pool.
    ///
    /// Iterates the active decal list in reverse (safe for swap-and-pop removal)
    /// and returns any decal where `IsExpired()` or `!isActive`.
    void Cleanup();

    /// Creates a quaternion that aligns the local -Z axis with `surfaceNormal`.
    ///
    /// Uses the cross product between world up and the surface normal to find
    /// the rotation axis. Falls back to world right if the normal is nearly
    /// parallel to world up (dot > 0.999).
    ///
    /// # Arguments
    /// * `surfaceNormal` -- Unit vector to align -Z with.
    ///
    /// # Returns
    /// Normalized quaternion whose -Z direction equals `surfaceNormal`.
    [[nodiscard]] static Quat AlignToSurface(const Vec3& surfaceNormal);

    /// Generates a random rotation around the given axis.
    ///
    /// Uniformly samples an angle in [0, 2*pi) and returns an axis-angle
    /// quaternion. Uses the manager's internal `std::mt19937` generator.
    ///
    /// # Arguments
    /// * `axis` -- Rotation axis (should be normalized).
    ///
    /// # Returns
    /// Unit quaternion representing a random rotation around `axis`.
    [[nodiscard]] Quat RandomRotationAroundAxis(const Vec3& axis);

    /// Tests whether a sphere is inside or intersects a frustum.
    ///
    /// Extracts the six frustum planes from the combined view-projection matrix
    /// and tests the sphere (center + radius) against each plane.
    ///
    /// # Arguments
    /// * `viewProjection` -- Combined view-projection matrix.
    /// * `center`         -- Sphere center in world space.
    /// * `radius`         -- Sphere radius.
    ///
    /// # Returns
    /// `true` if the sphere is at least partially inside the frustum.
    [[nodiscard]] static bool IsSphereInFrustum(const Mat4& viewProjection,
                                                const Vec3& center,
                                                float radius);

    // -- Data -----------------------------------------------------------------

    /// Pre-allocated pool of VfxDecal instances.
    std::unique_ptr<DecalPool> pool_;

    /// Shelf-packed texture atlas for decal albedo textures.
    std::unique_ptr<DecalAtlas> albedoAtlas_;

    /// Shelf-packed texture atlas for decal normal maps.
    std::unique_ptr<DecalAtlas> normalAtlas_;

    /// Shelf-packed texture atlas for decal roughness maps.
    std::unique_ptr<DecalAtlas> roughnessAtlas_;

    /// Cached sorted list of visible (non-culled) active decals.
    /// Rebuilt on each `GetSortedDecals()` call.
    std::vector<VfxDecal*> sortedDecals_;

    /// Per-decal culling flag. Indexed by pool slot offset.
    /// True = culled (not visible). Resized to pool capacity on Initialize().
    std::vector<bool> culledFlags_;

    /// Mersenne Twister RNG for random decal rotation.
    std::mt19937 rng_;

    /// True after Initialize(), false after Shutdown() or before first init.
    bool initialized_ = false;
};

} // namespace uz
