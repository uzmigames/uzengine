#pragma once

// editor_component.h — Editor-only rendering: ground grid, wireframe nodes,
// type-specific gizmos.  Excluded from game builds (no grid, no overlays).
//
// Gizmo rendering is delegated to the GizmoRegistry, which maps NodeType
// to a self-contained GizmoRenderer. To add a new gizmo for a new NodeType,
// register it in EditorComponent::init() — no switch/case changes needed.
//
// RULE RHI-1: No backend types above rhi.h — uses RHI handles only.

#include <cstdint>
#include <mutex>
#include <vector>

#include "engine/core/math.h"
#include "engine/renderer/rhi/rhi.h"

#include "engine/renderer/gizmos/gizmo_registry.h"
#include "engine/renderer/gizmos/gizmo_transform.h"

namespace uz {

struct Vertex;

// ============================================================================
// EditorComponent — editor-only rendering (grid + gizmos)
// ============================================================================

class EditorComponent {
public:
    /// Scene node descriptor for gizmo rendering in the editor viewport.
    struct EditorNode {
        Float3 position{0.0f, 0.0f, 0.0f};
        Float3 rotation{0.0f, 0.0f, 0.0f};  // euler angles (legacy, kept for compat)
        Quat rotationQuat{0.0f, 0.0f, 0.0f, 1.0f}; // quaternion (x,y,z,w) — authoritative
        Float3 scale{1.0f, 1.0f, 1.0f};
        Float3 color{0.8f, 0.8f, 0.8f};     // wireframe tint
        uint16_t          nodeType{1};                   // NodeType enum value
        uint32_t          nodeId{0};                     // scene node ID for picking
    };

    EditorComponent()  = default;
    ~EditorComponent() = default;

    EditorComponent(const EditorComponent&)            = delete;
    EditorComponent& operator=(const EditorComponent&) = delete;
    EditorComponent(EditorComponent&&)                 = delete;
    EditorComponent& operator=(EditorComponent&&)      = delete;

    /// Initialize editor GPU resources (line PSO, grid VB, gizmo registry).
    bool init();

    /// Record draw commands for the editor overlay (grid + per-node gizmos).
    /// cameraPos is the world-space eye position used to scale the gizmo by distance.
    /// When usePivot is true, the transform gizmo is positioned at pivotPos instead
    /// of the selected node's position (used for multi-select centroid gizmo, task 7.2).
    void draw(RhiCmdList cmd,
              const Mat4& viewProj,
              Float3 cameraPos,
              uint32_t selectedNodeId = 0,
              int gizmoMode = 0,
              int gizmoActiveAxis = -1,
              bool usePivot = false,
              Float3 pivotPos = {0.0f, 0.0f, 0.0f});

    /// Release all GPU resources.
    void shutdown();

    /// Replace the editor scene nodes. Thread-safe.
    size_t loadNodes(std::vector<EditorNode> nodes);

    /// Returns a copy of the current editor nodes. Thread-safe.
    std::vector<EditorNode> getNodes() const;

private:
    bool createLinePipelineState();
    bool createSolidPipelineState();
    bool createGizmoNoDepthPSO();
    bool createGridVertexBuffer();

    // Line-list PSO (shared by grid and all gizmo draws).
    RhiPipeline linePipeline_ = RHI_NULL;

    // Solid triangle PSO (scene-depth tested — for node-type gizmos like cubes/lights).
    RhiPipeline solidPipeline_ = RHI_NULL;

    // No-depth PSO for the interactive transform gizmo — always renders on top.
    RhiPipeline gizmoNoDepthPipeline_ = RHI_NULL;

    // Ground grid vertex buffer.
    RhiBuffer   gridVertexBuffer_ = RHI_NULL;
    uint32_t    gridVertexCount_ = 0;

    // Gizmo registry — owns all gizmo renderers, keyed by NodeType.
    GizmoRegistry gizmoRegistry_;
    GizmoTransform transformGizmo_;

    // Editor scene nodes (loaded via IPC, protected by mutex).
    std::vector<EditorNode> editorNodes_;
    mutable std::mutex      editorNodesMutex_;

    // Pool of CBV buffers for per-draw b1 bindings (grid, per-node, gizmo).
    // Each draw with different b1 data needs its own UPLOAD-heap buffer.
    static constexpr uint32_t kCBVPoolSize = 64;
    RhiBuffer cbvPool_[kCBVPoolSize] = {};
    uint32_t  cbvPoolNext_ = 0;

    // Allocate a CBV from the pool, upload data, return handle.
    RhiBuffer allocAndUploadCBV(const void* data, size_t size);
};

} // namespace uz
