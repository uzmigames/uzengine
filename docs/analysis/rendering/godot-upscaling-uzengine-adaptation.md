<!-- doc-meta
title: Godot Upscaling Patterns — UzEngine Adaptation Guide
category: analysis
engine: multi-engine
theme: rendering
created: 2026-03-16
updated: 2026-03-16
status: active
author: godot-architect
-->

## Overview

Practical guide for adapting Godot 4's high-resolution rendering and upscaling architecture to UzEngine. This document complements the detailed analysis in `godot-high-resolution-upscaling.md` with concrete C++ code patterns, DX12-specific adaptations, and integration points within UzEngine's existing renderer.

## Context

UzEngine needs 4K support on day one. Godot's viewport-based scaling + pluggable upscaler strategy is clean and proven. This guide shows how to:
1. Integrate FSR 1.0 and FSR 2.0 into UzEngine's render graph
2. Manage temporal state (motion vectors, jitter phases) across frames
3. Handle fallback when upscalers unavailable
4. Expose configuration to game code via Lua API

---

## Architecture Integration Points

### 1. Renderer Configuration (uz::RendererConfig)

Add scaling settings to the main renderer config:

```cpp
// renderer_config.h
struct RendererConfig {
    struct Scaling3D {
        enum Mode : uint8_t {
            Mode_Bilinear = 0,
            Mode_FSR1 = 1,
            Mode_FSR2 = 2,
            Mode_MetalFXSpatial = 3,
            Mode_MetalFXTemporal = 4,
        };

        Mode mode = Mode_Bilinear;
        float scale = 1.0f;  // 0.25 to 2.0
        float fsr_sharpness = 0.2f;  // 0.0 to 2.0
        bool auto_enable_motion_vectors = true;
        bool auto_disable_taa_with_temporal = true;
    } scaling_3d;
};
```

### 2. Viewport Class Extension

Each viewport needs scaling state:

```cpp
// uz_viewport.h
struct Viewport {
    Size2i target_size;      // Display resolution (e.g., 3840x2160)
    Size2i internal_size;    // Render resolution (computed)

    float scaling_3d_scale = 1.0f;
    uz::RendererConfig::Scaling3D::Mode scaling_3d_mode;

    uint32_t jitter_phase = 0;
    uint32_t jitter_phase_count = 0;

    // Set by _configure_3d_render_buffers()
    float texture_mipmap_bias = 0.0f;
    bool use_motion_vectors = false;
};

// Methods
void set_scaling_3d_mode(uz::RendererConfig::Scaling3D::Mode mode) {
    scaling_3d_mode = mode;
    // Trigger reconfiguration before next render
    needs_3d_buffer_reconfigure = true;
}

void set_scaling_3d_scale(float scale) {
    scaling_3d_scale = uz::Clamp(scale, 0.25f, 2.0f);
    needs_3d_buffer_reconfigure = true;
}
```

### 3. Render Buffer Configuration

During `BeginFrame()`, call `_configure_3d_render_buffers()`:

