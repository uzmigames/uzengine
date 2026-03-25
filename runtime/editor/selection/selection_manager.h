/// SelectionManager — Manages single-entity selection in editor mode.
///
/// Provides entity selection via direct `Select(entity)` or screen-space
/// picking via `SelectAtScreenPos()`. Picking constructs a ray from screen
/// coordinates through the inverse view-projection matrix and tests it
/// against all entities' world-space AABBs using the slab method.
///
/// # Reversed-Z Depth
/// UzEngine uses reversed-Z (near=1, far=0). The unprojection handles this
/// by mapping NDC Z=1 to the near plane and Z=0 to the far plane.
///
/// # Thread Safety
/// NOT thread-safe. All calls must be from the main thread.

#pragma once

#include "entity.h"
#include "vector3.h"
#include "mat4.h"

namespace uz {

class Camera;
class World;
class SceneRenderer;

// ─────────────────────────────────────────────────────────────────────────────
// SelectionManager
// ─────────────────────────────────────────────────────────────────────────────

/// Manages single-entity selection for the editor.
///
/// # Design
/// Currently supports single selection only. Multi-selection (Phase 2)
/// will replace the single `m_selected` field with a set.
///
/// # Ownership
/// Does NOT own any entities. The Entity handle may become stale if the
/// entity is destroyed in the World — callers should validate with
/// `World::IsValid()` before using the selected entity.
class SelectionManager {
public:
    /// Selects an entity by handle.
    ///
    /// # Arguments
    /// * `entity` — The entity to select. May be invalid (equivalent to Deselect).
    void Select(Entity entity);

    /// Clears the current selection.
    void Deselect();

    /// Returns the currently selected entity handle.
    ///
    /// The returned handle may be invalid (no selection) or stale (entity destroyed).
    /// Always validate with `World::IsValid()` before accessing components.
    [[nodiscard]] Entity GetSelected() const { return m_selected; }

    /// Returns true if an entity is currently selected.
    ///
    /// This is a local check only — does NOT verify the entity is still alive.
    [[nodiscard]] bool HasSelection() const { return m_selected.IsValid(); }

    /// Picks the nearest entity at screen position via ray-AABB intersection.
    ///
    /// Constructs a world-space ray from the screen coordinates, then iterates
    /// all entities with SceneComponent + StaticMeshComponent and tests the ray
    /// against their world-space AABBs. Selects the nearest hit; deselects if
    /// no entity is hit.
    ///
    /// # Arguments
    /// * `screenX` — Mouse X position in window-relative pixels.
    /// * `screenY` — Mouse Y position in window-relative pixels.
    /// * `viewportWidth` — Viewport width in pixels.
    /// * `viewportHeight` — Viewport height in pixels.
    /// * `camera` — Camera for ray construction (view + projection matrices).
    /// * `world` — ECS world containing entities to test against.
    /// * `renderer` — Scene renderer for mesh bounds lookup. May be null
    ///   (falls back to unit-cube AABB).
    void SelectAtScreenPos(float screenX, float screenY,
                           float viewportWidth, float viewportHeight,
                           const Camera* camera, World* world,
                           const SceneRenderer* renderer);

private:
    Entity m_selected{};
};

// ─────────────────────────────────────────────────────────────────────────────
// Ray-AABB intersection (slab method)
// ─────────────────────────────────────────────────────────────────────────────

/// Tests whether a ray intersects an axis-aligned bounding box using the
/// slab method (Kay & Kajiya 1986).
///
/// # Arguments
/// * `rayOrigin` — Ray origin in world space.
/// * `rayDir` — Normalized ray direction.
/// * `aabbMin` — AABB minimum corner.
/// * `aabbMax` — AABB maximum corner.
/// * `tMin` — [out] Entry distance along the ray (may be negative if origin is inside).
/// * `tMax` — [out] Exit distance along the ray.
///
/// # Returns
/// `true` if the ray intersects the AABB and `tMax >= 0` (hit is in front of ray origin).
[[nodiscard]] bool RayIntersectsAABB(const Vec3& rayOrigin, const Vec3& rayDir,
                                      const Vec3& aabbMin, const Vec3& aabbMax,
                                      float& tMin, float& tMax);

/// Unprojects a normalized device coordinate point to world space.
///
/// # Arguments
/// * `invViewProj` — Inverse of the view-projection matrix.
/// * `ndcX` — X in NDC space [-1, 1].
/// * `ndcY` — Y in NDC space [-1, 1].
/// * `ndcZ` — Z in NDC space [0, 1]. For reversed-Z: 1 = near, 0 = far.
///
/// # Returns
/// The world-space position corresponding to the given NDC point.
[[nodiscard]] Vec3 UnprojectPoint(const Mat4& invViewProj,
                                   float ndcX, float ndcY, float ndcZ);

} // namespace uz
