/// Mipmap Generation Compute Shader
///
/// Downsamples a source mip level to produce the next mip level using
/// bilinear filtering (average of 4 texels). Designed as UzEngine's
/// first compute shader and async compute queue proof-of-concept.
///
/// Dispatch: ceil(outputWidth / 8), ceil(outputHeight / 8), 1

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants — source mip dimensions and reciprocals
// ─────────────────────────────────────────────────────────────────────────────

cbuffer MipGenParams : register(b0)
{
    /// Source mip width in texels.
    float srcWidth;
    /// Source mip height in texels.
    float srcHeight;
    /// 1.0 / srcWidth — precomputed to avoid per-thread division.
    float invSrcWidth;
    /// 1.0 / srcHeight — precomputed to avoid per-thread division.
    float invSrcHeight;
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

/// Source mip level to read from.
Texture2D<float4> srcTexture : register(t0);

/// Bilinear sampler with clamp addressing to avoid border artifacts.
SamplerState linearSampler : register(s0);

/// Destination mip level to write into.
RWTexture2D<float4> dstTexture : register(u0);

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    // Early-out for threads beyond output dimensions.
    // Output is half the source size (rounded up by dispatch granularity).
    float dstWidth  = max(srcWidth  * 0.5f, 1.0f);
    float dstHeight = max(srcHeight * 0.5f, 1.0f);

    if (dtid.x >= (uint)dstWidth || dtid.y >= (uint)dstHeight)
        return;

    // Map output texel center to source UV.
    // +0.5 centers on the texel; multiply by inverse source dims to get [0,1] UV.
    // Each output texel corresponds to a 2x2 block in the source, so we offset
    // by 1 source texel (the bilinear tap naturally averages the 2x2 neighborhood).
    float2 uv = (dtid.xy * 2.0f + 1.0f) * float2(invSrcWidth, invSrcHeight);

    // Bilinear sample averages the 4 source texels covered by this output texel.
    float4 color = srcTexture.SampleLevel(linearSampler, uv, 0);

    dstTexture[dtid.xy] = color;
}
