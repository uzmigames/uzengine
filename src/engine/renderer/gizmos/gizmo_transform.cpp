// gizmo_transform.cpp — Interactive transform gizmo geometry (SOLID).
//
// Creates TRIANGLE_LIST vertex buffers for translate (cylinder+cone),
// rotate (torus arcs), and scale (cylinder+cube) gizmo handles.
// Each axis is drawn with its own bright color (R=X, G=Y, B=Z).
// Geometry is ordered per-axis (X, then Y, then Z) so drawMode can
// issue separate draw calls per axis for hover highlight feedback.

#include "gizmo_transform.h"
#include "engine/renderer/gizmos/gizmo_utils.h"

#include <cmath>
#include <vector>


namespace uz {

static constexpr float kAxisLen   = 2.5f;   // Length of each axis handle
static constexpr float kShaftRad  = 0.04f;  // Cylinder shaft radius
static constexpr float kConeRad   = 0.14f;  // Cone base radius
static constexpr float kConeLen   = 0.5f;   // Cone length
static constexpr int   kShaftSegs = 8;      // Segments around cylinder/cone
static constexpr int   kCircleSegs = 48;    // Segments per rotation circle
static constexpr float kCircleRad = 1.8f;   // Rotation circle radius
static constexpr float kTorusRad  = 0.05f;  // Torus tube radius
static constexpr int   kTorusSegs = 6;      // Segments around torus tube
static constexpr float kCubeSize  = 0.15f;  // Scale cube half-size
static constexpr float kFreeRingRad = 2.0f;  // Free rotation ring radius (slightly larger than axis rings)
static constexpr float kFreeRingTubeRad = 0.03f; // Free ring tube radius (thinner than axis rings)

static constexpr float kPI2 = 6.28318530718f;

// ============================================================================
// Helper: build a cylinder along an arbitrary axis
// ============================================================================

static void addCylinder(std::vector<Vertex>& out,
                        Float3 start, Float3 end,
                        float radius, int segs, Float3 color) {
    float dx = end.x - start.x, dy = end.y - start.y, dz = end.z - start.z;
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 0.0001f) return;

    Vec3 dirV = vec3_normalize({dx, dy, dz, 0.0f});
    Vec3 up = (fabsf(dy/len) > 0.9f)
        ? Vec3{1,0,0,0} : Vec3{0,1,0,0};
    Vec3 right = vec3_normalize(vec3_cross(up, dirV));
    Vec3 actualUp = vec3_cross(dirV, right);

    Float3 r3 = {right.x, right.y, right.z};
    Float3 u3 = {actualUp.x, actualUp.y, actualUp.z};

    std::vector<Float3> ringStart(segs), ringEnd(segs);
    for (int i = 0; i < segs; ++i) {
        float angle = (float)i / segs * kPI2;
        float c = cosf(angle) * radius, s = sinf(angle) * radius;
        ringStart[i] = {
            start.x + r3.x*c + u3.x*s,
            start.y + r3.y*c + u3.y*s,
            start.z + r3.z*c + u3.z*s
        };
        ringEnd[i] = {
            end.x + r3.x*c + u3.x*s,
            end.y + r3.y*c + u3.y*s,
            end.z + r3.z*c + u3.z*s
        };
    }

    for (int i = 0; i < segs; ++i) {
        int j = (i + 1) % segs;
        out.push_back({ringStart[i], color});
        out.push_back({ringEnd[i], color});
        out.push_back({ringStart[j], color});
        out.push_back({ringStart[j], color});
        out.push_back({ringEnd[i], color});
        out.push_back({ringEnd[j], color});
    }
}

// ============================================================================
// Helper: build a cone along an arbitrary axis
// ============================================================================

static void addCone(std::vector<Vertex>& out,
                    Float3 base, Float3 tip,
                    float radius, int segs, Float3 color) {
    float dx = tip.x - base.x, dy = tip.y - base.y, dz = tip.z - base.z;
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 0.0001f) return;

    Vec3 dirV = vec3_normalize({dx, dy, dz, 0.0f});
    Vec3 up = (fabsf(dy/len) > 0.9f)
        ? Vec3{1,0,0,0} : Vec3{0,1,0,0};
    Vec3 right = vec3_normalize(vec3_cross(up, dirV));
    Vec3 actualUp = vec3_cross(dirV, right);

    Float3 r3 = {right.x, right.y, right.z};
    Float3 u3 = {actualUp.x, actualUp.y, actualUp.z};

    std::vector<Float3> ring(segs);
    for (int i = 0; i < segs; ++i) {
        float angle = (float)i / segs * kPI2;
        float c = cosf(angle) * radius, s = sinf(angle) * radius;
        ring[i] = {
            base.x + r3.x*c + u3.x*s,
            base.y + r3.y*c + u3.y*s,
            base.z + r3.z*c + u3.z*s
        };
    }

    for (int i = 0; i < segs; ++i) {
        int j = (i + 1) % segs;
        out.push_back({ring[i], color});
        out.push_back({tip, color});
        out.push_back({ring[j], color});
    }

    for (int i = 1; i < segs - 1; ++i) {
        out.push_back({ring[0], color});
        out.push_back({ring[i+1], color});
        out.push_back({ring[i], color});
    }
}

