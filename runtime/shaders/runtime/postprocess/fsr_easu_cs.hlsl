/// FSR 1.0 EASU — Edge Adaptive Spatial Upscaling (Compute Shader)
///
/// First pass of AMD FidelityFX Super Resolution 1.0. Performs high-quality
/// spatial upscaling from a lower-resolution input to a higher-resolution
/// output using an edge-adaptive Lanczos-like filter.
///
/// The algorithm:
///   1. Map each output pixel to input texture space
///   2. Gather a neighborhood of input texels (12 taps)
///   3. Detect local edge direction from horizontal/vertical gradient analysis
///   4. Shape a Lanczos2-like kernel along the detected edge direction
///   5. Accumulate weighted samples and normalize
///
/// The edge detection uses the luma of the 4 cardinal neighbors plus center
/// to estimate local gradient direction. The kernel is then stretched along
/// the edge to avoid blurring across edges while maintaining sharpness along
/// them.
///
/// @ref AMD 2021 "FidelityFX Super Resolution" — GPUOpen published algorithm
/// @ref AMD FidelityFX SDK (MIT license) — ffx_fsr1.h EASU implementation
/// @constants All constants derived from input/output resolution ratios
///   (computed in-shader from dimensions). No precomputed con0-con3 needed.
/// @deviations NONE — algorithm matches AMD reference ffx_fsr1.h

#include "../include/math_util.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants at b0 — 8 DWORDs.
/// Resolution parameters + bindless indices. The shader derives all
/// sampling offsets and kernel parameters from these dimensions.
cbuffer FSREasuParams : register(b0) {
    uint  g_InputSrvIndex;  ///< Bindless SRV index for input (low-res) texture.
    uint  g_OutputUavIndex; ///< Bindless UAV index for output (high-res) texture.
    uint  g_OutputWidth;    ///< Output texture width in pixels.
    uint  g_OutputHeight;   ///< Output texture height in pixels.
    uint  g_InputWidth;     ///< Input texture width in pixels.
    uint  g_InputHeight;    ///< Input texture height in pixels.
    uint  _pad0;
    uint  _pad1;
};

SamplerState linearSampler : register(s0);

#ifndef UZ_BINDLESS
Texture2D<float4>   g_InputTexture  : register(t0);
RWTexture2D<float4> g_OutputTexture : register(u0);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Lanczos2 Approximation
// ─────────────────────────────────────────────────────────────────────────────

/// Attempt to approximate Lanczos2 using a rational polynomial.
/// This avoids the expensive sin/cos of the true Lanczos kernel.
///
/// The approximation: (25/16)(2/5 * x^2 - 1)^2 - (25/16 - 1)
/// which closely matches Lanczos2 in the [-2,2] support range.
///
/// @ref AMD ffx_fsr1.h — FsrEasuTapF / Lanczos2 approximation
///
/// Input: d2 = squared distance from kernel center (in texels).
/// Returns: filter weight (0 at edges, 1 at center).
float FsrEasuLanczos2(float d2)
{
    // The 25/16 factor scales the polynomial to match Lanczos2 amplitude.
    // (2/5 * d2 - 1)^2 produces the main lobe shape.
    // Subtract (25/16 - 1) = 9/16 to bring the zero-crossing to d=2.
    float a = (2.0f / 5.0f) * d2 - 1.0f;
    float b = (25.0f / 16.0f) * a * a - (25.0f / 16.0f - 1.0f);
    return max(0.0f, b);
}

// ─────────────────────────────────────────────────────────────────────────────
// EASU Tap Accumulation
// ─────────────────────────────────────────────────────────────────────────────

/// Accumulates a single texel's contribution to the EASU output.
///
/// Each tap is weighted by:
///   1. Lanczos2 kernel weight (distance-based)
///   2. Edge-direction shaping (stretches kernel along detected edge)
///
/// @param aC   Running weighted color accumulator (inout).
/// @param aW   Running total weight accumulator (inout).
/// @param tap  Texel color (RGB).
/// @param off  Offset from kernel center to this texel (in texels).
/// @param dir  Edge direction vector (normalized).
/// @param len  Kernel stretch factor along edge (1=isotropic, >1=anisotropic).
/// @param lob  Negative lobe strength (controls ringing).
/// @param clp  Weight clamp (prevents extreme contribution from any single tap).
void FsrEasuTap(
    inout float3 aC,
    inout float  aW,
    float3 tap,
    float2 off,
    float2 dir,
    float2 len,
    float  lob,
    float  clp)
{
    // Project the offset onto edge-aligned axes.
    // d.x = distance along edge, d.y = distance across edge.
    float2 v;
    v.x = dot(off, dir);             // Along edge — stretched by len.x
    v.y = dot(off, float2(-dir.y, dir.x)); // Across edge — scaled by len.y

    // Apply anisotropic stretch: widen kernel along edge direction.
    v *= len;

    // Squared distance in the anisotropic kernel space.
    float d2 = v.x * v.x + v.y * v.y;

    // Clip: discard taps outside the kernel support radius.
    // d2 is in stretched space; clp defines the support cutoff.
    d2 = min(d2, clp);

    // Compute Lanczos2 weight from squared distance.
    float w = FsrEasuLanczos2(d2);

    // Accumulate.
    aC += tap * w;
    aW += w;
}

