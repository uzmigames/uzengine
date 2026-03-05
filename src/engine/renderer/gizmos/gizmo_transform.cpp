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

using namespace DirectX;

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

static constexpr float kPI2 = 6.28318530718f;

// ============================================================================
// Helper: build a cylinder along an arbitrary axis
// ============================================================================

static void addCylinder(std::vector<Vertex>& out,
                        XMFLOAT3 start, XMFLOAT3 end,
                        float radius, int segs, XMFLOAT3 color) {
    float dx = end.x - start.x, dy = end.y - start.y, dz = end.z - start.z;
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 0.0001f) return;

    XMVECTOR dir = XMVector3Normalize(XMVectorSet(dx, dy, dz, 0));
    XMVECTOR up = fabsf(dy/len) > 0.9f ?
        XMVectorSet(1,0,0,0) : XMVectorSet(0,1,0,0);
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, dir));
    XMVECTOR actualUp = XMVector3Cross(dir, right);

    XMFLOAT3 r3, u3;
    XMStoreFloat3(&r3, right);
    XMStoreFloat3(&u3, actualUp);

    std::vector<XMFLOAT3> ringStart(segs), ringEnd(segs);
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
                    XMFLOAT3 base, XMFLOAT3 tip,
                    float radius, int segs, XMFLOAT3 color) {
    float dx = tip.x - base.x, dy = tip.y - base.y, dz = tip.z - base.z;
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 0.0001f) return;

    XMVECTOR dir = XMVector3Normalize(XMVectorSet(dx, dy, dz, 0));
    XMVECTOR up = fabsf(dy/len) > 0.9f ?
        XMVectorSet(1,0,0,0) : XMVectorSet(0,1,0,0);
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, dir));
    XMVECTOR actualUp = XMVector3Cross(dir, right);

    XMFLOAT3 r3, u3;
    XMStoreFloat3(&r3, right);
    XMStoreFloat3(&u3, actualUp);

    std::vector<XMFLOAT3> ring(segs);
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
                         float halfSize, XMFLOAT3 color) {
    float s = halfSize;
    XMFLOAT3 corners[8] = {
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
    const XMFLOAT3 cols[3] = {{0.9f,0.15f,0.15f}, {0.15f,0.85f,0.15f}, {0.25f,0.45f,0.95f}};
    const XMFLOAT3 dirs[3] = {{1,0,0}, {0,1,0}, {0,0,1}};
    float shaftEnd = kAxisLen - kConeLen;

    for (int a = 0; a < 3; ++a) {
        axisStart[a] = static_cast<uint32_t>(out.size());
        XMFLOAT3 sEnd = {dirs[a].x * shaftEnd, dirs[a].y * shaftEnd, dirs[a].z * shaftEnd};
        XMFLOAT3 cEnd = {dirs[a].x * kAxisLen, dirs[a].y * kAxisLen, dirs[a].z * kAxisLen};
        addCylinder(out, {0,0,0}, sEnd, kShaftRad, kShaftSegs, cols[a]);
        addCone(out, sEnd, cEnd, kConeRad, kShaftSegs, cols[a]);
        axisCount[a] = static_cast<uint32_t>(out.size()) - axisStart[a];
    }
}

static void buildRotateVerts(std::vector<Vertex>& out, uint32_t axisStart[3], uint32_t axisCount[3]) {
    const XMFLOAT3 cols[3] = {{0.9f,0.15f,0.15f}, {0.15f,0.85f,0.15f}, {0.25f,0.45f,0.95f}};

    for (int a = 0; a < 3; ++a) {
        axisStart[a] = static_cast<uint32_t>(out.size());
        for (int i = 0; i < kCircleSegs; ++i) {
            float a0 = (float)i / kCircleSegs * kPI2;
            float a1 = (float)(i + 1) / kCircleSegs * kPI2;
            XMFLOAT3 c0, c1;
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
    const XMFLOAT3 cols[3] = {{0.9f,0.15f,0.15f}, {0.15f,0.85f,0.15f}, {0.25f,0.45f,0.95f}};
    const XMFLOAT3 dirs[3] = {{1,0,0}, {0,1,0}, {0,0,1}};

    for (int a = 0; a < 3; ++a) {
        axisStart[a] = static_cast<uint32_t>(out.size());
        XMFLOAT3 sEnd = {dirs[a].x * kAxisLen, dirs[a].y * kAxisLen, dirs[a].z * kAxisLen};
        addCylinder(out, {0,0,0}, sEnd, kShaftRad, kShaftSegs, cols[a]);
        addSolidCube(out, sEnd.x, sEnd.y, sEnd.z, kCubeSize, cols[a]);
        axisCount[a] = static_cast<uint32_t>(out.size()) - axisStart[a];
    }
}

// ============================================================================
// GizmoTransform implementation
// ============================================================================

void GizmoTransform::init(ID3D12Device* device) {
    {
        std::vector<Vertex> verts;
        buildTranslateVerts(verts, translateAxisStart_, translateAxisCount_);
        translateVertCount_ = static_cast<uint32_t>(verts.size());
        gizmo_upload_vb(device, verts.data(), translateVertCount_,
                        translateVB_, translateVBView_, "GizmoTransform_Translate");
    }
    {
        std::vector<Vertex> verts;
        buildRotateVerts(verts, rotateAxisStart_, rotateAxisCount_);
        rotateVertCount_ = static_cast<uint32_t>(verts.size());
        gizmo_upload_vb(device, verts.data(), rotateVertCount_,
                        rotateVB_, rotateVBView_, "GizmoTransform_Rotate");
    }
    {
        std::vector<Vertex> verts;
        buildScaleVerts(verts, scaleAxisStart_, scaleAxisCount_);
        scaleVertCount_ = static_cast<uint32_t>(verts.size());
        gizmo_upload_vb(device, verts.data(), scaleVertCount_,
                        scaleVB_, scaleVBView_, "GizmoTransform_Scale");
    }
}

void GizmoTransform::draw(ID3D12GraphicsCommandList* cmdList) {
    drawTranslate(cmdList);
}

void GizmoTransform::drawTranslate(ID3D12GraphicsCommandList* cmdList) {
    cmdList->IASetVertexBuffers(0, 1, &translateVBView_);
    cmdList->DrawInstanced(translateVertCount_, 1, 0, 0);
}

void GizmoTransform::drawRotate(ID3D12GraphicsCommandList* cmdList) {
    cmdList->IASetVertexBuffers(0, 1, &rotateVBView_);
    cmdList->DrawInstanced(rotateVertCount_, 1, 0, 0);
}

void GizmoTransform::drawScale(ID3D12GraphicsCommandList* cmdList) {
    cmdList->IASetVertexBuffers(0, 1, &scaleVBView_);
    cmdList->DrawInstanced(scaleVertCount_, 1, 0, 0);
}

void GizmoTransform::drawMode(ID3D12GraphicsCommandList* cmdList,
                               int mode, int activeAxis) {
    // Choose the right VB and per-axis ranges
    D3D12_VERTEX_BUFFER_VIEW* vbView;
    uint32_t* axisStarts;
    uint32_t* axisCounts;
    uint32_t totalVerts;

    switch (mode) {
        case 0:
            vbView = &translateVBView_;
            axisStarts = translateAxisStart_;
            axisCounts = translateAxisCount_;
            totalVerts = translateVertCount_;
            break;
        case 1:
            vbView = &rotateVBView_;
            axisStarts = rotateAxisStart_;
            axisCounts = rotateAxisCount_;
            totalVerts = rotateVertCount_;
            break;
        case 2:
            vbView = &scaleVBView_;
            axisStarts = scaleAxisStart_;
            axisCounts = scaleAxisCount_;
            totalVerts = scaleVertCount_;
            break;
        default:
            vbView = &translateVBView_;
            axisStarts = translateAxisStart_;
            axisCounts = translateAxisCount_;
            totalVerts = translateVertCount_;
            break;
    }

    cmdList->IASetVertexBuffers(0, 1, vbView);

    if (activeAxis < 0 || activeAxis > 2) {
        // No highlight — draw everything in one call
        cmdList->DrawInstanced(totalVerts, 1, 0, 0);
    } else {
        // Draw each axis separately: non-highlighted at normal brightness,
        // highlighted axis at boosted brightness via root constant color.
        for (int a = 0; a < 3; ++a) {
            if (axisCounts[a] == 0) continue;

            if (a == activeAxis) {
                // Highlight: set bright white color multiplier
                const float bright[4] = {1.8f, 1.8f, 1.8f, 0.0f};
                cmdList->SetGraphicsRoot32BitConstants(1, 4, bright, 0);
            } else {
                // Normal: vertex colors pass through (multiplied by 1.0)
                const float normal[4] = {1.0f, 1.0f, 1.0f, 0.0f};
                cmdList->SetGraphicsRoot32BitConstants(1, 4, normal, 0);
            }

            cmdList->DrawInstanced(axisCounts[a], 1, axisStarts[a], 0);
        }

        // Reset to normal
        const float reset[4] = {1.0f, 1.0f, 1.0f, 0.0f};
        cmdList->SetGraphicsRoot32BitConstants(1, 4, reset, 0);
    }
}

void GizmoTransform::shutdown() {
    translateVB_.Reset();
    rotateVB_.Reset();
    scaleVB_.Reset();
}

} // namespace uz
