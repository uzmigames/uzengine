#pragma once

#include "engine/core/math.h"
#include "engine/renderer/rhi/rhi.h"
#include "engine/renderer/core/render_graph.h"

namespace uz {

// ============================================================================
// FrameConstants — GPU-uploaded per-frame data (uploaded once per frame)
// ============================================================================

struct alignas(16) FrameConstants {
    Mat4    view;              // [0..63]
    Mat4    proj;              // [64..127]
    Mat4    view_proj;         // [128..191]
    Mat4    inv_view_proj;     // [192..255]
    Vec3    camera_world_pos;  // [256..271] (Vec3 is 16 bytes with internal pad)
    Vec2    resolution;        // [272..279]
    Vec2    inv_resolution;    // [280..287]
    float   time;              // [288..291]
    float   delta_time;        // [292..295]
    RhiTexture white_texture;  // [296..303] (8 bytes on 64-bit)

    // Directional light
    Vec3    light_dir;         // [304..319]
    Vec3    light_color;       // [320..335]
    float   light_intensity;   // [336..339]
    float   _pad2[3];          // [340..351]
};

static_assert(sizeof(FrameConstants) == 352, "FrameConstants size mismatch");
static_assert(sizeof(FrameConstants) % 16 == 0, "FrameConstants must be 16-byte aligned for GPU upload");

// ============================================================================
// ISceneProxy — Renderer-facing view of a scene component
// ============================================================================

/// Created on game thread, consumed on render thread.
/// Proxies are IMMUTABLE during a frame — created/destroyed between frames.
struct ISceneProxy {
    virtual ~ISceneProxy() = default;

    /// Called during render graph setup phase.
    /// Proxy declares what render graph resources it reads/writes.
    virtual void setup(RgPassBuilder& builder) { (void)builder; }

    /// Called during render graph execute phase.
    /// Issues actual draw calls via rhi()->cmd_*().
    virtual void execute(RhiCmdList cmd, const FrameConstants& fc) = 0;

    /// Returns the axis-aligned bounding box for frustum culling (world space).
    /// Return empty AABB to always render (e.g., directional lights).
    [[nodiscard]] virtual uz::Aabb bounds() const = 0;

    /// Returns the render layer flags (opaque, transparent, shadow, etc.)
    [[nodiscard]] virtual uint32_t render_flags() const = 0;

    /// Called when the source component's data changes (game thread, between frames).
    /// The proxy MUST copy all data it needs — no shared pointers to game state.
    virtual void sync_from_component(const void* component_data) = 0;
};

// Render layer flags
inline constexpr uint32_t RENDER_LAYER_NONE        = 0;
inline constexpr uint32_t RENDER_LAYER_OPAQUE      = 1 << 0;
inline constexpr uint32_t RENDER_LAYER_TRANSPARENT = 1 << 1;
inline constexpr uint32_t RENDER_LAYER_SHADOW      = 1 << 2;
inline constexpr uint32_t RENDER_LAYER_EDITOR      = 1 << 3;

} // namespace uz
