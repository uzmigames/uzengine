/// SSGI Bilateral Upsample Compute Shader
///
/// Upsamples the half-resolution temporally-accumulated SSGI result to full
/// resolution using joint bilateral filtering. The full-res depth and normal
/// buffers guide the upsampling to preserve geometric edges that the half-res
/// SSGI trace cannot resolve.
///
/// For each full-res pixel, the shader finds the 4 nearest half-res texels
/// (2x2 bilinear neighborhood) and weights each by depth and normal similarity
/// with the full-res surface at that pixel. This prevents SSGI color from
/// bleeding across depth or normal discontinuities.
///
/// # Resources
/// - t0: Half-res SSGI color (RGBA16F) — temporally accumulated
/// - t1: Half-res SSGI AO (R16F) — temporally accumulated
/// - t2: Full-res depth buffer (R32_FLOAT)
/// - t3: Full-res normal G-Buffer (R8G8B8A8_UNORM)
/// - u0: Full-res SSGI color output (RGBA16F)
/// - u1: Full-res SSGI AO output (R16F)
/// - b0: Root constants (8 DWORDs)
/// - b1: PerFrameCB
///
/// @ref Jimenez 2016 "Screen-Space Indirect Lighting" — indirect composite with bilateral upsample
/// @ref Tomasi & Manduchi 1998 "Bilateral Filtering" — joint bilateral upsampling
/// @constants DepthSigma=0.01 (bilateral depth tolerance, relative)
///            NormalPower=2.0 (normal similarity exponent)
/// @deviations Upsampling is a separate pass for clarity. The bilateral
///   weighting logic follows the standard joint bilateral approach.

#include "../include/math_util.hlsli"
#include "../include/octahedral.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Depth bilateral sigma (relative). Controls depth sensitivity.
#define DEPTH_SIGMA 0.01f

/// Normal similarity exponent for bilateral weight.
#define NORMAL_POWER 2.0f

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0, 8 DWORDs = 32 bytes)
// ─────────────────────────────────────────────────────────────────────────────