```cpp
// renderer.cpp
void Renderer::_configure_3d_render_buffers(Viewport& viewport) {
    if (viewport.target_size.width == 0 || viewport.target_size.height == 0) {
        return;
    }

    // 1. Validate scaling mode against device capabilities
    uz::RendererConfig::Scaling3D::Mode mode = viewport.scaling_3d_mode;
    float scale = viewport.scaling_3d_scale;

    // Check if upscaler available
    bool upscaler_available = m_capabilities.supports_fsr2;

    // Fallback chain
    if (mode == Mode_FSR2 && !m_capabilities.supports_fsr2) {
        mode = Mode_FSR1;
        if (!upscaler_available) mode = Mode_Bilinear;
    }

    // Don't upscale (only downscale)
    if (mode != Mode_Bilinear && scale > 1.0f) {
        UZ_LOG(Render, Warning, "FSR not designed for downsampling; falling back to bilinear");
        mode = Mode_Bilinear;
    }

    // 2. Compute internal resolution
    int target_width = viewport.target_size.width;
    int target_height = viewport.target_size.height;
    int render_width, render_height;

    switch (mode) {
        case Mode_Bilinear:
            render_width = uz::Clamp(int(target_width * scale), 1, 16384);
            render_height = uz::Clamp(int(target_height * scale), 1, 16384);
            break;
        case Mode_FSR1:
        case Mode_FSR2:
            render_width = uz::Max(int(target_width * scale), 1);
            render_height = uz::Max(int(target_height * scale), 1);
            break;
    }

    viewport.internal_size = Size2i(render_width, render_height);

    // 3. Compute jitter phase count for temporal upscalers
    uint32_t jitter_phase_count = 0;
    if (mode == Mode_FSR2) {
        // FFX-FSR2 jitter phases = 8 * scale_ratio^2
        float scale_ratio = float(target_width) / render_width;
        jitter_phase_count = uint32_t(8.0f * scale_ratio * scale_ratio);
    }
    viewport.jitter_phase_count = jitter_phase_count;
    viewport.jitter_phase = 0;  // Reset phase counter each frame (or between scenes)

    // 4. Auto-adjust mipmap bias
    viewport.texture_mipmap_bias = std::log2(uz::Min(scale, 1.0f));

    // 5. Check motion vector requirements
    viewport.use_motion_vectors = (mode == Mode_FSR2);

    // 6. Configure render graph
    m_render_graph.set_render_target_size(viewport.render_target_rid, viewport.internal_size);
}
```

---

## Upscaler Implementation Pattern

### Base Class

```cpp
// uz_upscaler.h
namespace uz {

class Upscaler {
public:
    virtual ~Upscaler() = default;

    virtual void Initialize(uz::RHI* rhi) = 0;
    virtual void Shutdown() = 0;

    // Process: read from source_texture, write to dest_texture
    // Dispatch on compute or draw as appropriate
    virtual void Process(
        uz::RHI* rhi,
        uz::RenderGraphBuilder& graph,
        Size2i internal_size,
        Size2i target_size,
        uz::RID source_texture,
        uz::RID dest_texture,
        float sharpness
    ) = 0;

    virtual const char* GetLabel() const = 0;
};

}  // namespace uz
```

### FSR 1.0 Implementation

```cpp
// uz_upscaler_fsr1.h
namespace uz {

class UpscalerFSR1 : public Upscaler {
private:
    struct {
        uz::RID shader;
        uz::RID pipeline;
    } passes[2];  // EASU, RCAS

    uz::RID sampler_linear;

public:
    void Initialize(uz::RHI* rhi) override;
    void Shutdown() override;
    void Process(uz::RHI* rhi, uz::RenderGraphBuilder& graph,
                 Size2i internal_size, Size2i target_size,
                 uz::RID source_texture, uz::RID dest_texture,
                 float sharpness) override;
    const char* GetLabel() const override { return "FSR 1.0"; }
};

}  // namespace uz
```

### FSR 2.0 Implementation (Simplified)

```cpp
// uz_upscaler_fsr2.h
namespace uz {

class UpscalerFSR2 : public Upscaler {
private:
    struct FrameState {
        uz::RID history_color;
        uz::RID depth_pyramid;
        uz::RID luminance_pyramid;
    };

    static constexpr int FRAME_HISTORY = 4;
    FrameState frame_history[FRAME_HISTORY];
    int frame_index = 0;

    struct {
        uz::RID shader;
        uz::RID pipeline;
    } passes[9];  // Depth clip, lock, accumulate, RCAS, etc.

    uz::RID sampler_linear, sampler_point;

public:
    void Initialize(uz::RHI* rhi) override;
    void Shutdown() override;

    void Process(uz::RHI* rhi, uz::RenderGraphBuilder& graph,
                 Size2i internal_size, Size2i target_size,
                 uz::RID source_texture, uz::RID dest_texture,
                 float sharpness) override;

    void AdvanceFrame() {
        frame_index = (frame_index + 1) % FRAME_HISTORY;
    }

    const char* GetLabel() const override { return "FSR 2.0"; }
};

}  // namespace uz
```