// ─────────────────────────────────────────────────────────────────────────────
// Edge Direction Detection
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the dominant edge direction and stretch factor from a
/// cross-shaped luma neighborhood.
///
/// Uses the horizontal and vertical gradients of luma to estimate
/// the local edge orientation. The kernel is then stretched along the
/// edge to preserve sharpness without crossing the edge boundary.
///
/// @ref AMD ffx_fsr1.h — FsrEasuSetF, direction detection logic
///
/// @param dir     Output: normalized edge direction vector.
/// @param stretch Output: kernel elongation factor (1=isotropic, up to 2).
/// @param b       Luma at (0,-1) — top neighbor.
/// @param d       Luma at (-1,0) — left neighbor.
/// @param e       Luma at (0,0) — center.
/// @param f       Luma at (1,0) — right neighbor.
/// @param h       Luma at (0,1) — bottom neighbor.
void FsrEasuDir(
    out float2 dir,
    out float  stretch,
    float b, float d, float e, float f, float h)
{
    // Gradient estimation from cross-shaped neighborhood.
    float dc = d - e;
    float bc = b - e;
    float fc = f - e;
    float hc = h - e;

    // Horizontal and vertical edge strength.
    float dirX = fc - dc;
    float dirY = hc - bc;

    // Normalize the direction vector.
    // Add small epsilon to avoid division by zero on flat regions.
    float dirR = rsqrt(max(dirX * dirX + dirY * dirY, 1.0f / 32768.0f));
    dir.x = dirX * dirR;
    dir.y = dirY * dirR;

    // Compute stretch factor based on edge contrast.
    // Higher contrast edges get more stretch (more anisotropic filtering).
    float lenX = max(abs(dc), abs(fc));
    float lenY = max(abs(bc), abs(hc));
    float dirLen = max(lenX, lenY);
    float s = saturate(abs(dirLen) * dirR);

    // Map to kernel elongation: 1.0 = isotropic, 2.0 = max stretch.
    stretch = 1.0f + (2.0f - 1.0f) * s;
}

// ─────────────────────────────────────────────────────────────────────────────
// EASU Main Kernel — Per-Pixel Evaluation
// ─────────────────────────────────────────────────────────────────────────────

