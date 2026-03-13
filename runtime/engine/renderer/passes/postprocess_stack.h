/// UzEngine Post-Processing Stack
///
/// Implements a modular post-processing pipeline with individual effect toggles.
/// Each effect is registered as a separate render graph pass. The stack executes
/// passes in a defined order to avoid state conflicts.
///
/// # Supported Effects
/// - Tone Mapping (ACES filmic, HDR → LDR)
/// - Bloom (downsampling + blur + composite)
/// - FXAA (fast approximate anti-aliasing)
///
/// # Pipeline Order
/// 1. Tone Mapping (converts HDR scene to LDR)
/// 2. Bloom (downsamples, blurs, composites glow)
/// 3. FXAA (anti-aliases edges in final LDR)
/// 4. Output to backbuffer
///
/// # Effect Toggles
/// Each effect can be independently enabled/disabled via the effect descriptor.
/// Disabled effects are skipped during render graph construction.
/// This allows runtime tuning without recompiling shaders.
///
/// # Constant Buffers
/// Each pass has its own CB for tuning parameters (intensity, threshold, etc).
/// CBs are allocated from the ring allocator and bound at pass registration time.

#pragma once

#include "render_graph_types.h"
#include "constant_buffers.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz {

class CBRingAllocator;

namespace rg {
class RgBuilder;
} // namespace rg

// ─────────────────────────────────────────────────────────────────────────────
// Tone Mapping Pass
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the ACES tone mapping pass.
///
/// Converts HDR scene color to LDR [0, 1] using the ACES filmic curve.
/// Must be the first post-process pass in the stack.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct ToneMapPassDesc {
    rg::RgResourceId inputTexture;         ///< HDR scene color texture (read, ShaderResource state).
    rg::RgResourceId outputTexture;        ///< LDR tone-mapped output (write, RenderTarget state).
    CBAllocation     tonemapParams;        ///< Pre-allocated CB with tone mapping parameters.
    RHIPipelineHandle tonemapPipeline;     ///< PSO handle for tonemap_ps.hlsl.
    float            tonemapIntensity;     ///< Global intensity multiplier (default: 1.0).
    float            exposureAdjust;       ///< Exposure adjustment in log2 scale (default: 0.0).
    bool             enabled;              ///< If false, tone mapping is skipped (input passed through).
};

/// Registers the ACES tone mapping pass in the render graph.
///
/// This pass must be registered before bloom to ensure correct resource states.
///
/// # Arguments
/// * `builder` — Render graph builder.
/// * `desc` — Pass configuration (resources, parameters, PSO).
///
/// # Returns
/// Resource ID of the tone-mapped output texture.
rg::RgResourceId AddToneMapPass(rg::RgBuilder& builder, const ToneMapPassDesc& desc);

// ─────────────────────────────────────────────────────────────────────────────
// Bloom Pass
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the bloom effect (downsampling + blur + composite).
///
/// Bloom creates a glow effect by:
/// 1. Downsampling with luminance threshold (isolate bright areas).
/// 2. Creating a mip chain for progressive downsamples.
/// 3. Applying separable Gaussian blur at each level.
/// 4. Compositing the blurred bloom back onto the scene.
///
/// # Mip Levels
/// The mip chain typically has 3-5 levels, each 1/2 resolution of the previous.
/// For a 1920x1080 input:
/// - Mip 0: 1920x1080 (source, downsampled to mip 1)
/// - Mip 1: 960x540 (blurred, composited onto scene)
/// Deeper mips (2, 3, 4) can be created for wider blur with more taps.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct BloomPassDesc {
    rg::RgResourceId inputTexture;         ///< LDR scene color (read, ShaderResource state).
    rg::RgResourceId outputTexture;        ///< Scene + bloom composite (write, RenderTarget state).
    CBAllocation     downSampleParams;     ///< CB for downsampling thresholds.
    CBAllocation     blurParams;           ///< CB for blur intensity and texel offsets.
    CBAllocation     compositeParams;      ///< CB for bloom composite intensity.
    RHIPipelineHandle downsamplePipeline;  ///< PSO for bloom_downsample_ps.hlsl.
    RHIPipelineHandle blurHPipeline;       ///< PSO for bloom_blur_h_ps.hlsl.
    RHIPipelineHandle blurVPipeline;       ///< PSO for bloom_blur_v_ps.hlsl.
    RHIPipelineHandle compositePipeline;   ///< PSO for bloom_composite_ps.hlsl.
    float            bloomThreshold;       ///< Luminance threshold for bright pixel isolation (default: 0.5).
    float            bloomIntensity;       ///< Bloom contribution to final image (default: 0.5).
    float            blurIntensity;        ///< Blur strength multiplier (default: 1.0).
    uint32_t         mipLevels;            ///< Number of downsampling levels (typical: 3-5).
    float            viewportWidth;        ///< Viewport width in pixels.
    float            viewportHeight;       ///< Viewport height in pixels.
    bool             enabled;              ///< If false, bloom is skipped (input passed through).
};

/// Registers the bloom effect (downsampling + blur + composite) in the render graph.
///
/// # Arguments
/// * `builder` — Render graph builder.
/// * `desc` — Pass configuration (resources, parameters, pipelines).
///
/// # Returns
/// Resource ID of the bloom-composited output texture.
rg::RgResourceId AddBloomPass(rg::RgBuilder& builder, const BloomPassDesc& desc);