// ============================================================================
// Helper: build a solid cube
// ============================================================================

static void addSolidCube(std::vector<Vertex>& out,
                         float cx, float cy, float cz,
                         float halfSize, Float3 color) {
    float s = halfSize;
    Float3 corners[8] = {
        {cx-s, cy-s, cz-s}, {cx+s, cy-s, cz-s},
        {cx+s, cy+s, cz-s}, {cx-s, cy+s, cz-s},
        {cx-s, cy-s, cz+s}, {cx+s, cy-s, cz+s},
        {cx+s, cy+s, cz+s}, {cx-s, cy+s, cz+s}
    };
    int faces[6][4] = {
        {0,3,2,1}, {4,5,6,7}, {0,1,5,4},
        {3,7,6,2}, {0,4,7,3}, {1,2,6,5}
    };
    for (auto& f : faces) {
        out.push_back({corners[f[0]], color});
        out.push_back({corners[f[1]], color});
        out.push_back({corners[f[2]], color});
        out.push_back({corners[f[0]], color});
        out.push_back({corners[f[2]], color});
        out.push_back({corners[f[3]], color});
    }
}

// ============================================================================
// Geometry builders — geometry is ordered per-axis (X→Y→Z)
// Returns per-axis vertex counts via axisCount[3]
// ============================================================================

static void buildTranslateVerts(std::vector<Vertex>& out, uint32_t axisStart[3], uint32_t axisCount[3]) {
    // Task 1.4: spec colors X=#CC2222, Y=#22CC22, Z=#2222CC
    const Float3 cols[3] = {{0.80f,0.133f,0.133f}, {0.133f,0.80f,0.133f}, {0.133f,0.133f,0.80f}};
    const Float3 dirs[3] = {{1,0,0}, {0,1,0}, {0,0,1}};
    float shaftEnd = kAxisLen - kConeLen;

    for (int a = 0; a < 3; ++a) {
        axisStart[a] = static_cast<uint32_t>(out.size());
        Float3 sEnd = {dirs[a].x * shaftEnd, dirs[a].y * shaftEnd, dirs[a].z * shaftEnd};
        Float3 cEnd = {dirs[a].x * kAxisLen, dirs[a].y * kAxisLen, dirs[a].z * kAxisLen};
        addCylinder(out, {0,0,0}, sEnd, kShaftRad, kShaftSegs, cols[a]);
        addCone(out, sEnd, cEnd, kConeRad, kShaftSegs, cols[a]);
        axisCount[a] = static_cast<uint32_t>(out.size()) - axisStart[a];
    }
}

static void buildRotateVerts(std::vector<Vertex>& out, uint32_t axisStart[3], uint32_t axisCount[3]) {
    // Task 1.4: spec colors X=#CC2222, Y=#22CC22, Z=#2222CC
    const Float3 cols[3] = {{0.80f,0.133f,0.133f}, {0.133f,0.80f,0.133f}, {0.133f,0.133f,0.80f}};

    for (int a = 0; a < 3; ++a) {
        axisStart[a] = static_cast<uint32_t>(out.size());
        for (int i = 0; i < kCircleSegs; ++i) {
            float a0 = (float)i / kCircleSegs * kPI2;
            float a1 = (float)(i + 1) / kCircleSegs * kPI2;
            Float3 c0, c1;
            if (a == 0) { // YZ plane
                c0 = {0, cosf(a0)*kCircleRad, sinf(a0)*kCircleRad};
                c1 = {0, cosf(a1)*kCircleRad, sinf(a1)*kCircleRad};
            } else if (a == 1) { // XZ plane
                c0 = {cosf(a0)*kCircleRad, 0, sinf(a0)*kCircleRad};
                c1 = {cosf(a1)*kCircleRad, 0, sinf(a1)*kCircleRad};
            } else { // XY plane
                c0 = {cosf(a0)*kCircleRad, sinf(a0)*kCircleRad, 0};
                c1 = {cosf(a1)*kCircleRad, sinf(a1)*kCircleRad, 0};
            }
            addCylinder(out, c0, c1, kTorusRad, kTorusSegs, cols[a]);
        }
        axisCount[a] = static_cast<uint32_t>(out.size()) - axisStart[a];
    }
}

