// gizmo_cube.cpp — Default wireframe cube gizmo (12 edges, LINE_LIST).

#include "gizmo_cube.h"
#include "gizmo_utils.h"
#include "engine/renderer/renderer.h"  // Vertex

using namespace DirectX;

namespace uz {

void GizmoCube::init(ID3D12Device* device) {
    constexpr float h = 0.5f;
    const XMFLOAT3 white = {1.0f, 1.0f, 1.0f};

    const XMFLOAT3 corners[8] = {
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
    gizmo_upload_vb(device, verts, kVertCount, vb_, vbView_, "GizmoCube VB");
}

void GizmoCube::draw(ID3D12GraphicsCommandList* cmdList) {
    cmdList->IASetVertexBuffers(0, 1, &vbView_);
    cmdList->DrawInstanced(vertCount_, 1, 0, 0);
}

void GizmoCube::shutdown() {
    vb_.Reset();
}

} // namespace uz