// ─────────────────────────────────────────────────────────────────────────────
// FXAA Pass
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the FXAA anti-aliasing pass.
///
/// Applies fast approximate anti-aliasing (FXAA) to reduce aliasing artifacts.
/// Should be the final post-process before output to the backbuffer.
///
/// # Algorithm
/// Detects edges via luminance contrast and blurs perpendicular to edges.
/// Non-edge pixels are returned unchanged. Much faster than MSAA or temporal AA.
///
/// # Quality vs. Performance
/// FXAA provides ~2xMSAA quality at ~10x the performance of 2xMSAA.
/// Most effective on high-frequency edges (diagonal lines, thin features).
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct FXAAPassDesc {
    rg::RgResourceId inputTexture;       ///< LDR scene color (read, ShaderResource state).
    rg::RgResourceId outputTexture;      ///< Anti-aliased output (write, RenderTarget state).
    CBAllocation     fxaaParams;         ///< CB for FXAA parameters (threshold, intensity).
    RHIPipelineHandle fxaaPipeline;      ///< PSO handle for fxaa_ps.hlsl.
    float            edgeThreshold;      ///< Edge detection threshold (default: 0.1, range: [0.05, 0.2]).
    float            blurStrength;       ///< Blur amount for edge pixels (default: 1.0, range: [0.5, 2.0]).
    float            viewportWidth;      ///< Viewport width in pixels.
    float            viewportHeight;     ///< Viewport height in pixels.
    bool             enabled;            ///< If false, FXAA is skipped (input passed through).
};

/// Registers the FXAA anti-aliasing pass in the render graph.
///
/// This should be the last post-processing pass before output to the backbuffer.
///
/// # Arguments
/// * `builder` — Render graph builder.
/// * `desc` — Pass configuration (resources, parameters, PSO).
///
/// # Returns
/// Resource ID of the anti-aliased output texture.
rg::RgResourceId AddFXAAPass(rg::RgBuilder& builder, const FXAAPassDesc& desc);

// ─────────────────────────────────────────────────────────────────────────────
// Post-Processing Stack
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the complete post-processing stack.
///
/// Combines tone mapping, bloom, and FXAA into a single coherent pipeline.
/// Each effect can be independently toggled. The stack preserves resource
/// dependencies and registers passes in the correct execution order.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct PostProcessStackDesc {
    rg::RgResourceId inputTexture;         ///< HDR scene color from main render passes.
    rg::RgResourceId outputTexture;        ///< Final LDR output for display.
    CBRingAllocator* cbAllocator;          ///< Ring allocator for pass CBs. Must not be null.

    // Tone mapping
    RHIPipelineHandle tonemapPipeline;     ///< PSO for tonemap_ps.hlsl.
    float            tonemapIntensity;     ///< Global intensity (default: 1.0).
    float            exposureAdjust;       ///< Exposure in log2 (default: 0.0).
    bool             enableTonemap;        ///< Enable tone mapping (default: true).

    // Bloom
    RHIPipelineHandle downsamplePipeline;  ///< PSO for bloom_downsample_ps.hlsl.
    RHIPipelineHandle blurHPipeline;       ///< PSO for bloom_blur_h_ps.hlsl.
    RHIPipelineHandle blurVPipeline;       ///< PSO for bloom_blur_v_ps.hlsl.
    RHIPipelineHandle bloomCompositePipeline; ///< PSO for bloom_composite_ps.hlsl.
    float            bloomThreshold;       ///< Luminance threshold (default: 0.5).
    float            bloomIntensity;       ///< Bloom contribution (default: 0.5).
    float            blurIntensity;        ///< Blur strength (default: 1.0).
    uint32_t         bloomMipLevels;       ///< Number of bloom mips (default: 3).
    bool             enableBloom;          ///< Enable bloom (default: true).

    // FXAA
    RHIPipelineHandle fxaaPipeline;        ///< PSO for fxaa_ps.hlsl.
    float            edgeThreshold;        ///< Edge detection threshold (default: 0.1).
    float            fxaaBlurStrength;     ///< FXAA blur amount (default: 1.0).
    bool             enableFXAA;           ///< Enable FXAA (default: true).

    // Viewport
    float            viewportWidth;        ///< Viewport width in pixels.
    float            viewportHeight;       ///< Viewport height in pixels.
};

/// Registers the complete post-processing stack in the render graph.
///
/// Registers tone mapping, bloom, and FXAA passes in the correct order.
/// Each enabled effect is registered in sequence. Disabled effects pass
/// their input through to the next effect.
///
/// # Execution Order
/// 1. Tone mapping (HDR → LDR)
/// 2. Bloom (downsampling + blur + composite)
/// 3. FXAA (edge-based anti-aliasing)
/// 4. Output (backbuffer write)
///
/// # Arguments
/// * `builder` — Render graph builder.
/// * `desc` — Stack configuration (all effect parameters, PSOs, toggles).
///
/// # Returns
/// Resource ID of the final post-processed output (ready for swapchain).
rg::RgResourceId AddPostProcessStack(rg::RgBuilder& builder, const PostProcessStackDesc& desc);

} // namespace uz
