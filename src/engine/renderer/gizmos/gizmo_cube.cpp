// gizmo_cube.cpp — Default wireframe cube gizmo (12 edges, LINE_LIST).

#include "gizmo_cube.h"
#include "gizmo_utils.h"
#include "engine/renderer/renderer.h"  // Vertex


namespace uz {

bool GizmoCube::init() {
    constexpr float h = 0.5f;
    const Float3 white = {1.0f, 1.0f, 1.0f};

    const Float3 corners[8] = {
        {-h, -h, -h}, { h, -h, -h}, { h,  h, -h}, {-h,  h, -h},
        {-h, -h,  h}, { h, -h,  h}, { h,  h,  h}, {-h,  h,  h},
    };

    constexpr int edges[12][2] = {
        {0,1}, {1,2}, {2,3}, {3,0},
        {4,5}, {5,6}, {6,7}, {7,4},
        {0,4}, {1,5}, {2,6}, {3,7},
    };

    constexpr uint32_t kVertCount = 24;
    Vertex verts[kVertCount];

    for (int e = 0; e < 12; ++e) {
        verts[e * 2 + 0] = { corners[edges[e][0]], white };
        verts[e * 2 + 1] = { corners[edges[e][1]], white };
    }

    vertCount_ = kVertCount;
    vb_ = gizmo_upload_vb(verts, kVertCount, "GizmoCube VB");
    return vb_ != RHI_NULL;
}

void GizmoCube::draw(RhiCmdList cmd) {
    rhi()->cmd_set_vertex_buffer(cmd, 0, vb_, 0, sizeof(Vertex));
    rhi()->cmd_draw(cmd, vertCount_, 1, 0, 0);
}

void GizmoCube::shutdown() {
    if (vb_ != RHI_NULL) { rhi()->buffer_destroy(vb_); vb_ = RHI_NULL; }
}

} // namespace uz