---

## Render Graph Integration

### Adding Upscaling Pass

```cpp
// During RenderGraph construction
void Renderer::BuildRenderGraph(const Viewport& viewport) {
    uz::RenderGraphBuilder graph = m_render_graph.Builder();

    // ... other passes ...

    // Base pass outputs to viewport.internal_size
    uz::RID base_pass_output = graph.AddPass<BasePass>(viewport.internal_size);

    // Post-processing (tone map, color grading) still at internal resolution
    uz::RID post_output = graph.AddPass<PostProcessPass>(
        base_pass_output,
        viewport.internal_size
    );

    // Upscaling pass (if needed)
    uz::RID final_output;
    if (viewport.scaling_3d_mode != Mode_Bilinear || viewport.scaling_3d_scale != 1.0f) {
        if (viewport.scaling_3d_mode == Mode_Bilinear) {
            // Simple bilinear via fullscreen quad
            final_output = graph.AddPass<BilinearUpscalePass>(
                post_output,
                viewport.internal_size,
                viewport.target_size
            );
        } else if (viewport.scaling_3d_mode == Mode_FSR2) {
            final_output = graph.AddPass<ComputeUpscalePass>(
                m_upscaler_fsr2,
                post_output,
                viewport.internal_size,
                viewport.target_size,
                viewport.fsr_sharpness
            );
        }
    } else {
        final_output = post_output;
    }

    // Output to swapchain
    graph.SetOutput(final_output, viewport.target_size);

    // Compile and cache
    m_render_graph.Compile(graph);
}
```

---

## Motion Vector Support

### 1. Depth/Normal Pass Modification

Add velocity output to depth prepass:

```cpp
// uz_depth_prepass.hlsl
cbuffer CameraData : register(b0) {
    float4x4 view_proj;
    float4x4 prev_view_proj;  // Previous frame camera
    float3 camera_position;
    float padding0;
};

struct PSOutput {
    float depth : SV_Depth;
    float2 velocity : SV_Target1;  // New output
};

PSOutput main(VertexOutput input) : SV_TARGET {
    PSOutput output;

    float4 proj = mul(float4(input.world_pos, 1.0), view_proj);
    float4 prev_proj = mul(float4(input.prev_frame_world_pos, 1.0), prev_view_proj);

    float2 current_screen = proj.xy / proj.w;
    float2 prev_screen = prev_proj.xy / prev_proj.w;

    output.velocity = (current_screen - prev_screen) * 0.5;  // NDC to [0,1]
    output.depth = proj.z / proj.w;

    return output;
}
```

### 2. Vertex Shader Jitter

Apply camera jitter to camera matrices:

```cpp
// renderer.cpp: Update camera constant buffer
void Renderer::ApplyCameraJitter(Viewport& viewport, CameraData& cam_data) {
    if (viewport.jitter_phase_count == 0) {
        return;  // No jitter needed
    }

    // Halton sequence or random jitter
    float jitter_x = ComputeHaltonSequence(viewport.jitter_phase, 2) - 0.5f;
    float jitter_y = ComputeHaltonSequence(viewport.jitter_phase, 3) - 0.5f;

    // Normalize to pixel units
    jitter_x = jitter_x / viewport.target_size.width;
    jitter_y = jitter_y / viewport.target_size.height;

    // Apply to projection matrix (not view)
    Matrix4x4 jitter_mtx = Matrix4x4::Translation(jitter_x, jitter_y, 0.0f);
    cam_data.view_proj = cam_data.view * jitter_mtx * cam_data.projection;

    // Advance jitter phase
    viewport.jitter_phase = (viewport.jitter_phase + 1) % viewport.jitter_phase_count;
}
```

