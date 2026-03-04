#pragma once

// gizmo_cube.h — Default wireframe cube gizmo for generic nodes.

#include "renderer/gizmos/gizmo_renderer.h"

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl/client.h>

namespace uz {

class GizmoCube final : public GizmoRenderer {
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