/// Upsample dispatch parameters.
cbuffer UpsampleRootCB : register(b0) {
    uint  ssgiHalfSrvIndex;    // DWORD 0 — half-res SSGI color SRV
    uint  ssgiAOHalfSrvIndex;  // DWORD 1 — half-res SSGI AO SRV
    uint  depthFullSrvIndex;   // DWORD 2 — full-res depth SRV
    uint  normalFullSrvIndex;  // DWORD 3 — full-res normal SRV
    uint  outputUavIndex;      // DWORD 4 — (unused for register-bound)
    uint  aoOutputUavIndex;    // DWORD 5 — (unused for register-bound)
    uint  fullWidth;           // DWORD 6 — full-res output width
    uint  fullHeight;          // DWORD 7 — full-res output height
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-Frame CB (b1)
// ─────────────────────────────────────────────────────────────────────────────

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;
    float4   timeParams;
    float4   lightDir;
    float4   lightColor;
    float4   ambientColor;
    row_major float4x4 invViewProj;
    float    nearPlane;
    float    farPlane;
    float    _pad_pf[14];
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
// SM6.6: textures accessed via ResourceDescriptorHeap[]
#else
Texture2D<float4> ssgiHalfColor  : register(t0);
Texture2D<float>  ssgiHalfAO     : register(t1);
Texture2D<float>  depthFullTex   : register(t2);
Texture2D<float4> normalFullTex  : register(t3);
#endif

#ifndef UZ_BINDLESS
RWTexture2D<float4> fullResOutput   : register(u0);
RWTexture2D<float>  fullResAOOutput : register(u1);
#endif

SamplerState pointSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Texture Sampling Helpers
// ─────────────────────────────────────────────────────────────────────────────

float4 SampleSSGIHalf(int2 coord, uint halfWidth, uint halfHeight) {
    // Clamp to texture bounds
    coord = clamp(coord, int2(0, 0), int2((int)halfWidth - 1, (int)halfHeight - 1));
    float2 uv = ((float2)coord + 0.5f) / float2((float)halfWidth, (float)halfHeight);
#ifdef UZ_BINDLESS
    Texture2D<float4> tex = ResourceDescriptorHeap[ssgiHalfSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0);
#else
    return ssgiHalfColor.SampleLevel(pointSampler, uv, 0);
#endif
}

float SampleSSGIAOHalf(int2 coord, uint halfWidth, uint halfHeight) {
    coord = clamp(coord, int2(0, 0), int2((int)halfWidth - 1, (int)halfHeight - 1));
    float2 uv = ((float2)coord + 0.5f) / float2((float)halfWidth, (float)halfHeight);
#ifdef UZ_BINDLESS
    Texture2D<float> tex = ResourceDescriptorHeap[ssgiAOHalfSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0).r;
#else
    return ssgiHalfAO.SampleLevel(pointSampler, uv, 0).r;
#endif
}

float SampleDepthFull(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float> tex = ResourceDescriptorHeap[depthFullSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0).r;
#else
    return depthFullTex.SampleLevel(pointSampler, uv, 0).r;
#endif
}

float4 SampleNormalFull(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float4> tex = ResourceDescriptorHeap[normalFullSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0);
#else
    return normalFullTex.SampleLevel(pointSampler, uv, 0);
#endif
}

/// Load depth from the half-res depth buffer at a half-res texel coordinate.
/// Since we don't have a separate half-res depth texture, we sample the full-res
/// depth at the center of the corresponding full-res 2x2 block.
float SampleDepthAtHalfRes(int2 halfCoord, uint halfWidth, uint halfHeight) {
    // Map half-res coord to full-res UV (center of the 2x2 block)
    float2 fullUV = ((float2)(halfCoord * 2) + 1.0f) / float2((float)fullWidth, (float)fullHeight);
    return SampleDepthFull(fullUV);
}

/// Load normal from the full-res buffer at the center of a half-res texel's 2x2 block.
float3 SampleNormalAtHalfRes(int2 halfCoord, uint halfWidth, uint halfHeight) {
    float2 fullUV = ((float2)(halfCoord * 2) + 1.0f) / float2((float)fullWidth, (float)fullHeight);
    return OctDecode(SampleNormalFull(fullUV).rg);
}

// ─────────────────────────────────────────────────────────────────────────────
// Depth Linearization
// ─────────────────────────────────────────────────────────────────────────────

/// Reversed-Z: deviceZ=1 at near, deviceZ=0 at far.
float LinearizeDepth(float deviceZ) {
    return nearPlane * farPlane / (nearPlane + deviceZ * (farPlane - nearPlane));
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Shader
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Jimenez 2016 — bilateral upsample from half-res to full-res
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    // Bounds check — dispatch covers full-res output
    if (dispatchThreadId.x >= fullWidth || dispatchThreadId.y >= fullHeight) {
        return;
    }

    float2 fullPixelCenter = (float2)dispatchThreadId.xy + 0.5f;
    float2 fullUV = fullPixelCenter / float2((float)fullWidth, (float)fullHeight);

    // ── Load full-res surface properties ──
    float  fullDepth  = SampleDepthFull(fullUV);
    float3 fullNormal = OctDecode(SampleNormalFull(fullUV).rg);
    float  fullLinearZ = LinearizeDepth(fullDepth);

    // Half-res dimensions
    uint halfWidth  = (fullWidth  + 1) / 2;
    uint halfHeight = (fullHeight + 1) / 2;

    // ── Find the 4 nearest half-res texels ──
    // Map full-res pixel to half-res coordinate space
    float2 halfCoordF = fullPixelCenter * 0.5f - 0.5f;
    int2   halfCoord00 = int2(floor(halfCoordF));

    // Bilinear interpolation weights within the half-res texel
    float2 bilinearWeights = frac(halfCoordF);

    // The 4 neighbors in the 2x2 bilinear grid
    int2 offsets[4] = {
        int2(0, 0),
        int2(1, 0),
        int2(0, 1),
        int2(1, 1),
    };

    // Standard bilinear weights
    float bilinearW[4] = {
        (1.0f - bilinearWeights.x) * (1.0f - bilinearWeights.y),
        bilinearWeights.x * (1.0f - bilinearWeights.y),
        (1.0f - bilinearWeights.x) * bilinearWeights.y,
        bilinearWeights.x * bilinearWeights.y,
    };

    // ── Accumulate with bilateral weights ──
    float4 totalColor  = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float  totalAO     = 0.0f;
    float  totalWeight = 0.0f;

    [unroll]
    for (uint i = 0; i < 4; i++) {
        int2 sampleCoord = halfCoord00 + offsets[i];

        // Load half-res SSGI data
        float4 halfColor = SampleSSGIHalf(sampleCoord, halfWidth, halfHeight);
        float  halfAO    = SampleSSGIAOHalf(sampleCoord, halfWidth, halfHeight);

        // Load surface properties at the half-res texel's corresponding full-res position
        float  halfDepth  = SampleDepthAtHalfRes(sampleCoord, halfWidth, halfHeight);
        float3 halfNormal = SampleNormalAtHalfRes(sampleCoord, halfWidth, halfHeight);
        float  halfLinearZ = LinearizeDepth(halfDepth);

        // ── Depth bilateral weight ──
        float depthDiff = abs(fullLinearZ - halfLinearZ) / max(fullLinearZ, 1.0f);
        float depthWeight = exp(-depthDiff * depthDiff / (DEPTH_SIGMA * DEPTH_SIGMA));

        // ── Normal bilateral weight ──
        float normalSimilarity = max(0.0f, dot(fullNormal, halfNormal));
        float normalWeight = pow(normalSimilarity, NORMAL_POWER);

        // Combined weight: bilinear * depth * normal
        float weight = bilinearW[i] * depthWeight * normalWeight;

        totalColor  += halfColor * weight;
        totalAO     += halfAO * weight;
        totalWeight += weight;
    }

    // ── Normalize and output ──
    // Fallback: if all weights are near zero (edge case at extreme depth discontinuities),
    // use the nearest half-res texel directly to avoid black pixels.
    float4 resultColor;
    float  resultAO;

    if (totalWeight > 1e-6f) {
        float invWeight = rcp(totalWeight);
        resultColor = totalColor * invWeight;
        resultAO    = totalAO * invWeight;
    } else {
        // Fallback to nearest half-res texel
        int2 nearestCoord = int2(round(halfCoordF));
        resultColor = SampleSSGIHalf(nearestCoord, halfWidth, halfHeight);
        resultAO    = SampleSSGIAOHalf(nearestCoord, halfWidth, halfHeight);
    }

    // Write full-res outputs
#ifdef UZ_BINDLESS
    RWTexture2D<float4> outColor = ResourceDescriptorHeap[outputUavIndex];
    RWTexture2D<float>  outAO   = ResourceDescriptorHeap[aoOutputUavIndex];
    outColor[dispatchThreadId.xy] = resultColor;
    outAO[dispatchThreadId.xy]    = resultAO;
#else
    fullResOutput[dispatchThreadId.xy]   = resultColor;
    fullResAOOutput[dispatchThreadId.xy] = resultAO;
#endif
}
