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

#include "engine/renderer/gizmos/gizmo_renderer.h"

#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

namespace uz {

class GizmoTransform : public GizmoRenderer {
public:
    GizmoTransform()  = default;
    ~GizmoTransform() override = default;

    void init(ID3D12Device* device) override;
    void draw(ID3D12GraphicsCommandList* cmdList) override;
    void shutdown() override;

    /// Draw the translate handles: 3 axis arrows.
    void drawTranslate(ID3D12GraphicsCommandList* cmdList);

    /// Draw the rotate handles: 3 axis circles.
    void drawRotate(ID3D12GraphicsCommandList* cmdList);

    /// Draw the scale handles: 3 axis lines with cubes.
    void drawScale(ID3D12GraphicsCommandList* cmdList);

    /// Draw a specific mode. Called by EditorComponent.
    /// mode: 0=translate, 1=rotate, 2=scale
    /// activeAxis: -1=none, 0=X, 1=Y, 2=Z (highlighted)
    void drawMode(ID3D12GraphicsCommandList* cmdList, int mode, int activeAxis);

private:
    // Translate: 3 axis arrows
    Microsoft::WRL::ComPtr<ID3D12Resource> translateVB_;
    D3D12_VERTEX_BUFFER_VIEW translateVBView_{};
    uint32_t translateVertCount_ = 0;
    uint32_t translateAxisStart_[3]{0,0,0};  // per-axis vertex start
    uint32_t translateAxisCount_[3]{0,0,0};  // per-axis vertex count

    // Rotate: 3 axis circles
    Microsoft::WRL::ComPtr<ID3D12Resource> rotateVB_;
    D3D12_VERTEX_BUFFER_VIEW rotateVBView_{};
    uint32_t rotateVertCount_ = 0;
    uint32_t rotateAxisStart_[3]{0,0,0};
    uint32_t rotateAxisCount_[3]{0,0,0};

    // Scale: 3 axis lines + cubes
    Microsoft::WRL::ComPtr<ID3D12Resource> scaleVB_;
    D3D12_VERTEX_BUFFER_VIEW scaleVBView_{};
    uint32_t scaleVertCount_ = 0;
    uint32_t scaleAxisStart_[3]{0,0,0};
    uint32_t scaleAxisCount_[3]{0,0,0};
};

} // namespace uz