### 3. Auto-Enable Motion Vectors

```cpp
// renderer.cpp: During _configure_3d_render_buffers()
void Renderer::UpdateMotionVectorRequirement(Viewport& viewport) {
    bool needs_motion_vectors = (viewport.scaling_3d_mode == Mode_FSR2);

    if (needs_motion_vectors != viewport.use_motion_vectors) {
        viewport.use_motion_vectors = needs_motion_vectors;

        if (needs_motion_vectors) {
            // Enable motion vector pass in render graph
            m_render_graph.EnablePass("MotionVectorPass");
            UZ_LOG(Render, Info, "Motion vectors enabled for FSR2");
        } else {
            // Disable if no longer needed
            m_render_graph.DisablePass("MotionVectorPass");
            UZ_LOG(Render, Info, "Motion vectors disabled (not needed)");
        }
    }
}
```

---

## Fallback Logic Implementation

```cpp
// renderer.cpp
uz::RendererConfig::Scaling3D::Mode Renderer::ResolveUpscalerMode(
    uz::RendererConfig::Scaling3D::Mode requested_mode,
    float scale) {

    auto mode = requested_mode;

    // 1. Ignore scale == 1.0 with upscalers (no need to upscale)
    if (scale >= (1.0f - EPSILON) && scale <= (1.0f + EPSILON)) {
        if (mode != Mode_Bilinear) {
            mode = Mode_Bilinear;  // Overhead not worth it
        }
    }

    // 2. Fallback for temporal upscalers
    if (mode == Mode_FSR2 && !m_capabilities.supports_fsr2) {
        mode = Mode_FSR1;
        UZ_LOG(Render, Warning, "FSR2 not available; using FSR1");
    }

    // 3. Fallback for spatial upscalers
    if (mode == Mode_FSR1 && !m_capabilities.supports_fsr1) {
        mode = Mode_Bilinear;
        UZ_LOG(Render, Warning, "FSR1 not available; using bilinear");
    }

    // 4. Prevent downsampling with FSR
    if (mode != Mode_Bilinear && scale > (1.0f + EPSILON)) {
        mode = Mode_Bilinear;
        UZ_LOG(Render, Warning, "FSR not designed for downsampling (scale > 1.0)");
    }

    // 5. Check if upscaler is actually enabled (not low-end GPU)
    if (mode != Mode_Bilinear && !m_is_upscaling_available) {
        mode = Mode_Bilinear;
        UZ_LOG(Render, Warning, "Upscaling not available on this GPU");
    }

    return mode;
}
```

---

## Lua API Exposure

### Viewport Scaling Configuration

```cpp
// lua_api/scene.cpp
void Register_Viewport_Scaling_API(lua_State* L) {
    // uz.viewport.set_scaling_3d_mode(viewport_id, mode_enum)
    lua_register(L, "set_scaling_3d_mode", [](lua_State* L) {
        uint32_t vp_id = luaL_checkinteger(L, 1);
        int mode = luaL_checkinteger(L, 2);

        auto vp = g_renderer->GetViewport(vp_id);
        if (vp) {
            vp->set_scaling_3d_mode(static_cast<uz::RendererConfig::Scaling3D::Mode>(mode));
        }
        return 0;
    });

    // uz.viewport.set_scaling_3d_scale(viewport_id, scale)
    lua_register(L, "set_scaling_3d_scale", [](lua_State* L) {
        uint32_t vp_id = luaL_checkinteger(L, 1);
        float scale = luaL_checknumber(L, 2);

        auto vp = g_renderer->GetViewport(vp_id);
        if (vp) {
            vp->set_scaling_3d_scale(scale);
        }
        return 0;
    });

    // uz.viewport.set_fsr_sharpness(viewport_id, sharpness)
    lua_register(L, "set_fsr_sharpness", [](lua_State* L) {
        uint32_t vp_id = luaL_checkinteger(L, 1);
        float sharpness = luaL_checknumber(L, 2);

        auto vp = g_renderer->GetViewport(vp_id);
        if (vp) {
            vp->fsr_sharpness = uz::Clamp(sharpness, 0.0f, 2.0f);
        }
        return 0;
    });
}
```

