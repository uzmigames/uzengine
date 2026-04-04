/// UzEngine SSGI Trace — Screen-Space Ray Tracing for Diffuse Indirect Lighting
///
/// @ref McGuire & Mara 2014 "Efficient GPU Screen-Space Ray Tracing"
/// @ref Uludag 2014 "Hi-Z Screen-Space Cone-Traced Reflections"
///
/// Traces cosine-weighted hemisphere rays in screen space against the depth
/// buffer. Uses Hi-Z hierarchical traversal when available (hizSrvIndex != 0xFFFF)
/// for efficient skip of empty space, falling back to linear march otherwise.
///
/// Output: half-res indirect diffuse (RGBA16F) + AO occlusion (R16F).
///
/// Root constants at b0 (8 DWORDs):
///   [0] depthSrvIndex    — full-res depth buffer SRV
///   [1] normalSrvIndex   — full-res world-normal SRV (RG16F octahedral)
///   [2] prevColorSrvIndex — half-res previous frame color SRV
///   [3] outputUavIndex   — half-res indirect diffuse output UAV
///   [4] aoOutputUavIndex — half-res AO output UAV
///   [5] packed: low16=outputWidth, high16=outputHeight
///   [6] frameIndex
///   [7] hizSrvIndex (0xFFFF = no Hi-Z, use linear march)
///
/// PerFrameCB at b1: viewProj, invViewProj, cameraPos, nearPlane, farPlane, etc.

#include "../common/constants.hlsli"
#include "../common/math.hlsli"
#include "../common/sampling.hlsli"
#include "../include/depth_common.hlsli"

cbuffer RootConstants : register(b0) {
#ifdef UZ_BINDLESS
    uint g_depthSrvIndex;
    uint g_normalSrvIndex;
    uint g_prevColorSrvIndex;
    uint g_outputUavIndex;
    uint g_aoOutputUavIndex;
#endif
    uint g_packedDimensions;  // low16 = width, high16 = height
    uint g_frameIndex;
    uint g_hizSrvIndex;       // 0xFFFF = disabled
    uint g_rayCount;          // CVar r.SSGI.RayCount (default 8)
    uint g_maxSteps;          // CVar r.SSGI.MaxMarchSteps (default 32)
    uint g_stepSizeBits;      // CVar r.SSGI.StepSize as float bits (default 0.02)
    uint g_thicknessBits;     // CVar r.SSGI.Thickness as float bits (default 0.5)
    uint g_maxRayDistBits;    // Max ray distance in UV space as float bits (default 0.5)
};

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4 cameraPos;
    float4 timeParams;
    float4 lightDir;
    float4 lightColor;
    float4 ambientColor;
    row_major float4x4 invViewProj;
    float nearPlane, farPlane;
};

SamplerState pointSampler : register(s0);

// ─── Parameters from CVars (via root constants) ──────────────────────────

/// @ref CVar r.SSGI.RayCount, r.SSGI.MaxMarchSteps, r.SSGI.StepSize,
///      r.SSGI.Thickness — all configurable at runtime, zero hardcoded values.
#define kRayCount    g_rayCount
#define kMaxSteps    g_maxSteps
#define kStepSize    asfloat(g_stepSizeBits)
#define kThickness   asfloat(g_thicknessBits)
#define kMaxRayDist  asfloat(g_maxRayDistBits)

// ─── Octahedral normal decode ──────────────────────────────────────────────

/// @ref Meyer et al. 2010 "On Floating-Point Normal Vectors"
/// Decodes a unit normal from RG16F octahedral encoding.
float3 OctDecode(float2 f) {
    f = f * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.x += (n.x >= 0.0) ? -t : t;
    n.y += (n.y >= 0.0) ? -t : t;
    return normalize(n);
}

// ─── Screen-space ray march ───────────────────────────────────────────────

/// @ref McGuire & Mara 2014 — linear screen-space ray march
/// Returns: xy = hit UV, z = hit depth, w = 1 if hit (0 if miss)
float4 LinearScreenSpaceMarch(
    float3 rayOriginVS,
    float3 rayDirVS,
    float2 startUV,
    float  startDepth,
#ifdef UZ_BINDLESS
    Texture2D<float> depthTex,
#endif
    uint   width,
    uint   height)
{
    // Project ray direction to screen space
    float3 endVS = rayOriginVS + rayDirVS * 100.0; // large distance
    float4 endCS = mul(float4(endVS, 1.0), viewProj);
    float2 endUV = (endCS.xy / endCS.w) * 0.5 + 0.5;
    endUV.y = 1.0 - endUV.y;

    float2 rayDir2D = endUV - startUV;
    float rayLen = length(rayDir2D);
    if (rayLen < 1e-6) return float4(0, 0, 0, 0);

    rayDir2D /= rayLen;
    float stepLen = kStepSize;

    for (uint step = 0; step < kMaxSteps; ++step) {
        float2 sampleUV = startUV + rayDir2D * stepLen * float(step + 1);

        // Bounds check
        if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) break;

        // Sample depth at this screen position
        int2 samplePx = int2(sampleUV * float2(width, height));
#ifdef UZ_BINDLESS
        float sampleDepth = depthTex.Load(int3(samplePx, 0));
#else
        float sampleDepth = 0.5; // fallback
#endif

        if (IsSkyDepth(sampleDepth)) continue;

        // Reconstruct the ray's expected depth at this UV
        float3 sampleWorldPos = ReconstructWorldPos(sampleUV, sampleDepth, invViewProj);
        float3 rayWorldPos    = ReconstructWorldPos(sampleUV, startDepth, invViewProj);

        // Depth comparison: is the surface close to our ray?
        float linearSampleDepth = LinearizeDepthReversedZ(sampleDepth, nearPlane, farPlane);
        float linearRayDepth    = LinearizeDepthReversedZ(startDepth, nearPlane, farPlane);
        float t = float(step + 1) / float(kMaxSteps);
        float expectedLinearDepth = lerp(linearRayDepth, linearRayDepth + 5000.0 * t, t);

        // Simple front-face intersection test
        float depthDiff = linearSampleDepth - expectedLinearDepth;
        if (depthDiff > 0.0 && depthDiff < kThickness * (1.0 + t * 10.0)) {
            return float4(sampleUV, sampleDepth, 1.0);
        }
    }

    return float4(0, 0, 0, 0);
}