static void buildScaleVerts(std::vector<Vertex>& out, uint32_t axisStart[3], uint32_t axisCount[3]) {
    // Task 1.4: spec colors X=#CC2222, Y=#22CC22, Z=#2222CC
    const Float3 cols[3] = {{0.80f,0.133f,0.133f}, {0.133f,0.80f,0.133f}, {0.133f,0.133f,0.80f}};
    const Float3 dirs[3] = {{1,0,0}, {0,1,0}, {0,0,1}};

    for (int a = 0; a < 3; ++a) {
        axisStart[a] = static_cast<uint32_t>(out.size());
        Float3 sEnd = {dirs[a].x * kAxisLen, dirs[a].y * kAxisLen, dirs[a].z * kAxisLen};
        addCylinder(out, {0,0,0}, sEnd, kShaftRad, kShaftSegs, cols[a]);
        addSolidCube(out, sEnd.x, sEnd.y, sEnd.z, kCubeSize, cols[a]);
        axisCount[a] = static_cast<uint32_t>(out.size()) - axisStart[a];
    }
}

// ============================================================================
// Helper: build a free rotation ring (circle in XY plane, billboarded at draw)
// ============================================================================

static void buildFreeRingVerts(std::vector<Vertex>& out) {
    const Float3 col = {0.80f, 0.80f, 0.80f}; // light grey (#CCCCCC)
    for (int i = 0; i < kCircleSegs; ++i) {
        float a0 = (float)i / kCircleSegs * kPI2;
        float a1 = (float)(i + 1) / kCircleSegs * kPI2;
        // Circle in XY plane (will be billboarded via MVP at draw time)
        Float3 c0 = {cosf(a0) * kFreeRingRad, sinf(a0) * kFreeRingRad, 0};
        Float3 c1 = {cosf(a1) * kFreeRingRad, sinf(a1) * kFreeRingRad, 0};
        addCylinder(out, c0, c1, kFreeRingTubeRad, kTorusSegs, col);
    }
}

// ============================================================================
// GizmoTransform implementation
// ============================================================================

bool GizmoTransform::init() {
    {
        std::vector<Vertex> verts;
        buildTranslateVerts(verts, translateAxisStart_, translateAxisCount_);
        translateVertCount_ = static_cast<uint32_t>(verts.size());
        translateVB_ = gizmo_upload_vb(verts.data(), translateVertCount_,
                        "GizmoTransform_Translate");
    }
    {
        std::vector<Vertex> verts;
        buildRotateVerts(verts, rotateAxisStart_, rotateAxisCount_);
        rotateVertCount_ = static_cast<uint32_t>(verts.size());
        rotateVB_ = gizmo_upload_vb(verts.data(), rotateVertCount_,
                        "GizmoTransform_Rotate");
    }
    {
        std::vector<Vertex> verts;
        buildScaleVerts(verts, scaleAxisStart_, scaleAxisCount_);
        scaleVertCount_ = static_cast<uint32_t>(verts.size());
        scaleVB_ = gizmo_upload_vb(verts.data(), scaleVertCount_,
                        "GizmoTransform_Scale");
    }
    {
        std::vector<Vertex> verts;
        buildFreeRingVerts(verts);
        freeRingVertCount_ = static_cast<uint32_t>(verts.size());
        freeRingVB_ = gizmo_upload_vb(verts.data(), freeRingVertCount_,
                        "GizmoTransform_FreeRing");
    }
    return translateVB_ != RHI_NULL && rotateVB_ != RHI_NULL
        && scaleVB_ != RHI_NULL && freeRingVB_ != RHI_NULL;
}

void GizmoTransform::draw(RhiCmdList cmd) {
    drawTranslate(cmd);
}

void GizmoTransform::drawTranslate(RhiCmdList cmd) {
    rhi()->cmd_set_vertex_buffer(cmd, 0, translateVB_, 0, sizeof(Vertex));
    rhi()->cmd_draw(cmd, translateVertCount_, 1, 0, 0);
}

