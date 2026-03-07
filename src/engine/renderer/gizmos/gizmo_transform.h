#pragma once

// gizmo_transform.h — Interactive transform gizmo (translate/rotate/scale).
//
// Draws colored axis handles for the selected node:
//   - Translate: 3 arrows (R=X, G=Y, B=Z) with arrowheads
//   - Rotate:    3 circles (R=X, G=Y, B=Z)
//   - Scale:     3 lines with cube endpoints
//
// Unlike other GizmoRenderers (which are per-node-type), this is drawn
// ONLY for the currently selected node, overlaid on top of everything.
//
// RULE RHI-1: No backend types above rhi.h — uses RHI handles only.

#include "engine/renderer/gizmos/gizmo_renderer.h"

#include <cstdint>

namespace uz {

class GizmoTransform : public GizmoRenderer {
public:
    GizmoTransform()  = default;
    ~GizmoTransform() override = default;

    bool init() override;
    void draw(RhiCmdList cmd) override;
    void shutdown() override;

    /// Draw the translate handles: 3 axis arrows.
    void drawTranslate(RhiCmdList cmd);

    /// Draw the rotate handles: 3 axis circles.
    void drawRotate(RhiCmdList cmd);

    /// Draw the scale handles: 3 axis lines with cubes.
    void drawScale(RhiCmdList cmd);

    /// Draw a specific mode. Called by EditorComponent.
    /// mode: 0=translate, 1=rotate, 2=scale
    /// activeAxis: -1=none, 0=X, 1=Y, 2=Z, 3=free (highlighted)
    void drawMode(RhiCmdList cmd, int mode, int activeAxis);

    /// Draw the free rotation ring (billboarded circle facing camera).
    /// Must be called with a billboard MVP already set via root constants.
    void drawFreeRing(RhiCmdList cmd, bool highlighted);

private:
    // Translate: 3 axis arrows
    RhiBuffer translateVB_ = RHI_NULL;
    uint32_t translateVertCount_ = 0;
    uint32_t translateAxisStart_[3]{0,0,0};
    uint32_t translateAxisCount_[3]{0,0,0};

    // Rotate: 3 axis circles
    RhiBuffer rotateVB_ = RHI_NULL;
    uint32_t rotateVertCount_ = 0;
    uint32_t rotateAxisStart_[3]{0,0,0};
    uint32_t rotateAxisCount_[3]{0,0,0};

    // Scale: 3 axis lines + cubes
    RhiBuffer scaleVB_ = RHI_NULL;
    uint32_t scaleVertCount_ = 0;
    uint32_t scaleAxisStart_[3]{0,0,0};
    uint32_t scaleAxisCount_[3]{0,0,0};

    // Free rotation ring (XY plane circle, billboarded at draw time)
    RhiBuffer freeRingVB_ = RHI_NULL;
    uint32_t freeRingVertCount_ = 0;

    // Pre-created CBVs for axis highlight/normal colors (b1 root CBV).
    RhiBuffer brightCBV_ = RHI_NULL;  // {1.8, 1.8, 1.8, 0}
    RhiBuffer normalCBV_ = RHI_NULL;  // {1.0, 1.0, 1.0, 0}
};

} // namespace uz