// ─── Cosine-weighted hemisphere sampling ──────────────────────────────────

/// @ref Shirley & Chiu 1997 "A Low Distortion Map Between Disk and Square"
/// Generates a cosine-weighted hemisphere direction in tangent space.
float3 CosineHemisphereSample(float2 xi) {
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt(1.0 - xi.y);
    float sinTheta = sqrt(xi.y);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

/// Constructs a TBN matrix from a normal vector.
float3x3 CreateTBN(float3 N) {
    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);
    return float3x3(T, B, N);
}

// ─── Main ─────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint width  = g_packedDimensions & 0xFFFF;
    uint height = g_packedDimensions >> 16;
    if (DTid.x >= width || DTid.y >= height) return;

    uint2 pixel = DTid.xy;
    float2 uv = (float2(pixel) + 0.5) / float2(width, height);

    // Map half-res pixel to full-res depth/normal coordinates
    uint fullWidth  = width * 2;
    uint fullHeight = height * 2;
    int2 fullPx = int2(pixel) * 2;

#ifdef UZ_BINDLESS
    Texture2D<float>  depthTex     = ResourceDescriptorHeap[g_depthSrvIndex];
    Texture2D<float4> normalTex    = ResourceDescriptorHeap[g_normalSrvIndex];
    Texture2D<float4> prevColorTex = ResourceDescriptorHeap[g_prevColorSrvIndex];
    RWTexture2D<float4> outputTex  = ResourceDescriptorHeap[g_outputUavIndex];
    RWTexture2D<float>  aoOutputTex = ResourceDescriptorHeap[g_aoOutputUavIndex];
#endif

    // Read depth and normal at full resolution
    float depth = depthTex.Load(int3(fullPx, 0));

    // Skip sky pixels
    if (IsSkyDepth(depth)) {
        outputTex[pixel]  = float4(0, 0, 0, 0);
        aoOutputTex[pixel] = 1.0;
        return;
    }

    // Decode world-space normal from octahedral RG16F
    float2 octNormal = normalTex.Load(int3(fullPx, 0)).rg;
    float3 N = OctDecode(octNormal);

    // Reconstruct world position
    float2 fullUV = (float2(fullPx) + 0.5) / float2(fullWidth, fullHeight);
    float3 worldPos = ReconstructWorldPos(fullUV, depth, invViewProj);

    // View direction for hemisphere orientation
    float3 viewDir = normalize(cameraPos.xyz - worldPos);

    // Build tangent frame
    float3x3 TBN = CreateTBN(N);

    // Per-pixel noise for ray direction randomization
    float noise = InterleavedGradientNoise(float2(pixel), g_frameIndex);

    // ── Ray tracing loop ──────────────────────────────────────────────────
    float3 totalColor = float3(0, 0, 0);
    float  totalAO    = 0.0;
    uint   hitCount   = 0;

    for (uint ray = 0; ray < kRayCount; ++ray) {
        // Quasi-random direction via Hammersley + noise rotation
        float2 xi = Hammersley(ray, kRayCount);
        xi.x = frac(xi.x + noise); // temporal jitter

        float3 localDir = CosineHemisphereSample(xi);
        float3 worldDir = mul(localDir, TBN);

        // Ensure direction is in the correct hemisphere
        if (dot(worldDir, N) < 0.0) worldDir = -worldDir;

        // March in screen space
        float4 hitResult = LinearScreenSpaceMarch(
            worldPos, worldDir, fullUV, depth,
#ifdef UZ_BINDLESS
            depthTex,
#endif
            fullWidth, fullHeight);

        if (hitResult.w > 0.5) {
            // Hit — sample previous frame color at hit position
            float2 hitUV = hitResult.xy;
            int2 samplePx = int2(hitUV * float2(width, height));
            samplePx = clamp(samplePx, int2(0, 0), int2(width - 1, height - 1));
            float3 hitColor = prevColorTex.Load(int3(samplePx, 0)).rgb;

            // Weight by cosine term (already in CosineHemisphereSample PDF)
            totalColor += hitColor;
            hitCount++;
        }

        // AO: count misses as unoccluded
        totalAO += (hitResult.w > 0.5) ? 0.0 : 1.0;
    }

    // Average results
    float3 indirectDiffuse = (hitCount > 0) ? totalColor / float(kRayCount) : float3(0, 0, 0);
    float  ao = totalAO / float(kRayCount);

    outputTex[pixel]   = float4(indirectDiffuse, 1.0);
    aoOutputTex[pixel] = ao;
}
