// gizmo_point_light.cpp — Point light gizmo.
//
// Three orthogonal wireframe circles (XY, XZ, YZ) centered at origin.

#include "gizmo_point_light.h"
#include "gizmo_utils.h"
#include "engine/renderer/core/renderer.h"

#include <cmath>


namespace uz {

bool GizmoPointLight::init() {
    const Float3 white = {1.0f, 1.0f, 1.0f};

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
    vb_ = gizmo_upload_vb(verts, idx, "GizmoPointLight VB");
    return vb_ != RHI_NULL;
}

void GizmoPointLight::draw(RhiCmdList cmd) {
    rhi()->cmd_set_vertex_buffer(cmd, 0, vb_, 0, sizeof(Vertex));
    rhi()->cmd_draw(cmd, vertCount_, 1, 0, 0);
}

void GizmoPointLight::shutdown() {
    if (vb_ != RHI_NULL) { rhi()->buffer_destroy(vb_); vb_ = RHI_NULL; }
}

} // namespace uz
