/// GizmoInteraction — Implementation of hit testing and drag interaction.

#include "gizmo_interaction.h"

#include "camera.h"
#include "editor_input_handler.h"
#include "log.h"
#include "vector2.h"
#include "vector4.h"
#include "mat4.h"

#include <cmath>

namespace uz {

UZ_DECLARE_LOG_CATEGORY(GizmoInteraction);
UZ_DEFINE_LOG_CATEGORY(GizmoInteraction, Trace);

// ─────────────────────────────────────────────────────────────────────────────
// Constants — must match gizmo_renderer.cpp geometry
// ─────────────────────────────────────────────────────────────────────────────

/// Length of axis stems in unit gizmo space (same as gizmo_renderer.cpp kAxisLength).
static constexpr float kAxisLength = 0.8f;

/// Position along each axis pair for the plane translate handles.
static constexpr float kPlaneHandleOffset = 0.3f;

/// Size of the plane translate handle rectangles.
static constexpr float kPlaneHandleSize = 0.15f;

/// Circle radius for rotate gizmo (same as gizmo_renderer.cpp kCircleRadius).
static constexpr float kCircleRadius = 0.85f;

/// Center cube half-size (same as gizmo_renderer.cpp kCenterCubeHalfSize).
static constexpr float kCenterCubeHalfSize = 0.06f;

/// Hit testing threshold in screen pixels. Handles within this distance
/// from the mouse cursor are considered "hit".
static constexpr float kHitThresholdPx = 15.0f;

/// Hit threshold for center cube (slightly larger since it is small).
static constexpr float kCenterHitThresholdPx = 20.0f;

/// Minimum mouse delta in pixels before a drag produces a transform change.
/// Prevents jitter from sub-pixel mouse movement.
static constexpr float kMinDragDeltaPx = 0.5f;

static constexpr float kPi = 3.14159265358979323846f;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Projects a world-space point to screen-space pixel coordinates.
///
/// Returns Vec3(screenX, screenY, ndcZ). If the point is behind the camera
/// (clip.w <= 0), returns an off-screen sentinel so it won't interfere with
/// hit testing.
static Vec3 ProjectToScreen(const Vec3& worldPos, const Camera& camera,
                             float vpWidth, float vpHeight) {
    const Mat4 vp = camera.GetViewProjection();
    const Vec4 clip = vp * Vec4(worldPos, 1.0f);

    // Point is behind the camera — return off-screen sentinel.
    if (clip.w <= 0.001f) {
        return {-9999.f, -9999.f, 0.f};
    }

    const float invW = 1.0f / clip.w;
    const float ndcX = clip.x * invW;
    const float ndcY = clip.y * invW;
    const float ndcZ = clip.z * invW;

    // NDC [-1,+1] → screen pixels. Y is flipped (screen Y=0 is top).
    const float screenX = (ndcX * 0.5f + 0.5f) * vpWidth;
    const float screenY = (1.0f - (ndcY * 0.5f + 0.5f)) * vpHeight;

    return {screenX, screenY, ndcZ};
}

/// Computes the shortest distance from point (px, py) to the line segment (ax,ay)-(bx,by).
///
/// Standard 2D point-to-segment distance using projection clamping.
static float PointToSegmentDistance2D(float px, float py,
                                      float ax, float ay,
                                      float bx, float by) {
    const float dx = bx - ax;
    const float dy = by - ay;
    const float lenSq = dx * dx + dy * dy;

    // Degenerate segment (zero length) — return point-to-point distance.
    if (lenSq < 1e-8f) {
        const float ex = px - ax;
        const float ey = py - ay;
        return std::sqrt(ex * ex + ey * ey);
    }

    // Project mouse point onto the segment, clamped to [0, 1].
    float t = ((px - ax) * dx + (py - ay) * dy) / lenSq;
    t = std::max(0.f, std::min(1.f, t));

    const float closestX = ax + t * dx;
    const float closestY = ay + t * dy;
    const float ex = px - closestX;
    const float ey = py - closestY;
    return std::sqrt(ex * ex + ey * ey);
}

/// Returns true if the point (px, py) is inside the 2D quad defined by four
/// screen-space corners using the cross-product winding test.
static bool PointInQuad2D(float px, float py,
                           float x0, float y0, float x1, float y1,
                           float x2, float y2, float x3, float y3) {
    // Test using two triangles: (0,1,2) and (0,2,3).
    auto sign = [](float ax, float ay, float bx, float by, float cx, float cy) {
        return (ax - cx) * (by - cy) - (bx - cx) * (ay - cy);
    };

    // Triangle 1: corners 0, 1, 2
    {
        const float d1 = sign(px, py, x0, y0, x1, y1);
        const float d2 = sign(px, py, x1, y1, x2, y2);
        const float d3 = sign(px, py, x2, y2, x0, y0);
        const bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
        const bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
        if (!(hasNeg && hasPos)) return true;
    }

    // Triangle 2: corners 0, 2, 3
    {
        const float d1 = sign(px, py, x0, y0, x2, y2);
        const float d2 = sign(px, py, x2, y2, x3, y3);
        const float d3 = sign(px, py, x3, y3, x0, y0);
        const bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
        const bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
        if (!(hasNeg && hasPos)) return true;
    }

    return false;
}

/// Computes the world-space axis directions for a given coordinate space.
/// When space=World, returns the standard basis. When space=Local, rotates
/// the basis by the entity rotation.
static void ComputeAxisDirs(uint8_t gizmoSpace, const Quat& entityRotation,
                             Vec3 outDirs[3]) {
    outDirs[0] = Vec3::UnitX(); // X
    outDirs[1] = Vec3::UnitY(); // Y
    outDirs[2] = Vec3::UnitZ(); // Z

    if (gizmoSpace == static_cast<uint8_t>(GizmoSpace::Local)) {
        outDirs[0] = entityRotation * outDirs[0];
        outDirs[1] = entityRotation * outDirs[1];
        outDirs[2] = entityRotation * outDirs[2];
    }
}

/// Computes the constant-screen-space scale factor used by the gizmo renderer.
/// Must match the formula in gizmo_renderer.cpp Draw().
static float ComputeScreenScale(const Vec3& gizmoPos, const Camera& camera) {
    const Vec3 camPos = camera.GetPosition();
    const float dx = gizmoPos.x - camPos.x;
    const float dy = gizmoPos.y - camPos.y;
    const float dz = gizmoPos.z - camPos.z;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist < 0.001f) return 0.f;

