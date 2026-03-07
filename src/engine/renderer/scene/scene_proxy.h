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
    RhiTexture white_texture;  // [296..303]
    RhiTexture black_texture;  // [304..311]
    RhiTexture normal_texture; // [312..319]

    // Directional light
    Vec3    light_dir;         // [320..335]
    Vec3    light_color;       // [336..351]
    float   light_intensity;   // [352..355]
    // Shadow params (packed into former _pad2 to stay within 64-DWORD root sig budget)
    float   shadow_bias;       // [356..359] bias to avoid shadow acne
    float   shadow_ortho_size; // [360..363] orthographic half-size (world units)
    float   shadow_light_dist; // [364..367] light eye distance from scene center

    // CPU-side texture handles (bound via cmd_set_texture, not uploaded to GPU)
    RhiTexture  shadow_map;    // [368..375] handle to 2048x2048 D32 depth texture
    RhiTexture  brdf_lut;      // [376..383] 256x256 RG16F split-sum BRDF LUT

    // Per-frame CBV handle for LightFrameCB (b2 refactored from root constants to root CBV)
    RhiBuffer   light_frame_cbv;   // [384..391] GPU constant buffer for b2

    // IBL cubemap handles (bound via cmd_set_texture at t8, t9)
    RhiTexture  irradiance_cube;   // [392..399] 32x32 diffuse irradiance cubemap
    RhiTexture  prefiltered_cube;  // [400..407] 128x128 specular prefiltered cubemap (5 mips)
    uint64_t    _ibl_pad;          // [408..415] alignment padding
};

static_assert(sizeof(FrameConstants) == 416, "FrameConstants size mismatch");
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
