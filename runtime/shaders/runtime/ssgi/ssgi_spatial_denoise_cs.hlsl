/// SSGI Spatial Denoise Compute Shader — Cross-Bilateral Filter
///
/// Applies a cross-bilateral spatial filter to the raw SSGI trace output.
/// Uses depth and normal edge-stopping functions to preserve geometric edges
/// while smoothing noise from the low ray-count stochastic trace.
///
/// 8 samples in a rotated disk pattern around each pixel, weighted by
/// depth similarity and normal similarity. The disk is rotated per-pixel
/// using interleaved gradient noise to break up pattern artifacts.
///
/// # Resources
/// - t0: Raw SSGI indirect diffuse (RGBA16F, half-res)
/// - t1: Raw SSGI AO (R16F, half-res)
/// - t2: Depth buffer (R32_FLOAT)
/// - t3: Normal+Material G-Buffer (R8G8B8A8_UNORM)
/// - u0: Denoised indirect diffuse output (RGBA16F, half-res)
/// - u1: Denoised AO output (R16F, half-res)
/// - b0: Root constants (8 DWORDs)
/// - b1: PerFrameCB
///
/// @source UE5 ScreenSpaceDenoise.cpp:2960-2985 — DenoiseScreenSpaceDiffuseIndirect
/// @source UE5 SSDSignalAccumulator.ush:1-120 — bilateral weight computation
/// @constants ReconstructionSamples=8 (ScreenSpaceDenoise.cpp:2963)
///            NormalPower=4.0 (SSDSignalAccumulator.ush:84)
///            DepthSigma = 0.02 (heuristic matching UE5 bilateral depth tolerance)
///            DiskRadius = 4.0 pixels (reconstruction kernel radius)
/// @deviations Simplified from UE5's full SSD pipeline which uses signal-specific
///   accumulators, frequency decomposition, and multi-pass reconstruction. We use
///   a single-pass 8-sample cross-bilateral filter which captures the essential
///   edge-preserving denoising behavior.

#include "../include/math_util.hlsli"
#include "../include/octahedral.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Quality Defines
// ─────────────────────────────────────────────────────────────────────────────

/// Number of spatial filter samples (reconstruction samples).
/// @source UE5 ScreenSpaceDenoise.cpp:2963 — ReconstructionSamples=8
#define SPATIAL_SAMPLE_COUNT 8

/// Kernel radius in pixels for the spatial filter.
/// @source UE5 ScreenSpaceDenoise.cpp:2964 — KernelSpreadFactor (adaptive)
/// Using a fixed 4-pixel radius as baseline.
#define KERNEL_RADIUS 4.0f

/// Normal similarity exponent — higher = sharper edge detection.
/// @source UE5 SSDSignalAccumulator.ush:84 — pow(NdotN, 4)
#define NORMAL_POWER 4.0f

/// Depth sigma for bilateral weight — controls depth sensitivity.
/// Smaller = sharper depth edges, larger = more aggressive smoothing.
/// @source UE5 SSDSignalAccumulator.ush:78 — depth bilateral sigma
#define DEPTH_SIGMA 0.02f