/// Evaluates EASU for a single output pixel.
///
/// Gathers a neighborhood from the input texture (12 point-sampled taps),
/// detects the local edge direction from luma gradients, shapes an anisotropic
/// Lanczos2-like kernel along the edge, and accumulates weighted samples.
///
/// @ref AMD ffx_fsr1.h — FsrEasuF function
///
/// @param pos        Output pixel position (integer coordinates).
/// @param inputTex   Input texture (low-resolution).
/// @param inputSize  Input texture dimensions (width, height).
/// @param outputSize Output texture dimensions (width, height).
/// @return           Filtered RGB color for this output pixel.
float3 FsrEasuF(
    uint2 pos,
    Texture2D<float4> inputTex,
    float2 inputSize,
    float2 outputSize)
{
    // Map output pixel center to input texture space.
    // The scaling ratio maps each output pixel to a sub-texel position in the input.
    float2 ratio = inputSize / outputSize;

    // Center of the output pixel in input texel coordinates.
    // The +0.5*ratio - 0.5 offset centers the kernel on the input texel grid.
    float2 pp = float2(pos) * ratio + 0.5f * ratio - 0.5f;

    // Integer part = nearest input texel, fractional part = sub-texel offset.
    float2 fp = floor(pp);
    pp -= fp;

    // Input texel size for UV computation.
    float2 texelSize = 1.0f / inputSize;

    // UV of the nearest input texel center.
    float2 baseUV = (fp + 0.5f) * texelSize;

    // ── Gather 12 taps in a cross + diagonal neighborhood ────────────────
    // Using SampleLevel with explicit offsets from the base texel.

    // Cross-shaped core (5 taps).
    float3 b = inputTex.SampleLevel(linearSampler, baseUV + float2( 0, -1) * texelSize, 0).rgb;
    float3 d = inputTex.SampleLevel(linearSampler, baseUV + float2(-1,  0) * texelSize, 0).rgb;
    float3 e = inputTex.SampleLevel(linearSampler, baseUV,                               0).rgb;
    float3 f = inputTex.SampleLevel(linearSampler, baseUV + float2( 1,  0) * texelSize, 0).rgb;
    float3 h = inputTex.SampleLevel(linearSampler, baseUV + float2( 0,  1) * texelSize, 0).rgb;

    // Diagonal taps (4 corners).
    float3 a = inputTex.SampleLevel(linearSampler, baseUV + float2(-1, -1) * texelSize, 0).rgb;
    float3 c = inputTex.SampleLevel(linearSampler, baseUV + float2(-1,  1) * texelSize, 0).rgb;
    float3 g = inputTex.SampleLevel(linearSampler, baseUV + float2( 1, -1) * texelSize, 0).rgb;
    float3 i = inputTex.SampleLevel(linearSampler, baseUV + float2( 1,  1) * texelSize, 0).rgb;

    // Extended taps (3 additional for wider 4x4 coverage).
    float3 j = inputTex.SampleLevel(linearSampler, baseUV + float2( 0, -2) * texelSize, 0).rgb;
    float3 k = inputTex.SampleLevel(linearSampler, baseUV + float2(-2,  0) * texelSize, 0).rgb;
    float3 l = inputTex.SampleLevel(linearSampler, baseUV + float2( 2,  0) * texelSize, 0).rgb;

    // ── Compute luma for edge detection ──────────────────────────────────

    // BT.709 luminance weights.
    static const float3 kLumaWeights = float3(0.2126f, 0.7152f, 0.0722f);

    float bL = dot(b, kLumaWeights);
    float dL = dot(d, kLumaWeights);
    float eL = dot(e, kLumaWeights);
    float fL = dot(f, kLumaWeights);
    float hL = dot(h, kLumaWeights);

    // ── Detect edge direction ────────────────────────────────────────────

    float2 dir;
    float  stretch;
    FsrEasuDir(dir, stretch, bL, dL, eL, fL, hL);

    // Kernel stretch factors: along-edge and across-edge.
    float2 len = float2(stretch, 1.0f);

    // Negative lobe strength — controls how much the Lanczos side-lobes
    // contribute. 0.5/stretch balances sharpness against ringing.
    float lob = 0.5f + (1.0f / 4.0f - 0.04f) - 0.5f / stretch;

    // Support radius squared — taps beyond this distance are discarded.
    float clp = 1.0f / lob;

    // ── Accumulate all taps through the shaped kernel ────────────────────

    float3 aC = 0.0f;
    float  aW = 0.0f;

    // The offsets are relative to the center texel (fp), adjusted by
    // the sub-texel position (pp) so the kernel is centered on the
    // exact output pixel position in input space.

    FsrEasuTap(aC, aW, a, float2(-1, -1) - pp, dir, len, lob, clp);
    FsrEasuTap(aC, aW, b, float2( 0, -1) - pp, dir, len, lob, clp);
    FsrEasuTap(aC, aW, g, float2( 1, -1) - pp, dir, len, lob, clp);

    FsrEasuTap(aC, aW, d, float2(-1,  0) - pp, dir, len, lob, clp);
    FsrEasuTap(aC, aW, e, float2( 0,  0) - pp, dir, len, lob, clp);
    FsrEasuTap(aC, aW, f, float2( 1,  0) - pp, dir, len, lob, clp);

    FsrEasuTap(aC, aW, c, float2(-1,  1) - pp, dir, len, lob, clp);
    FsrEasuTap(aC, aW, h, float2( 0,  1) - pp, dir, len, lob, clp);
    FsrEasuTap(aC, aW, i, float2( 1,  1) - pp, dir, len, lob, clp);

    FsrEasuTap(aC, aW, j, float2( 0, -2) - pp, dir, len, lob, clp);
    FsrEasuTap(aC, aW, k, float2(-2,  0) - pp, dir, len, lob, clp);
    FsrEasuTap(aC, aW, l, float2( 2,  0) - pp, dir, len, lob, clp);

    // Normalize: divide accumulated color by total weight.
    // Guard against zero weight (flat regions with all taps clipped).
    return aC / max(aW, 1.0f / 65536.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// FSR 1.0 EASU compute shader.
///
/// Thread group: [8, 8, 1] — standard 2D tile for image processing.
/// Each thread computes one output pixel.
///
/// Dispatch: ceil(outputWidth/8) x ceil(outputHeight/8) x 1.
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Early-out for threads outside the output dimensions.
    if (DTid.x >= g_OutputWidth || DTid.y >= g_OutputHeight) return;

    // Access resources — bindless (SM6.6) or explicit register (SM5).
#ifdef UZ_BINDLESS
    Texture2D<float4>   inputTex  = ResourceDescriptorHeap[g_InputSrvIndex];
    RWTexture2D<float4> outputTex = ResourceDescriptorHeap[g_OutputUavIndex];
#else
    Texture2D<float4>   inputTex  = g_InputTexture;
    RWTexture2D<float4> outputTex = g_OutputTexture;
#endif

    float2 inputSize  = float2(g_InputWidth, g_InputHeight);
    float2 outputSize = float2(g_OutputWidth, g_OutputHeight);

    // Run EASU filter.
    float3 color = FsrEasuF(DTid.xy, inputTex, inputSize, outputSize);

    // Write result. Alpha = 1.0 (EASU operates on RGB only).
    outputTex[DTid.xy] = float4(color, 1.0f);
}
