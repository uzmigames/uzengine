/// GizmoInteraction — Hit testing and drag interaction for editor transform gizmos.
///
/// Manages the full interaction lifecycle for translate/rotate/scale gizmos:
/// 1. Hit testing: screen-space mouse position vs projected gizmo axis handles.
/// 2. Drag begin: captures initial state (entity transform, mouse position, axis direction).
/// 3. Drag update: computes transform delta from mouse movement projected onto the drag axis.
/// 4. Drag end: clears drag state.
///
/// # Hit Testing Approach
/// Projects gizmo axis endpoints to screen space, then measures 2D pixel distance
/// from the mouse cursor to each projected axis segment. This avoids complex 3D
/// ray-line closest-point math and naturally handles perspective foreshortening.
///
/// # Drag Approach
/// For translate: projects mouse delta onto the screen-space direction of the drag axis,
/// then converts the pixel delta to world units using the camera's screen-to-world ratio.
/// For rotate: computes the angular change of the mouse position around the projected
/// gizmo center on the rotation plane.
/// For scale: same as translate but applies as a multiplicative factor.
///
/// # Thread Safety
/// NOT thread-safe. All calls must be from the main thread.

#pragma once

#include "vector3.h"
#include "quaternion.h"

#include <cstdint>

namespace uz {

class Camera;

// ─────────────────────────────────────────────────────────────────────────────
// GizmoHitResult
// ─────────────────────────────────────────────────────────────────────────────

/// Identifies which gizmo element was hit by a mouse pick or is being dragged.
///
/// Values 1-3 correspond to single-axis handles (translate arrows, rotate circles,
/// scale cubes). Values 4-6 correspond to two-axis plane handles (translate only).
/// Value 7 is the center handle (uniform translate or uniform scale).
enum class GizmoHitResult : uint8_t {
    None   = 0, ///< No gizmo element hit.
    AxisX  = 1, ///< X axis handle (red).
    AxisY  = 2, ///< Y axis handle (green).
    AxisZ  = 3, ///< Z axis handle (blue).
    PlaneXY = 4, ///< XY plane handle (translate mode).
    PlaneXZ = 5, ///< XZ plane handle (translate mode).
    PlaneYZ = 6, ///< YZ plane handle (translate mode).
    Center  = 7, ///< Center/screen-space handle (uniform translate or scale).
};

// ─────────────────────────────────────────────────────────────────────────────
// GizmoInteraction
// ─────────────────────────────────────────────────────────────────────────────

/// Manages gizmo drag state and computes transform deltas from mouse movement.
///
/// # Ownership
/// Does NOT own any external objects. Camera and entity data are passed by
/// reference/value to each method call.
///
/// # Usage
/// ```cpp
/// GizmoInteraction gizmo;
///
/// // Every frame: update hover highlight
/// auto hovered = gizmo.HitTest(mx, my, vw, vh, cam, pos, scale, mode, space, rot);
/// gizmo.SetHoveredAxis(hovered);
///
/// // On mouse down: begin drag if hit
/// if (hovered != GizmoHitResult::None) {
///     gizmo.BeginDrag(hovered, mx, my, vw, vh, cam, entityPos, entityRot,
///                     entityScale, screenScale, mode, space);
/// }
///
/// // On mouse move while dragging: compute delta
/// Vec3 dp; Quat dr; Vec3 ds;
/// if (gizmo.UpdateDrag(mx, my, vw, vh, cam, snap, snapGrid, dp, dr, ds)) {
///     scene.SetPosition(scene.GetPosition() + dp);
///     scene.SetRotation(dr * scene.GetRotation());
///     scene.SetScale(scene.GetScale() + ds);
/// }
///
/// // On mouse up: end drag
/// gizmo.EndDrag();
/// ```
class GizmoInteraction {
public:
    /// Tests if a screen-space mouse position hits a gizmo handle.
    ///
    /// Projects all axis endpoints to screen space, then finds the closest
    /// axis/plane/center handle within the hit threshold (15 pixels).
    ///
    /// # Arguments
    /// * `mouseX`, `mouseY` — Mouse position in window-relative pixels.
    /// * `vpWidth`, `vpHeight` — Viewport dimensions in pixels.
    /// * `camera` — Camera for view-projection and position.
    /// * `gizmoPos` — World-space center of the gizmo.
    /// * `screenScale` — Same constant screen-space scale used by GizmoRenderer.
    /// * `gizmoMode` — Current gizmo mode (0=Translate, 1=Rotate, 2=Scale).
    /// * `gizmoSpace` — Coordinate space (0=World, 1=Local).
    /// * `entityRotation` — Entity rotation (used when space=Local).
    ///
    /// # Returns
    /// The closest hit element, or `GizmoHitResult::None` if nothing is within range.
    [[nodiscard]] GizmoHitResult HitTest(float mouseX, float mouseY,
                                          float vpWidth, float vpHeight,
                                          const Camera& camera,
                                          const Vec3& gizmoPos, float screenScale,
                                          uint8_t gizmoMode, uint8_t gizmoSpace,
                                          const Quat& entityRotation) const;