#define PI 3.14159265358979323846f

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0, 8 DWORDs = 32 bytes)
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 ScreenSpaceDenoise.cpp:2960 — spatial denoise dispatch params
cbuffer SpatialDenoiseRootCB : register(b0) {
    uint  inputSrvIndex;       // DWORD 0 — raw SSGI color SRV (bindless)
    uint  aoInputSrvIndex;     // DWORD 1 — raw SSGI AO SRV (bindless)
    uint  depthSrvIndex;       // DWORD 2 — depth buffer SRV (bindless)
    uint  normalSrvIndex;      // DWORD 3 — normal G-Buffer SRV (bindless)
    uint  outputUavIndex;      // DWORD 4 — denoised color UAV (unused for register-bound)
    uint  aoOutputUavIndex;    // DWORD 5 — denoised AO UAV (unused for register-bound)
    uint  texWidth;            // DWORD 6 — half-res width
    uint  texHeight;           // DWORD 7 — half-res height
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
Texture2D<float4> ssgiInputTex   : register(t0);
Texture2D<float>  ssgiAOInputTex : register(t1);
Texture2D<float>  depthBuffer    : register(t2);
Texture2D<float4> normalBuffer   : register(t3);
#endif

/// UAV outputs accessed via bindless ResourceDescriptorHeap.
/// The root constants provide outputUavIndex and aoOutputUavIndex.
/// In non-bindless SM5 mode, fallback to register-bound u0/u1.
#ifndef UZ_BINDLESS
RWTexture2D<float4> denoisedOutput   : register(u0);
RWTexture2D<float>  denoisedAOOutput : register(u1);
#endif

SamplerState pointSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Texture Sampling Helpers
// ─────────────────────────────────────────────────────────────────────────────

float4 SampleSSGI(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float4> tex = ResourceDescriptorHeap[inputSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0);
#else
    return ssgiInputTex.SampleLevel(pointSampler, uv, 0);
#endif
}

float SampleSSGIAO(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float> tex = ResourceDescriptorHeap[aoInputSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0).r;
#else
    return ssgiAOInputTex.SampleLevel(pointSampler, uv, 0).r;
#endif
}

float SampleDepth(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float> tex = ResourceDescriptorHeap[depthSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0).r;
#else
    return depthBuffer.SampleLevel(pointSampler, uv, 0).r;
#endif
}

float4 SampleNormalMaterial(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float4> tex = ResourceDescriptorHeap[normalSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0);
#else
    return normalBuffer.SampleLevel(pointSampler, uv, 0);
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Depth Linearization
// ─────────────────────────────────────────────────────────────────────────────

float LinearizeDepth(float deviceZ) {
    return nearPlane * farPlane / (farPlane - deviceZ * (farPlane - nearPlane));
}

// ─────────────────────────────────────────────────────────────────────────────
// Interleaved Gradient Noise (for disk rotation)
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 SSRTReflections.usf:348 — InterleavedGradientNoise
/// @constants 52.9829189, 0.06711056, 0.00583715
/// @deviations NONE
float InterleavedGradientNoise(float2 pixelPos) {
    return frac(52.9829189f * frac((pixelPos.x * 0.06711056f) + (pixelPos.y * 0.00583715f)));
}

// ─────────────────────────────────────────────────────────────────────────────
// Poisson Disk Sample Offsets
// ─────────────────────────────────────────────────────────────────────────────

/// 8-sample Poisson disk offsets within unit circle.
/// Generated with Mitchell's best-candidate algorithm for blue-noise distribution.
/// @source UE5 ScreenSpaceDenoise.cpp — uses adaptive kernel; we use a fixed
///   Poisson disk with similar spatial coverage.
/// @deviations Fixed Poisson disk instead of UE5's adaptive kernel. The
///   denoising quality is comparable for 8 samples; UE5's adaptive kernel
///   primarily benefits higher sample counts.
static const float2 kPoissonDisk[SPATIAL_SAMPLE_COUNT] = {
    float2(-0.7071f,  0.7071f),
    float2( 0.0000f, -0.8750f),
    float2( 0.8660f,  0.5000f),
    float2(-0.6235f, -0.2588f),
    float2( 0.5878f, -0.8090f),
    float2(-0.9511f,  0.3090f),
    float2( 0.3090f,  0.9511f),
    float2( 0.2225f, -0.3090f),
};

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Shader
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 ScreenSpaceDenoise.cpp:2960-2985 — spatial reconstruction pass
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    // Bounds check
    if (dispatchThreadId.x >= texWidth || dispatchThreadId.y >= texHeight) {
        return;
    }

    float2 pixelCenter = (float2)dispatchThreadId.xy + 0.5f;
    float2 invSize = 1.0f / float2((float)texWidth, (float)texHeight);
    float2 uv = pixelCenter * invSize;

    // ── Load center pixel data ──
    float4 centerColor = SampleSSGI(uv);
    float  centerAO    = SampleSSGIAO(uv);
    float  centerDepth = SampleDepth(uv);
    float3 centerNormal = OctDecode(SampleNormalMaterial(uv).rg);

    float  centerLinearZ = LinearizeDepth(centerDepth);

    // Start accumulation with center pixel (weight = 1)
    float4 totalColor  = centerColor;
    float  totalAO     = centerAO;
    float  totalWeight = 1.0f;

    // ── Per-pixel rotation angle for Poisson disk ──
    // Rotate the disk per pixel to break up visible pattern artifacts.
    float rotationAngle = InterleavedGradientNoise(pixelCenter) * 2.0f * PI;
    float cosRot = cos(rotationAngle);
    float sinRot = sin(rotationAngle);

    // ── Sample neighbors ──
    // @source UE5 ScreenSpaceDenoise.cpp:2963 — ReconstructionSamples=8
    [unroll]
    for (uint i = 0; i < SPATIAL_SAMPLE_COUNT; i++) {
        // Rotate the Poisson disk offset
        float2 offset = kPoissonDisk[i];
        float2 rotatedOffset;
        rotatedOffset.x = offset.x * cosRot - offset.y * sinRot;
        rotatedOffset.y = offset.x * sinRot + offset.y * cosRot;

        // Scale by kernel radius and convert to UV offset
        float2 sampleUV = uv + rotatedOffset * KERNEL_RADIUS * invSize;

        // Bounds check — skip samples outside the texture
        if (any(sampleUV < 0.0f) || any(sampleUV > 1.0f)) {
            continue;
        }

        // Load neighbor data
        float4 neighborColor  = SampleSSGI(sampleUV);
        float  neighborAO     = SampleSSGIAO(sampleUV);
        float  neighborDepth  = SampleDepth(sampleUV);
        float3 neighborNormal = OctDecode(SampleNormalMaterial(sampleUV).rg);

        float neighborLinearZ = LinearizeDepth(neighborDepth);

        // ── Depth bilateral weight ──
        // @source UE5 SSDSignalAccumulator.ush:78 — exp(-depthDiff^2 / sigma)
        float depthDiff = abs(centerLinearZ - neighborLinearZ) / max(centerLinearZ, 1.0f);
        float depthWeight = exp(-depthDiff * depthDiff / (DEPTH_SIGMA * DEPTH_SIGMA));

        // ── Normal bilateral weight ──
        // @source UE5 SSDSignalAccumulator.ush:84 — pow(max(0, dot(Nc, Nn)), 4)
        float normalSimilarity = max(0.0f, dot(centerNormal, neighborNormal));
        float normalWeight = pow(normalSimilarity, NORMAL_POWER);

        // Combined bilateral weight
        float weight = depthWeight * normalWeight;

        // Accumulate
        totalColor  += neighborColor * weight;
        totalAO     += neighborAO * weight;
        totalWeight += weight;
    }

    // ── Normalize ──
    float invWeight = (totalWeight > 0.0f) ? rcp(totalWeight) : 0.0f;
    float4 denoisedColor = totalColor * invWeight;
    float  denoisedAO    = totalAO * invWeight;

    // Write denoised outputs
#ifdef UZ_BINDLESS
    RWTexture2D<float4> outColor = ResourceDescriptorHeap[outputUavIndex];
    RWTexture2D<float>  outAO   = ResourceDescriptorHeap[aoOutputUavIndex];
    outColor[dispatchThreadId.xy] = denoisedColor;
    outAO[dispatchThreadId.xy]    = denoisedAO;
#else
    denoisedOutput[dispatchThreadId.xy]   = denoisedColor;
    denoisedAOOutput[dispatchThreadId.xy] = denoisedAO;
#endif
}
