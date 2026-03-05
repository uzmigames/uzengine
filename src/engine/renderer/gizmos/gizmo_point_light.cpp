// gizmo_point_light.cpp — Point light gizmo.
//
// Three orthogonal wireframe circles (XY, XZ, YZ) centered at origin.

#include "gizmo_point_light.h"
#include "gizmo_utils.h"
#include "engine/renderer/renderer.h"

#include <cmath>

using namespace DirectX;

namespace uz {

void GizmoPointLight::init(ID3D12Device* device) {
    const XMFLOAT3 white = {1.0f, 1.0f, 1.0f};

    constexpr int   kSegs   = 24;
    constexpr float kRadius = 0.5f;
    constexpr int   kTotalVerts = 3 * kSegs * 2;

    Vertex verts[kTotalVerts];
    uint32_t idx = 0;

    constexpr float kTwoPi = 6.2831853f;
    const float step = kTwoPi / static_cast<float>(kSegs);

    // XY plane circle.
    for (int i = 0; i < kSegs; ++i) {
        const float a0 = step * static_cast<float>(i);
        const float a1 = step * static_cast<float>(i + 1);
        verts[idx++] = {{ kRadius * std::cos(a0), kRadius * std::sin(a0), 0.0f}, white};
        verts[idx++] = {{ kRadius * std::cos(a1), kRadius * std::sin(a1), 0.0f}, white};
    }

    // XZ plane circle.
    for (int i = 0; i < kSegs; ++i) {
        const float a0 = step * static_cast<float>(i);
        const float a1 = step * static_cast<float>(i + 1);
        verts[idx++] = {{ kRadius * std::cos(a0), 0.0f, kRadius * std::sin(a0)}, white};
        verts[idx++] = {{ kRadius * std::cos(a1), 0.0f, kRadius * std::sin(a1)}, white};
    }

    // YZ plane circle.
    for (int i = 0; i < kSegs; ++i) {
        const float a0 = step * static_cast<float>(i);
        const float a1 = step * static_cast<float>(i + 1);
        verts[idx++] = {{ 0.0f, kRadius * std::cos(a0), kRadius * std::sin(a0)}, white};
        verts[idx++] = {{ 0.0f, kRadius * std::cos(a1), kRadius * std::sin(a1)}, white};
    }

    vertCount_ = idx;
    gizmo_upload_vb(device, verts, idx, vb_, vbView_, "GizmoPointLight VB");
}

void GizmoPointLight::draw(ID3D12GraphicsCommandList* cmdList) {
    cmdList->IASetVertexBuffers(0, 1, &vbView_);
    cmdList->DrawInstanced(vertCount_, 1, 0, 0);
}

void GizmoPointLight::shutdown() {
    vb_.Reset();
}

} // namespace uz
