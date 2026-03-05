#pragma once

// gizmo_camera.h — Camera frustum wireframe gizmo.
// RULE RHI-1: No backend types above rhi.h — uses RHI handles only.

#include "engine/renderer/gizmos/gizmo_renderer.h"

namespace uz {

class GizmoCamera final : public GizmoRenderer {
public:
    bool init() override;
    void draw(RhiCmdList cmd) override;
    void shutdown() override;

private:
    RhiBuffer vb_ = RHI_NULL;
    uint32_t  vertCount_ = 0;
};

} // namespace uz