### Game-Side Lua Script

```lua
-- Configure 4K rendering with FSR2 1440p upscaling
local vp = uz.scene.get_main_viewport()
uz.viewport.set_scaling_3d_mode(vp, uz.SCALING_3D_MODE_FSR2)
uz.viewport.set_scaling_3d_scale(vp, 0.66)  -- 1440p → 4K
uz.viewport.set_fsr_sharpness(vp, 0.2)  -- Default sharpness

-- Or: native 4K (no upscaling)
uz.viewport.set_scaling_3d_scale(vp, 1.0)
```

---

## Capability Detection

### At Engine Startup

```cpp
// renderer.cpp: Constructor/Initialize()
void Renderer::DetectCapabilities() {
    // Check FSR support
    m_capabilities.supports_fsr1 = true;  // FSR1 works everywhere
    m_capabilities.supports_fsr2 = m_rhi->DeviceSupportsFeature(
        uz::RHIFeature::ComputeShaders
    ) && m_rhi->DeviceMemoryMiB() >= 2048;  // FSR2 needs compute + ~2GB VRAM

    // Check if low-end GPU (disable upscaling on weak systems)
    m_is_upscaling_available =
        !m_rhi->IsLowEndDevice() &&
        m_rhi->DeviceMemoryMiB() >= 1024;

    if (!m_is_upscaling_available) {
        UZ_LOG(Render, Warning, "Upscaling disabled on low-end GPU");
    }
}
```

---

## Performance Notes

| Upscaler | CPU Cost | GPU Cost | Memory | Quality |
|----------|----------|----------|--------|---------|
| Bilinear | Minimal | 1 pass | Minimal | Poor (blurry) |
| FSR 1.0 | Low | 2 passes (16×16 blocks) | 1×target | Good (spatial) |
| FSR 2.0 | Medium | 9 passes + history | 4×history + buffers | Excellent (temporal) |

**For 4K:**
- Scale=0.66: 44% of native pixel count → 2.25× speedup (rough estimate)
- FSR2 adds ~5-10% GPU cost (compute passes)
- Motion vectors add ~3% GPU cost (extra output from depth pass)

---

## Key Takeaways

1. **Viewport owns scaling config**: Each viewport independently sets mode/scale. Reconfigure during `BeginFrame()` if `needs_3d_buffer_reconfigure` is true.

2. **Fallback is automatic**: Engine resolves preferred mode to available mode at config time (not per-frame).

3. **FSR 2.0 requires motion vectors**: Auto-enable when FSR2 active. Gates motion vector rendering per-viewport.

4. **Jitter is per-viewport, per-frame**: Phase advances each frame; resets on resolution change.

5. **Mipmap bias compensates automatically**: Negative bias at lower scales keeps textures sharp; formula = log₂(scale).

6. **TAA conflicts with temporal upscalers**: Disable TAA internally when FSR2/MetalFX Temporal is active (avoid double-jitter).

---

## References

- Godot-detailed analysis: [godot-high-resolution-upscaling.md](godot-high-resolution-upscaling.md)
- AMD FSR2 SDK: Official FFX-FSR2 documentation
- UzEngine specs: `/docs/specs/rendering/` (for integration points)
- UE5 upscaling: UE5 supports Lumen upsampling + TSR (temporal super-resolution) as alternative patterns

---

## Changelog

| Date | Change |
|------|--------|
| 2026-03-16 | Initial creation — C++ adaptation patterns, render graph integration, Lua API, and capability detection |