    /// Begins a drag operation on the given gizmo element.
    ///
    /// Captures the initial entity transform and mouse position so that
    /// subsequent UpdateDrag calls can compute incremental deltas.
    ///
    /// # Arguments
    /// * `axis` — Which gizmo element is being dragged.
    /// * `mouseX`, `mouseY` — Mouse position at drag start.
    /// * `vpWidth`, `vpHeight` — Viewport dimensions.
    /// * `camera` — Camera at drag start.
    /// * `entityPos` — Entity world position at drag start.
    /// * `entityRot` — Entity rotation at drag start.
    /// * `entityScale` — Entity scale at drag start.
    /// * `screenScale` — Gizmo screen scale at drag start.
    /// * `gizmoMode` — Current gizmo mode (0=Translate, 1=Rotate, 2=Scale).
    /// * `gizmoSpace` — Coordinate space (0=World, 1=Local).
    void BeginDrag(GizmoHitResult axis, float mouseX, float mouseY,
                   float vpWidth, float vpHeight,
                   const Camera& camera,
                   const Vec3& entityPos, const Quat& entityRot,
                   const Vec3& entityScale,
                   float screenScale, uint8_t gizmoMode, uint8_t gizmoSpace);

    /// Updates the drag with a new mouse position and outputs transform deltas.
    ///
    /// Computes the change in position/rotation/scale based on mouse movement
    /// since the last call (for translate/scale) or since drag start (for rotate).
    ///
    /// # Arguments
    /// * `mouseX`, `mouseY` — Current mouse position.
    /// * `vpWidth`, `vpHeight` — Current viewport dimensions.
    /// * `camera` — Current camera.
    /// * `snapEnabled` — Whether snapping is active (Ctrl held).
    /// * `snapGrid` — Snap increment (cm for translate, degrees for rotate, factor for scale).
    /// * `outDeltaPos` — [out] World-space position delta (translate mode).
    /// * `outDeltaRot` — [out] Rotation delta as quaternion (rotate mode).
    /// * `outDeltaScale` — [out] Scale delta (scale mode).
    ///
    /// # Returns
    /// `true` if the transform changed (caller should apply deltas), `false` if
    /// no drag is active or the delta is zero.
    [[nodiscard]] bool UpdateDrag(float mouseX, float mouseY,
                                   float vpWidth, float vpHeight,
                                   const Camera& camera,
                                   bool snapEnabled, float snapGrid,
                                   Vec3& outDeltaPos, Quat& outDeltaRot,
                                   Vec3& outDeltaScale);

    /// Ends the current drag operation and clears all drag state.
    void EndDrag();

    /// Returns the axis currently being dragged, or None if not dragging.
    ///
    /// Used by GizmoRenderer to highlight the active axis in white.
    [[nodiscard]] GizmoHitResult GetActiveAxis() const {
        return m_dragActive ? m_dragAxis : GizmoHitResult::None;
    }

    /// Returns the axis currently hovered by the mouse.
    ///
    /// Used by GizmoRenderer to highlight the hovered axis in yellow.
    [[nodiscard]] GizmoHitResult GetHoveredAxis() const { return m_hoveredAxis; }

    /// Returns true if a drag operation is in progress.
    [[nodiscard]] bool IsDragging() const { return m_dragActive; }

    /// Sets the hovered axis from the latest HitTest result.
    void SetHoveredAxis(GizmoHitResult axis) { m_hoveredAxis = axis; }

    /// Returns the entity position captured at drag start.
    ///
    /// Valid only while IsDragging() is true. Used by the caller to compute
    /// absolute position from a total delta: `newPos = GetDragStartPos() + deltaPos`.
    [[nodiscard]] const Vec3& GetDragStartPos() const { return m_dragStartPos; }

    /// Returns the entity rotation captured at drag start.
    ///
    /// Valid only while IsDragging() is true. Used by the caller to compute
    /// absolute rotation: `newRot = deltaRot * GetDragStartRot()`.
    [[nodiscard]] const Quat& GetDragStartRot() const { return m_dragStartRot; }

    /// Returns the entity scale captured at drag start.
    ///
    /// Valid only while IsDragging() is true. Used by the caller to compute
    /// absolute scale: `newScale = GetDragStartScale() + deltaScale`.
    [[nodiscard]] const Vec3& GetDragStartScale() const { return m_dragStartScale; }

private:
    // ── Drag state ──────────────────────────────────────────────────────────
    bool m_dragActive = false;
    GizmoHitResult m_dragAxis = GizmoHitResult::None;
    GizmoHitResult m_hoveredAxis = GizmoHitResult::None;
    uint8_t m_dragMode = 0;   ///< GizmoMode at drag start.
    uint8_t m_dragSpace = 0;  ///< GizmoSpace at drag start.

    // ── Drag start snapshot ─────────────────────────────────────────────────
    Vec3 m_dragStartPos{};          ///< Entity world position at drag start.
    Quat m_dragStartRot{};          ///< Entity rotation at drag start.
    Vec3 m_dragStartScale{};        ///< Entity scale at drag start.
    float m_dragStartMouseX = 0.f;  ///< Mouse X at drag start (pixels).
    float m_dragStartMouseY = 0.f;  ///< Mouse Y at drag start (pixels).
    float m_dragScreenScale = 0.f;  ///< Screen scale at drag start.
    float m_dragVpWidth = 0.f;      ///< Viewport width at drag start.
    float m_dragVpHeight = 0.f;     ///< Viewport height at drag start.

    // ── Axis directions in world space (computed at drag start) ─────────────
    Vec3 m_dragAxisDir{};           ///< Primary axis direction for single-axis drags.
    Vec3 m_dragPlaneNormal{};       ///< Plane normal for plane-handle drags.
    Vec3 m_dragPlaneAxis0{};        ///< First in-plane axis for plane-handle drags.
    Vec3 m_dragPlaneAxis1{};        ///< Second in-plane axis for plane-handle drags.

    // ── Accumulated state for rotate mode ───────────────────────────────────
    float m_dragAccumAngle = 0.f;   ///< Accumulated rotation angle (radians).
    float m_dragLastAngle = 0.f;    ///< Mouse angle at previous frame (for delta).
};

} // namespace uz