void GizmoTransform::drawRotate(RhiCmdList cmd) {
    rhi()->cmd_set_vertex_buffer(cmd, 0, rotateVB_, 0, sizeof(Vertex));
    rhi()->cmd_draw(cmd, rotateVertCount_, 1, 0, 0);
}

void GizmoTransform::drawScale(RhiCmdList cmd) {
    rhi()->cmd_set_vertex_buffer(cmd, 0, scaleVB_, 0, sizeof(Vertex));
    rhi()->cmd_draw(cmd, scaleVertCount_, 1, 0, 0);
}

void GizmoTransform::drawMode(RhiCmdList cmd,
                               int mode, int activeAxis) {
    // Choose the right VB and per-axis ranges
    RhiBuffer vb;
    uint32_t* axisStarts;
    uint32_t* axisCounts;
    uint32_t totalVerts;

    switch (mode) {
        case 0:
            vb = translateVB_;
            axisStarts = translateAxisStart_;
            axisCounts = translateAxisCount_;
            totalVerts = translateVertCount_;
            break;
        case 1:
            vb = rotateVB_;
            axisStarts = rotateAxisStart_;
            axisCounts = rotateAxisCount_;
            totalVerts = rotateVertCount_;
            break;
        case 2:
            vb = scaleVB_;
            axisStarts = scaleAxisStart_;
            axisCounts = scaleAxisCount_;
            totalVerts = scaleVertCount_;
            break;
        default:
            vb = translateVB_;
            axisStarts = translateAxisStart_;
            axisCounts = translateAxisCount_;
            totalVerts = translateVertCount_;
            break;
    }

    rhi()->cmd_set_vertex_buffer(cmd, 0, vb, 0, sizeof(Vertex));

    if (activeAxis < 0 || activeAxis > 2) {
        // No highlight — draw everything in one call
        rhi()->cmd_draw(cmd, totalVerts, 1, 0, 0);
    } else {
        // Draw each axis separately: non-highlighted at normal brightness,
        // highlighted axis at boosted brightness via root constant color.
        for (int a = 0; a < 3; ++a) {
            if (axisCounts[a] == 0) continue;

            if (a == activeAxis) {
                // Highlight: set bright white color multiplier
                const float bright[4] = {1.8f, 1.8f, 1.8f, 0.0f};
                rhi()->cmd_set_root_constants(cmd, 1, bright, 4, 0);
            } else {
                // Normal: vertex colors pass through (multiplied by 1.0)
                const float normal[4] = {1.0f, 1.0f, 1.0f, 0.0f};
                rhi()->cmd_set_root_constants(cmd, 1, normal, 4, 0);
            }

            rhi()->cmd_draw(cmd, axisCounts[a], 1, axisStarts[a], 0);
        }

        // Reset to normal
        const float reset[4] = {1.0f, 1.0f, 1.0f, 0.0f};
        rhi()->cmd_set_root_constants(cmd, 1, reset, 4, 0);
    }
}

void GizmoTransform::drawFreeRing(RhiCmdList cmd, bool highlighted) {
    if (freeRingVB_ == RHI_NULL || freeRingVertCount_ == 0) return;

    if (highlighted) {
        const float bright[4] = {1.8f, 1.8f, 1.8f, 0.0f};
        rhi()->cmd_set_root_constants(cmd, 1, bright, 4, 0);
    } else {
        const float normal[4] = {1.0f, 1.0f, 1.0f, 0.0f};
        rhi()->cmd_set_root_constants(cmd, 1, normal, 4, 0);
    }

    rhi()->cmd_set_vertex_buffer(cmd, 0, freeRingVB_, 0, sizeof(Vertex));
    rhi()->cmd_draw(cmd, freeRingVertCount_, 1, 0, 0);

    // Reset color multiplier
    const float reset[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    rhi()->cmd_set_root_constants(cmd, 1, reset, 4, 0);
}

void GizmoTransform::shutdown() {
    if (translateVB_ != RHI_NULL) { rhi()->buffer_destroy(translateVB_); translateVB_ = RHI_NULL; }
    if (rotateVB_ != RHI_NULL) { rhi()->buffer_destroy(rotateVB_); rotateVB_ = RHI_NULL; }
    if (scaleVB_ != RHI_NULL) { rhi()->buffer_destroy(scaleVB_); scaleVB_ = RHI_NULL; }
    if (freeRingVB_ != RHI_NULL) { rhi()->buffer_destroy(freeRingVB_); freeRingVB_ = RHI_NULL; }
}

} // namespace uz
