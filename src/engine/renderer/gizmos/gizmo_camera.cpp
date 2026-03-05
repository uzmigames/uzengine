// gizmo_camera.cpp — Camera frustum wireframe gizmo.
//
// Draws a truncated pyramid (near rect + far rect + connecting edges)
// with an "up" triangle indicator, looking down +Z.

#include "gizmo_camera.h"
#include "gizmo_utils.h"
#include "engine/renderer/renderer.h"


namespace uz {

bool GizmoCamera::init() {
    const Float3 white = {1.0f, 1.0f, 1.0f};

    // Near plane corners (small, at z=0.2).
    const float nw = 0.15f, nh = 0.1f, nz = 0.2f;
    const Float3 n0 = {-nw, -nh, nz};
    const Float3 n1 = { nw, -nh, nz};
    const Float3 n2 = { nw,  nh, nz};
    const Float3 n3 = {-nw,  nh, nz};

    // Far plane corners (larger, at z=1.2).
    const float fw = 0.6f, fh = 0.4f, fz = 1.2f;
    const Float3 f0 = {-fw, -fh, fz};
    const Float3 f1 = { fw, -fh, fz};
    const Float3 f2 = { fw,  fh, fz};
    const Float3 f3 = {-fw,  fh, fz};

    const Float3 origin = {0.0f, 0.0f, 0.0f};

    Vertex verts[40];
    uint32_t idx = 0;

    // Near plane rectangle.
    verts[idx++] = {n0, white}; verts[idx++] = {n1, white};
    verts[idx++] = {n1, white}; verts[idx++] = {n2, white};
    verts[idx++] = {n2, white}; verts[idx++] = {n3, white};
    verts[idx++] = {n3, white}; verts[idx++] = {n0, white};

    // Far plane rectangle.
    verts[idx++] = {f0, white}; verts[idx++] = {f1, white};
    verts[idx++] = {f1, white}; verts[idx++] = {f2, white};
    verts[idx++] = {f2, white}; verts[idx++] = {f3, white};
    verts[idx++] = {f3, white}; verts[idx++] = {f0, white};

    // Connecting edges (origin → far corners).
    verts[idx++] = {origin, white}; verts[idx++] = {f0, white};
    verts[idx++] = {origin, white}; verts[idx++] = {f1, white};
    verts[idx++] = {origin, white}; verts[idx++] = {f2, white};
    verts[idx++] = {origin, white}; verts[idx++] = {f3, white};

    // "Up" indicator triangle on top of the near plane.
    const Float3 upL = {-0.1f, nh,         nz};
    const Float3 upR = { 0.1f, nh,         nz};
    const Float3 upT = { 0.0f, nh + 0.12f, nz};
    verts[idx++] = {upL, white}; verts[idx++] = {upR, white};
    verts[idx++] = {upR, white}; verts[idx++] = {upT, white};
    verts[idx++] = {upT, white}; verts[idx++] = {upL, white};

    vertCount_ = idx;
    vb_ = gizmo_upload_vb(verts, idx, "GizmoCamera VB");
    return vb_ != RHI_NULL;
}

void GizmoCamera::draw(RhiCmdList cmd) {
    rhi()->cmd_set_vertex_buffer(cmd, 0, vb_, 0, sizeof(Vertex));
    rhi()->cmd_draw(cmd, vertCount_, 1, 0, 0);
}

void GizmoCamera::shutdown() {
    if (vb_ != RHI_NULL) { rhi()->buffer_destroy(vb_); vb_ = RHI_NULL; }
}

} // namespace uz
