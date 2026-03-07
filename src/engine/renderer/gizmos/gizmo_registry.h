#pragma once

// gizmo_registry.h — Maps NodeType → GizmoRenderer.
//
// The registry owns all gizmo instances and provides O(1) lookup by
// NodeType enum value. To add a new gizmo for a new NodeType, just
// register it — no changes to EditorComponent needed.
//
// RULE RHI-1: No backend types above rhi.h — uses RHI handles only.

#include "engine/renderer/gizmos/gizmo_renderer.h"

#include <array>
#include <cstdint>
#include <memory>

namespace uz {

// Maximum NodeType enum value we support (from scene_node.h: Count = 9).
inline constexpr uint16_t kMaxNodeType = 16;

class GizmoRegistry {
public:
    GizmoRegistry()  = default;
    ~GizmoRegistry() = default;

    // Non-copyable.
    GizmoRegistry(const GizmoRegistry&)            = delete;
    GizmoRegistry& operator=(const GizmoRegistry&) = delete;

    /// Register a gizmo for a given NodeType. Takes ownership.
    void registerGizmo(uint16_t nodeType, std::unique_ptr<GizmoRenderer> gizmo);

    /// Set the fallback gizmo used when no type-specific gizmo is registered.
    void setDefaultGizmo(std::unique_ptr<GizmoRenderer> gizmo);

    /// Initialize all registered gizmos (called during EditorComponent::init).
    void initAll();

    /// Shutdown all registered gizmos.
    void shutdownAll();

    /// Suppress gizmo drawing for a given NodeType (returns nullptr from get).
    void suppressGizmo(uint16_t nodeType);

    /// Get the gizmo for a given NodeType. Returns nullptr if suppressed,
    /// the type-specific gizmo if registered, or the default gizmo otherwise.
    GizmoRenderer* get(uint16_t nodeType) const;

private:
    std::array<std::unique_ptr<GizmoRenderer>, kMaxNodeType> gizmos_{};
    std::array<bool, kMaxNodeType> suppressed_{};
    std::unique_ptr<GizmoRenderer> default_;
};

} // namespace uz
