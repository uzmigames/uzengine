#pragma once

// gizmo_renderer.h — Abstract interface for per-node-type editor gizmos.
//
// Each gizmo type implements this interface with its own geometry and
// GPU resources. The GizmoRegistry maps NodeType → GizmoRenderer so
// EditorComponent can draw any gizmo without hardcoded switches.
//
// RULE RHI-1: No backend types above rhi.h — uses RHI handles only.

#include <cstdint>
#include "engine/renderer/rhi/rhi.h"

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
    virtual bool init() = 0;

    /// Draw this gizmo. The command list already has the line PSO set,
    /// the MVP matrix in root param 0, and the color in root param 1.
    /// The gizmo just needs to bind its VB and issue the draw call.
    virtual void draw(RhiCmdList cmd) = 0;

    /// Release GPU resources. Safe to call multiple times.
    virtual void shutdown() = 0;
};

} // namespace uz
