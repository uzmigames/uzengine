#pragma once

// gizmo_renderer.h — Abstract interface for per-node-type editor gizmos.
//
// Each gizmo type implements this interface with its own geometry and
// GPU resources. The GizmoRegistry maps NodeType → GizmoRenderer so
// EditorComponent can draw any gizmo without hardcoded switches.

#include <cstdint>

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl/client.h>

namespace uz {

struct Vertex;  // Forward declaration (defined in renderer.h).

// ============================================================================
// GizmoRenderer — interface for type-specific editor gizmos
// ============================================================================

class GizmoRenderer {
public:
    virtual ~GizmoRenderer() = default;

    /// Create GPU resources (vertex buffers, etc).
    /// Called once during EditorComponent::init().
    virtual void init(ID3D12Device* device) = 0;

    /// Draw this gizmo. The command list already has the line PSO set,
    /// the MVP matrix in root param 0, and the color in root param 1.
    /// The gizmo just needs to bind its VB and issue the draw call.
    virtual void draw(ID3D12GraphicsCommandList* cmdList) = 0;

    /// Release GPU resources. Safe to call multiple times.
    virtual void shutdown() = 0;
};

} // namespace uz