    const float tanHalfFov = std::tan(camera.GetFovY() * 0.5f);
    // Same formula as gizmo_renderer.cpp: kTargetScreenPx=120, reference height=720.
    return dist * tanHalfFov * (120.0f / 720.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// HitTest
// ─────────────────────────────────────────────────────────────────────────────

GizmoHitResult GizmoInteraction::HitTest(float mouseX, float mouseY,
                                          float vpWidth, float vpHeight,
                                          const Camera& camera,
                                          const Vec3& gizmoPos, float screenScale,
                                          uint8_t gizmoMode, uint8_t gizmoSpace,
                                          const Quat& entityRotation) const {
    if (screenScale < 0.001f) return GizmoHitResult::None;

    // Compute axis directions based on coordinate space.
    Vec3 dirs[3];
    ComputeAxisDirs(gizmoSpace, entityRotation, dirs);

    // Project gizmo center to screen.
    const Vec3 screenCenter = ProjectToScreen(gizmoPos, camera, vpWidth, vpHeight);
    if (screenCenter.x < -5000.f) return GizmoHitResult::None; // Behind camera.

    float bestDist = kHitThresholdPx;
    GizmoHitResult bestHit = GizmoHitResult::None;

    // ── Translate or Scale mode: test single-axis stems ─────────────────
    if (gizmoMode == static_cast<uint8_t>(GizmoMode::Translate) ||
        gizmoMode == static_cast<uint8_t>(GizmoMode::Scale)) {

        for (int i = 0; i < 3; ++i) {
            const Vec3 tip = gizmoPos + dirs[i] * (screenScale * kAxisLength);
            const Vec3 screenTip = ProjectToScreen(tip, camera, vpWidth, vpHeight);

            const float dist = PointToSegmentDistance2D(
                mouseX, mouseY,
                screenCenter.x, screenCenter.y,
                screenTip.x, screenTip.y);

            if (dist < bestDist) {
                bestDist = dist;
                bestHit = static_cast<GizmoHitResult>(i + 1); // AxisX=1, AxisY=2, AxisZ=3
            }
        }
    }

    // ── Translate mode only: test plane handles ─────────────────────────
    if (gizmoMode == static_cast<uint8_t>(GizmoMode::Translate)) {
        // Plane handles: small quads at offset along two axes.
        // PlaneXY = dirs[0]+dirs[1], PlaneXZ = dirs[0]+dirs[2], PlaneYZ = dirs[1]+dirs[2].
        const int planeAxes[3][2] = {{0, 1}, {0, 2}, {1, 2}};

        for (int p = 0; p < 3; ++p) {
            const int a0 = planeAxes[p][0];
            const int a1 = planeAxes[p][1];

            // Four corners of the plane handle quad in world space.
            const Vec3 base = gizmoPos;
            const Vec3 c0 = base + dirs[a0] * (screenScale * kPlaneHandleOffset)
                                 + dirs[a1] * (screenScale * kPlaneHandleOffset);
            const Vec3 c1 = base + dirs[a0] * (screenScale * (kPlaneHandleOffset + kPlaneHandleSize))
                                 + dirs[a1] * (screenScale * kPlaneHandleOffset);
            const Vec3 c2 = base + dirs[a0] * (screenScale * (kPlaneHandleOffset + kPlaneHandleSize))
                                 + dirs[a1] * (screenScale * (kPlaneHandleOffset + kPlaneHandleSize));
            const Vec3 c3 = base + dirs[a0] * (screenScale * kPlaneHandleOffset)
                                 + dirs[a1] * (screenScale * (kPlaneHandleOffset + kPlaneHandleSize));

            // Project all corners to screen.
            const Vec3 s0 = ProjectToScreen(c0, camera, vpWidth, vpHeight);
            const Vec3 s1 = ProjectToScreen(c1, camera, vpWidth, vpHeight);
            const Vec3 s2 = ProjectToScreen(c2, camera, vpWidth, vpHeight);
            const Vec3 s3 = ProjectToScreen(c3, camera, vpWidth, vpHeight);

            if (PointInQuad2D(mouseX, mouseY,
                              s0.x, s0.y, s1.x, s1.y,
                              s2.x, s2.y, s3.x, s3.y)) {
                // Plane handles take priority over axis stems if the mouse is inside.
                bestHit = static_cast<GizmoHitResult>(4 + p); // PlaneXY=4, PlaneXZ=5, PlaneYZ=6
                bestDist = 0.f; // Force priority.
            }
        }
    }

    // ── Rotate mode: test axis circles ──────────────────────────────────
    if (gizmoMode == static_cast<uint8_t>(GizmoMode::Rotate)) {
        // Each rotate circle is a ring of segments. We test each segment
        // as a screen-space line segment.
        static constexpr int kTestSegments = 32; // Fewer segments than render for speed.

        for (int axis = 0; axis < 3; ++axis) {
            // The circle lies in the plane perpendicular to dirs[axis].
            // Compute two in-plane basis vectors.
            const Vec3& normal = dirs[axis];
            const Vec3 tangent0 = dirs[(axis + 1) % 3];
            const Vec3 tangent1 = dirs[(axis + 2) % 3];

            float minSegDist = kHitThresholdPx;

            for (int seg = 0; seg < kTestSegments; ++seg) {
                const float angle0 = (static_cast<float>(seg) / kTestSegments) * 2.f * kPi;
                const float angle1 = (static_cast<float>(seg + 1) / kTestSegments) * 2.f * kPi;

                const Vec3 p0 = gizmoPos + (tangent0 * std::cos(angle0) + tangent1 * std::sin(angle0))
                                            * (screenScale * kCircleRadius);
                const Vec3 p1 = gizmoPos + (tangent0 * std::cos(angle1) + tangent1 * std::sin(angle1))
                                            * (screenScale * kCircleRadius);

                const Vec3 sp0 = ProjectToScreen(p0, camera, vpWidth, vpHeight);
                const Vec3 sp1 = ProjectToScreen(p1, camera, vpWidth, vpHeight);

                const float dist = PointToSegmentDistance2D(
                    mouseX, mouseY, sp0.x, sp0.y, sp1.x, sp1.y);

                if (dist < minSegDist) {
                    minSegDist = dist;
                }
            }

            if (minSegDist < bestDist) {
                bestDist = minSegDist;
                bestHit = static_cast<GizmoHitResult>(axis + 1);
            }
        }
    }

    // ── Center handle (translate and scale modes) ───────────────────────
    if (gizmoMode == static_cast<uint8_t>(GizmoMode::Translate) ||
        gizmoMode == static_cast<uint8_t>(GizmoMode::Scale)) {

        // Test distance to the projected center point.
        const float dx = mouseX - screenCenter.x;
        const float dy = mouseY - screenCenter.y;
        const float centerDist = std::sqrt(dx * dx + dy * dy);

        // Project a corner of the center cube to determine its screen-space radius.
        const Vec3 cubeCorner = gizmoPos + dirs[0] * (screenScale * kCenterCubeHalfSize)
                                         + dirs[1] * (screenScale * kCenterCubeHalfSize)
                                         + dirs[2] * (screenScale * kCenterCubeHalfSize);
        const Vec3 screenCorner = ProjectToScreen(cubeCorner, camera, vpWidth, vpHeight);
        const float cubeScreenRadius = std::sqrt(
            (screenCorner.x - screenCenter.x) * (screenCorner.x - screenCenter.x) +
            (screenCorner.y - screenCenter.y) * (screenCorner.y - screenCenter.y));

        // Use the larger of the cube's screen radius or a minimum threshold.
        const float centerThreshold = std::max(cubeScreenRadius + 5.f, kCenterHitThresholdPx);

        if (centerDist < centerThreshold && centerDist < bestDist) {
            bestDist = centerDist;
            bestHit = GizmoHitResult::Center;
        }
    }

    return bestHit;
}

// ─────────────────────────────────────────────────────────────────────────────
// BeginDrag
// ─────────────────────────────────────────────────────────────────────────────

void GizmoInteraction::BeginDrag(GizmoHitResult axis,
                                  float mouseX, float mouseY,
                                  float vpWidth, float vpHeight,
                                  const Camera& camera,
                                  const Vec3& entityPos, const Quat& entityRot,
                                  const Vec3& entityScale,
                                  float screenScale,
                                  uint8_t gizmoMode, uint8_t gizmoSpace) {
    m_dragActive = true;
    m_dragAxis = axis;
    m_dragMode = gizmoMode;
    m_dragSpace = gizmoSpace;

    m_dragStartPos = entityPos;
    m_dragStartRot = entityRot;
    m_dragStartScale = entityScale;
    m_dragStartMouseX = mouseX;
    m_dragStartMouseY = mouseY;
    m_dragScreenScale = screenScale;
    m_dragVpWidth = vpWidth;
    m_dragVpHeight = vpHeight;

    // Compute axis directions in the appropriate coordinate space.
    Vec3 dirs[3];
    ComputeAxisDirs(gizmoSpace, entityRot, dirs);

    // Determine the drag axis direction and plane normal based on the hit element.
    switch (axis) {
    case GizmoHitResult::AxisX:
        m_dragAxisDir = dirs[0];
        m_dragPlaneNormal = dirs[0]; // Normal for rotate; unused for translate single-axis.
        break;
    case GizmoHitResult::AxisY:
        m_dragAxisDir = dirs[1];
        m_dragPlaneNormal = dirs[1];
        break;
    case GizmoHitResult::AxisZ:
        m_dragAxisDir = dirs[2];
        m_dragPlaneNormal = dirs[2];
        break;
    case GizmoHitResult::PlaneXY:
        m_dragPlaneNormal = dirs[2]; // XY plane → Z normal.
        m_dragPlaneAxis0 = dirs[0];
        m_dragPlaneAxis1 = dirs[1];
        m_dragAxisDir = Vec3::Zero();
        break;
    case GizmoHitResult::PlaneXZ:
        m_dragPlaneNormal = dirs[1]; // XZ plane → Y normal.
        m_dragPlaneAxis0 = dirs[0];
        m_dragPlaneAxis1 = dirs[2];
        m_dragAxisDir = Vec3::Zero();
        break;
    case GizmoHitResult::PlaneYZ:
        m_dragPlaneNormal = dirs[0]; // YZ plane → X normal.
        m_dragPlaneAxis0 = dirs[1];
        m_dragPlaneAxis1 = dirs[2];
        m_dragAxisDir = Vec3::Zero();
        break;
    case GizmoHitResult::Center:
        // Center: use the camera-facing plane for screen-space drag.
        m_dragPlaneNormal = (camera.GetPosition() - entityPos).Normalized();
        m_dragAxisDir = Vec3::Zero();
        break;
    case GizmoHitResult::None:
        m_dragActive = false;
        return;
    }

    // For rotate mode: compute the initial angle of the mouse on the rotation plane.
    if (gizmoMode == static_cast<uint8_t>(GizmoMode::Rotate)) {
        const Vec3 screenCenter = ProjectToScreen(entityPos, camera, vpWidth, vpHeight);
        const float dx = mouseX - screenCenter.x;
        const float dy = mouseY - screenCenter.y;
        m_dragLastAngle = std::atan2(dy, dx);
        m_dragAccumAngle = 0.f;
    }

    UZ_LOG(GizmoInteraction, Trace, "BeginDrag: axis={} mode={} space={}",
           static_cast<int>(axis), gizmoMode, gizmoSpace);
}

// ─────────────────────────────────────────────────────────────────────────────
// UpdateDrag
// ─────────────────────────────────────────────────────────────────────────────

bool GizmoInteraction::UpdateDrag(float mouseX, float mouseY,
                                   float vpWidth, float vpHeight,
                                   const Camera& camera,
                                   bool snapEnabled, float snapGrid,
                                   Vec3& outDeltaPos, Quat& outDeltaRot,
                                   Vec3& outDeltaScale) {
    if (!m_dragActive) return false;

    outDeltaPos = Vec3::Zero();
    outDeltaRot = Quat::Identity();
    outDeltaScale = Vec3::Zero();

    // ── Translate mode ──────────────────────────────────────────────────
    if (m_dragMode == static_cast<uint8_t>(GizmoMode::Translate)) {
        if (m_dragAxis == GizmoHitResult::Center) {
            // Screen-space translate: move on camera-facing plane.
            // Project the entity's start position to screen, compute the screen delta,
            // then unproject back to world space.
            const Vec3 screenStart = ProjectToScreen(m_dragStartPos, camera, vpWidth, vpHeight);
            const float sdx = mouseX - m_dragStartMouseX;
            const float sdy = mouseY - m_dragStartMouseY;

            if (std::abs(sdx) < kMinDragDeltaPx && std::abs(sdy) < kMinDragDeltaPx) {
                return false;
            }

            // Compute world-space movement by projecting screen delta onto camera right/up.
            // The camera's right and up vectors, scaled by the world-to-screen ratio at the
            // gizmo's distance, give us the conversion factor.
            const Vec3 camRight = camera.GetRight();
            const Vec3 camUp = camera.GetUp();

            // World units per pixel at the gizmo's distance.
            const float dist = (m_dragStartPos - camera.GetPosition()).Length();
            const float tanHalfFov = std::tan(camera.GetFovY() * 0.5f);
            const float worldPerPx = (2.f * dist * tanHalfFov) / vpHeight;

            Vec3 worldDelta = camRight * (sdx * worldPerPx) - camUp * (sdy * worldPerPx);

            if (snapEnabled && snapGrid > 0.f) {
                worldDelta = worldDelta.snap(Vec3(snapGrid));
            }

            outDeltaPos = worldDelta;
            return true;
        }

        if (m_dragAxis >= GizmoHitResult::PlaneXY && m_dragAxis <= GizmoHitResult::PlaneYZ) {
            // Plane-constrained translate: project mouse delta onto two in-plane axes.
            const float sdx = mouseX - m_dragStartMouseX;
            const float sdy = mouseY - m_dragStartMouseY;

            if (std::abs(sdx) < kMinDragDeltaPx && std::abs(sdy) < kMinDragDeltaPx) {
                return false;
            }

            // Project each in-plane axis to screen direction.
            const Vec3 screenOrigin = ProjectToScreen(m_dragStartPos, camera, vpWidth, vpHeight);
            const Vec3 screenTip0 = ProjectToScreen(m_dragStartPos + m_dragPlaneAxis0, camera, vpWidth, vpHeight);
            const Vec3 screenTip1 = ProjectToScreen(m_dragStartPos + m_dragPlaneAxis1, camera, vpWidth, vpHeight);

            Vec2 screenDir0 = Vec2(screenTip0.x - screenOrigin.x, screenTip0.y - screenOrigin.y);
            Vec2 screenDir1 = Vec2(screenTip1.x - screenOrigin.x, screenTip1.y - screenOrigin.y);

            const float len0 = screenDir0.Length();
            const float len1 = screenDir1.Length();
            if (len0 < 0.001f || len1 < 0.001f) return false;
            screenDir0 = screenDir0 * (1.f / len0);
            screenDir1 = screenDir1 * (1.f / len1);

            // Project screen mouse delta onto each axis screen direction.
            const Vec2 mouseDelta(sdx, sdy);
            const float proj0 = mouseDelta.Dot(screenDir0);
            const float proj1 = mouseDelta.Dot(screenDir1);

            // Convert pixel projection to world units.
            // The screen length of 1 world unit along each axis gives the conversion.
            const float worldPerPx0 = 1.f / len0;
            const float worldPerPx1 = 1.f / len1;

            Vec3 worldDelta = m_dragPlaneAxis0 * (proj0 * worldPerPx0)
                            + m_dragPlaneAxis1 * (proj1 * worldPerPx1);

            if (snapEnabled && snapGrid > 0.f) {
                worldDelta = worldDelta.snap(Vec3(snapGrid));
            }

            outDeltaPos = worldDelta;
            return true;
        }

        // Single-axis translate: project mouse delta onto the screen-space axis direction.
        {
            const float sdx = mouseX - m_dragStartMouseX;
            const float sdy = mouseY - m_dragStartMouseY;

            if (std::abs(sdx) < kMinDragDeltaPx && std::abs(sdy) < kMinDragDeltaPx) {
                return false;
            }

            // Project axis to screen to get the screen-space direction.
            const Vec3 screenOrigin = ProjectToScreen(m_dragStartPos, camera, vpWidth, vpHeight);
            const Vec3 screenTip = ProjectToScreen(m_dragStartPos + m_dragAxisDir, camera, vpWidth, vpHeight);

            Vec2 screenAxisDir = Vec2(screenTip.x - screenOrigin.x, screenTip.y - screenOrigin.y);
            const float screenAxisLen = screenAxisDir.Length();
            if (screenAxisLen < 0.001f) return false;
            screenAxisDir = screenAxisDir * (1.f / screenAxisLen);

            // Project mouse delta onto the screen-space axis direction.
            const Vec2 mouseDelta(sdx, sdy);
            const float projectedPx = mouseDelta.Dot(screenAxisDir);

            // Convert screen pixels to world units. The projection of 1 world unit
            // of the axis direction yields `screenAxisLen` pixels, so:
            //   worldDelta = projectedPx * (1 world unit / screenAxisLen pixels)
            float worldDelta = projectedPx / screenAxisLen;

            if (snapEnabled && snapGrid > 0.f) {
                worldDelta = std::round(worldDelta / snapGrid) * snapGrid;
            }

            outDeltaPos = m_dragAxisDir * worldDelta;
            return true;
        }
    }

    // ── Rotate mode ─────────────────────────────────────────────────────
    if (m_dragMode == static_cast<uint8_t>(GizmoMode::Rotate)) {
        // Compute the angle of the mouse relative to the gizmo center on screen.
        const Vec3 screenCenter = ProjectToScreen(m_dragStartPos, camera, vpWidth, vpHeight);
        const float dx = mouseX - screenCenter.x;
        const float dy = mouseY - screenCenter.y;

        // Require minimum distance from center to avoid singularity.
        if (std::sqrt(dx * dx + dy * dy) < 5.f) return false;

        const float currentAngle = std::atan2(dy, dx);

        // Compute angle delta since last frame, handling wraparound.
        float angleDelta = currentAngle - m_dragLastAngle;
        if (angleDelta > kPi)  angleDelta -= 2.f * kPi;
        if (angleDelta < -kPi) angleDelta += 2.f * kPi;

        m_dragAccumAngle += angleDelta;
        m_dragLastAngle = currentAngle;

        float rotAngle = m_dragAccumAngle;

        if (snapEnabled && snapGrid > 0.f) {
            // snapGrid is in degrees for rotate mode.
            const float snapRad = snapGrid * (kPi / 180.f);
            rotAngle = std::round(rotAngle / snapRad) * snapRad;
        }

        if (std::abs(rotAngle) < 1e-6f) return false;

        // Output TOTAL rotation from drag start. The caller applies:
        //   entity.rot = outDeltaRot * dragStartRot
        // The negation makes clockwise mouse motion produce clockwise rotation
        // when viewed along the axis (matching editor convention).
        outDeltaRot = Quat::FromAxisAngle(m_dragPlaneNormal, -rotAngle);
        return true;
    }

    // ── Scale mode ──────────────────────────────────────────────────────
    if (m_dragMode == static_cast<uint8_t>(GizmoMode::Scale)) {
        const float sdx = mouseX - m_dragStartMouseX;
        const float sdy = mouseY - m_dragStartMouseY;

        if (std::abs(sdx) < kMinDragDeltaPx && std::abs(sdy) < kMinDragDeltaPx) {
            return false;
        }

        if (m_dragAxis == GizmoHitResult::Center) {
            // Uniform scale: use vertical mouse movement as the scale factor.
            // Moving up = scale up, moving down = scale down.
            // Scale sensitivity: 1 pixel = 0.005 scale units.
            float scaleFactor = -sdy * 0.005f;

            if (snapEnabled && snapGrid > 0.f) {
                scaleFactor = std::round(scaleFactor / snapGrid) * snapGrid;
            }

            outDeltaScale = Vec3(scaleFactor);
            return std::abs(scaleFactor) > 1e-6f;
        }

        // Single-axis scale: project mouse delta onto screen-space axis direction.
        const Vec3 screenOrigin = ProjectToScreen(m_dragStartPos, camera, vpWidth, vpHeight);
        const Vec3 screenTip = ProjectToScreen(m_dragStartPos + m_dragAxisDir, camera, vpWidth, vpHeight);

        Vec2 screenAxisDir = Vec2(screenTip.x - screenOrigin.x, screenTip.y - screenOrigin.y);
        const float screenAxisLen = screenAxisDir.Length();
        if (screenAxisLen < 0.001f) return false;
        screenAxisDir = screenAxisDir * (1.f / screenAxisLen);

        const Vec2 mouseDelta(sdx, sdy);
        const float projectedPx = mouseDelta.Dot(screenAxisDir);

        // Convert projected pixels to a scale delta.
        // Sensitivity: 1 pixel = 0.005 scale units, same as uniform scale.
        float scaleDelta = projectedPx * 0.005f;

        if (snapEnabled && snapGrid > 0.f) {
            scaleDelta = std::round(scaleDelta / snapGrid) * snapGrid;
        }

        if (std::abs(scaleDelta) < 1e-6f) return false;

        // Apply scale delta only along the dragged axis.
        const int axisIdx = static_cast<int>(m_dragAxis) - 1; // AxisX=1 → 0, etc.
        if (axisIdx < 0 || axisIdx > 2) return false;

        outDeltaScale = Vec3::Zero();
        outDeltaScale[axisIdx] = scaleDelta;
        return true;
    }

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// EndDrag
// ─────────────────────────────────────────────────────────────────────────────

void GizmoInteraction::EndDrag() {
    if (m_dragActive) {
        UZ_LOG(GizmoInteraction, Trace, "EndDrag: axis={}",
               static_cast<int>(m_dragAxis));
    }
    m_dragActive = false;
    m_dragAxis = GizmoHitResult::None;
    m_dragMode = 0;
    m_dragSpace = 0;
    m_dragAccumAngle = 0.f;
    m_dragLastAngle = 0.f;
}

} // namespace uz
