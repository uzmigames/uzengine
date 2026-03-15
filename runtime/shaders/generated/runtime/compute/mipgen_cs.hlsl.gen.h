// mipgen_cs.hlsl.gen.h -- AUTO-GENERATED, DO NOT EDIT
#pragma once
namespace uz::generated {
inline constexpr char kMipgenCS[] = R"hlsl(
/// Mipmap generation compute shader for async compute queue.
///
/// Generates one mip level per dispatch by downsampling 2x2 texels using
/// bilinear filtering. Designed to run on the async compute queue while
/// the graphics queue renders the current frame.
///
/// # Inputs
/// - `g_InputMip` (t0): Source mip level to downsample.
/// - `g_LinearSampler` (s0): Bilinear sampler for 2x2 box filter.
/// - Root constants (b0): output dimensions + reciprocal input dimensions.
///
/// # Outputs
/// - `g_OutputMip` (u0): Destination mip level (half resolution of input).
///
/// # Thread Group
/// [8, 8, 1] — one thread per output texel. Dispatch ceil(width/8) x ceil(height/8) groups.

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants
// ─────────────────────────────────────────────────────────────────────────────

cbuffer MipgenConstants : register(b0)
{
    uint2  g_OutputSize;   // Output mip dimensions in texels
    float2 g_TexelSize;    // 1.0 / inputSize (for UV computation)
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

Texture2D<float4>   g_InputMip      : register(t0);
RWTexture2D<float4> g_OutputMip     : register(u0);
SamplerState        g_LinearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= g_OutputSize.x || dtid.y >= g_OutputSize.y)
        return;

    // Sample center of the 2x2 block in the input mip.
    // Bilinear filtering produces the box-filter average automatically.
    float2 uv = (float2(dtid.xy) + 0.5) * g_TexelSize;
    g_OutputMip[dtid.xy] = g_InputMip.SampleLevel(g_LinearSampler, uv, 0);
}
)hlsl";
} // namespace uz::generated
