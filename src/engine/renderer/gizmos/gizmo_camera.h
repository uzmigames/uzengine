#pragma once

// gizmo_camera.h — Camera frustum wireframe gizmo.

#include "engine/renderer/gizmos/gizmo_renderer.h"

#include <d3d12.h>
#include <wrl/client.h>

namespace uz {

class GizmoCamera final : public GizmoRenderer {
public:
    void init(ID3D12Device* device) override;
    void draw(ID3D12GraphicsCommandList* cmdList) override;
    void shutdown() override;

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> vb_;
    D3D12_VERTEX_BUFFER_VIEW              vbView_{};
    uint32_t                              vertCount_ = 0;
};

} // namespace uz
