#pragma once

// editor_component.h — Editor-only rendering: ground grid, wireframe nodes,
// type-specific gizmos.  Excluded from game builds (no grid, no overlays).
//
// Gizmo rendering is delegated to the GizmoRegistry, which maps NodeType
// to a self-contained GizmoRenderer. To add a new gizmo for a new NodeType,
// register it in EditorComponent::init() — no switch/case changes needed.

#include <cstdint>
#include <mutex>
#include <vector>

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include "renderer/gizmos/gizmo_registry.h"

namespace uz {

struct Vertex;

// ============================================================================
// EditorComponent — editor-only rendering (grid + gizmos)
// ============================================================================

class EditorComponent {
public:
    /// Scene node descriptor for gizmo rendering in the editor viewport.
    struct EditorNode {
        DirectX::XMFLOAT3 position{0.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT3 rotation{0.0f, 0.0f, 0.0f};  // euler angles (radians)
        DirectX::XMFLOAT3 scale{1.0f, 1.0f, 1.0f};
        DirectX::XMFLOAT3 color{0.8f, 0.8f, 0.8f};     // wireframe tint
        uint16_t          nodeType{1};                   // NodeType enum value
    };

    EditorComponent()  = default;
    ~EditorComponent() = default;

    EditorComponent(const EditorComponent&)            = delete;
    EditorComponent& operator=(const EditorComponent&) = delete;
    EditorComponent(EditorComponent&&)                 = delete;
    EditorComponent& operator=(EditorComponent&&)      = delete;

    /// Initialize editor GPU resources (line PSO, grid VB, gizmo registry).
    void init(ID3D12Device* device, ID3D12RootSignature* rootSig);

    /// Record draw commands for the editor overlay (grid + per-node gizmos).
    void draw(ID3D12GraphicsCommandList* cmdList,
              const DirectX::XMFLOAT4X4& viewProj);

    /// Release all GPU resources.
    void shutdown();

    /// Replace the editor scene nodes. Thread-safe.
    size_t loadNodes(std::vector<EditorNode> nodes);

private:
    void createLinePipelineState(ID3D12Device* device, ID3D12RootSignature* rootSig);
    void createGridVertexBuffer(ID3D12Device* device);

    // Line-list PSO (shared by grid and all gizmo draws).
    Microsoft::WRL::ComPtr<ID3D12PipelineState> linePipelineState_;

    // Ground grid vertex buffer.
    Microsoft::WRL::ComPtr<ID3D12Resource> gridVertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW              gridVertexBufferView_{};
    uint32_t                              gridVertexCount_ = 0;

    // Gizmo registry — owns all gizmo renderers, keyed by NodeType.
    GizmoRegistry gizmoRegistry_;

    // Editor scene nodes (loaded via IPC, protected by mutex).
    std::vector<EditorNode> editorNodes_;
    mutable std::mutex      editorNodesMutex_;
};

} // namespace uz
