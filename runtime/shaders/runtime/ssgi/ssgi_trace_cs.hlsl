/// Half-resolution diffuse trace: surface world gather or legacy screen-space tracing.
/// @ref Wright et al. 2022 "Lumen", final gather backed by a world radiance cache.
/// @ref McGuire & Mara 2014 "Efficient GPU Screen-Space Ray Tracing".
/// World mode reads current GBuffer geometry and produces E/pi; it never samples previous HDR.
/// Legacy mode retains linear screen-space tracing against depth and previous HDR.
/// Root constants contain 13 DWORDs; shared PerFrameCB supplies current SDF/cache bindings.
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

#include "ssgi_surface.hlsli"
#ifdef UZ_BINDLESS
#include "ssgi_world_gather.hlsli"
#endif

SamplerState pointSampler : register(s0);

// ─── Parameters from CVars (via root constants) ──────────────────────────

/// @ref CVar r.SSGI.RayCount, r.SSGI.MaxMarchSteps, r.SSGI.StepSize,
///      r.SSGI.Thickness — all configurable at runtime, zero hardcoded values.
#define kRayCount    g_rayCount
#define kMaxSteps    g_maxSteps
#define kStepSize    asfloat(g_stepSizeBits)
#define kThickness   asfloat(g_thicknessBits)
#define kMaxRayDist  asfloat(g_maxRayDistBits)

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
    uint fullWidth;
    uint fullHeight;
    int2 fullPx = int2(pixel) * 2;

#ifdef UZ_BINDLESS
    Texture2D<float>  depthTex     = ResourceDescriptorHeap[g_depthSrvIndex];
    Texture2D<float4> normalTex    = ResourceDescriptorHeap[g_normalSrvIndex];
    Texture2D<float4> prevColorTex = ResourceDescriptorHeap[g_prevColorSrvIndex];
    RWTexture2D<float4> outputTex  = ResourceDescriptorHeap[g_outputUavIndex];
    RWTexture2D<float>  aoOutputTex = ResourceDescriptorHeap[g_aoOutputUavIndex];
#endif

    depthTex.GetDimensions(fullWidth, fullHeight);
    fullPx = SurfacePixel(int2(pixel), uint2(fullWidth, fullHeight));
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

#ifdef UZ_BINDLESS
    if (sdfDiffuseSamples != 0) {
        float3 geometricNormal = SurfaceGeometricNormal(depthTex,fullPx,uint2(fullWidth,fullHeight),depth,worldPos);
        float3 origin, firstDirection;
        float4 rays[UZ_GATHER_MAX_REPORTED_RAYS];
        float2 rotationUsed;
        uint seed, reportedRays;
        bool resolved;
        float4 cascadeStats, clipmap0;
        float3 gathered = GatherSurfaceLightingIdentified(worldPos,N,geometricNormal,uint2(fullPx),
                                                          g_frameIndex,origin,seed,firstDirection,resolved,
                                                          rays,reportedRays,rotationUsed,cascadeStats,clipmap0);
        outputTex[pixel] = float4(gathered,1.0);
        aoOutputTex[pixel] = 1.0;
        // Region dump (diagnostics): what was sampled and what was read, see common/region_dump.hlsli.
        // Guarded so a normal frame neither writes records nor traces the extra probe ray.
        if (UzGatherDumpActive()) {
            const uint2 fullSize = uint2(fullWidth, fullHeight);
            UzGatherDumpHalf(pixel, fullSize, 9u, float4(worldPos, depth));
            UzGatherDumpHalf(pixel, fullSize, 10u, float4(origin, resolved ? 1.0 : 0.0));
            UzGatherDumpHalf(pixel, fullSize, 11u, float4(geometricNormal, asfloat(seed)));
            UzGatherDumpHalf(pixel, fullSize, 12u, float4(firstDirection, float(sdfDiffuseSamples)));
            UzGatherDumpHalf(pixel, fullSize, 13u, float4(asfloat(g_frameIndex), asfloat(sdfClipmapSrv.x),
                                                          asfloat(sdfClipmapSrv.y), asfloat(sdfClipmapSrv.z)));
            UzGatherDumpHalf(pixel, fullSize, 14u, float4(asfloat(sdfClipmapSrv.w), asfloat(rcProbeSrv),
                                                          asfloat(sdfSkySrv), asfloat(sdfRawClipmapSrv.x)));
            float3 snapped;
            bool probeHit;
            const float cell = max(sdfVoxelSize, 1.0) * 8.0;
            float3 probe = ProbeSurfaceLightingSnapped(worldPos, cell, snapped, probeHit);
            UzGatherDumpHalf(pixel, fullSize, 15u, float4(snapped, cell));
            UzGatherDumpHalf(pixel, fullSize, 16u, float4(probe, 1.0));
            UzGatherDumpHalf(pixel, fullSize, 18u,
                             float4(ProbeVolumeAtSnapped(snapped, float3(0,1,0)), probeHit ? 1.0 : 0.0));
            [unroll] for (uint rr = 0u; rr < UZ_GATHER_MAX_REPORTED_RAYS; ++rr) {
                UzGatherDumpHalf(pixel, fullSize, 19u + rr, rays[rr]);
            }
            UzGatherDumpHalf(pixel, fullSize, 27u, float4(gathered, float(reportedRays)));
            UzGatherDumpHalf(pixel, fullSize, 28u,
                             float4(rotationUsed, asfloat(sdfSampleRotation), 0.0));
            // +29 where the rays ended with respect to clipmap 0; +30 clipmap 0 as this shader saw it.
            UzGatherDumpHalf(pixel, fullSize, 29u, cascadeStats);
            UzGatherDumpHalf(pixel, fullSize, 30u, clipmap0);
        }
        return;
    }
#endif
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
