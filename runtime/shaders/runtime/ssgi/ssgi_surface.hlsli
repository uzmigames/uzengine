/// @ref Wright et al. 2022 "Lumen", final gather interpolation and depth/normal history rejection.
/// @ref Cigolle et al. 2014 "A Survey of Efficient Representations for Independent Unit Vectors".
#ifndef UZ_SSGI_SURFACE_HLSLI
#define UZ_SSGI_SURFACE_HLSLI
#include "../common/encoding.hlsli"
#include "../include/depth_common.hlsli"
#include "../common/region_dump.hlsli"
#include "ssgi_sample_validity.hlsli"
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj : packoffset(c0);
    float4 cameraPos : packoffset(c4);
    float4 timeParams : packoffset(c5);
    float4 lightDir : packoffset(c6);
    float4 lightColor : packoffset(c7);
    float4 ambientColor : packoffset(c8);
    row_major float4x4 invViewProj : packoffset(c9);
    float nearPlane : packoffset(c13.x);
    float farPlane : packoffset(c13.y);
    float skyLightIntensity : packoffset(c14.z);
    float skyLightColorR : packoffset(c14.w);
    float skyLightColorG : packoffset(c15.x);
    float skyLightColorB : packoffset(c15.y);
    uint rcProbeSrv : packoffset(c31.y);
    uint rcProbesPerAxis : packoffset(c31.z);
    float rcExtent : packoffset(c31.w);
    float3 rcOrigin : packoffset(c32.x);
    float rcIntensity : packoffset(c32.w);
    uint sdfSkySrv : packoffset(c53.z);
    float4 sdfClipmaps[4] : packoffset(c54);
    uint4 sdfClipmapSrv : packoffset(c58);
    float sdfTraceDistance : packoffset(c59.x);
    float sdfVoxelSize : packoffset(c59.y);
    uint sdfTraceSteps : packoffset(c59.z);
    uint sdfDiffuseSamples : packoffset(c60.x);
    uint sdfSampleRotation : packoffset(c60.z);
    uint4 sdfRawClipmapSrv : packoffset(c61);
    // Region dump (diagnostics) — the same fields the lighting PS reads, at their PerFrameCB
    // offsets 992..1008. Both SSGI passes already bind this buffer, so the gather needs no root
    // constants of its own to write its records.
    uint _debugRegionUav : packoffset(c62.x);
    uint _debugRegionSrv : packoffset(c62.y);
    uint _debugRegionCount : packoffset(c62.z);
    uint _debugRegionStamp : packoffset(c62.w);
    uint _debugRegionFields : packoffset(c63.x);
};

bool RejectUnavailableGatherSamples() {
    return sdfDiffuseSamples != 0u && (sdfSampleRotation & 0x80000000u) != 0u;
}

/// True when this frame is capturing the gather group.
bool UzGatherDumpActive() {
    return _debugRegionUav != 0xffffffffu && _debugRegionCount != 0u
        && (_debugRegionFields & UZ_REGION_FIELD_GATHER) != 0u;
}

/// Writes one GATHER record for a half-resolution gather pixel.
///
/// The rectangles are full-resolution pixels and this runs at half, so gather pixel `g` writes the
/// four full pixels `2g + {0,1}^2` — exactly the set for which `g` is tap (0,0) of the bilateral
/// reconstruction. Both the trace and the accumulator write through this, so their records land on
/// the same pixels and can be read side by side.
void UzGatherDumpHalf(uint2 gatherPixel, uint2 fullSize, uint slot, float4 value) {
    if (!UzGatherDumpActive()) return;
    StructuredBuffer<uint4>    rects = ResourceDescriptorHeap[_debugRegionSrv];
    RWStructuredBuffer<float4> dump  = ResourceDescriptorHeap[_debugRegionUav];
    for (uint r = 0u; r < _debugRegionCount; ++r) {
        const uint4 a = rects[r * 2u];
        const uint4 b = rects[r * 2u + 1u];
        for (uint q = 0u; q < 4u; ++q) {
            const uint2 fp = gatherPixel * 2u + uint2(q & 1u, q >> 1u);
            if (any(fp >= fullSize) || !UzRegionContains(a, fp)) continue;
            dump[UzRegionPixelBase(a, b, fp) + UzRegionGatherOffset(_debugRegionFields) + slot] = value;
        }
    }
}
/// @constants The half-resolution lattice represents full pixel 2*p. Pixel centers add 0.5.
int2 SurfacePixel(int2 halfPixel, uint2 fullSize) { return min(halfPixel * 2, int2(fullSize) - 1); }
float3 SurfacePosition(int2 pixel, float depth, uint2 fullSize) {
    return ReconstructWorldPos((float2(pixel) + 0.5) / float2(fullSize), depth, invViewProj);
}
/// @constants Two 10-bit UNORM coordinates fit exactly in float32's 24-bit mantissa; -1 marks sky.
float PackSurfaceNormal(float3 normal) {
    uint2 oct = uint2(round(saturate(OctEncode(normal)) * 1023.0));
    return float(oct.x | (oct.y << 10));
}
float3 UnpackSurfaceNormal(float packed) {
    uint bits = uint(packed);
    return OctDecode(float2(bits & 1023u, bits >> 10) / 1023.0);
}
/// @constants A 1 cm floor handles reconstruction roundoff; 0.1% camera distance scales plane tolerance.
float SurfacePlaneTolerance(float3 position) { return max(1.0, length(position - cameraPos.xyz) * 0.001); }
/// @constants Normal agreement of at least 0.9 and the receiver-plane tolerance reject disconnected surfaces.
bool SurfaceCompatible(float3 center, float3 normal, float3 samplePosition, float3 sampleNormal) {
    float3 delta = samplePosition - center;
    float planeDistance = max(abs(dot(delta,normal)),abs(dot(delta,sampleNormal)));
    return dot(normal,sampleNormal) >= 0.9 && planeDistance <= SurfacePlaneTolerance(center);
}
/// @constants Symmetric plane distance rejects parallel disconnected surfaces; normal cosine power is caller-controlled.
float SurfaceWeight(float3 center, float3 normal, float3 samplePosition, float3 sampleNormal, float normalPower) {
    float3 delta = samplePosition - center;
    float planeDistance = max(abs(dot(delta, normal)), abs(dot(delta, sampleNormal)));
    return exp(-planeDistance / SurfacePlaneTolerance(center)) * pow(saturate(dot(normal, sampleNormal)), normalPower);
}
/// @ref Jimenez et al. 2016 "Practical Realtime Strategies for Accurate Indirect Occlusion", nearest-depth derivatives.
/// @constants Raw raster depth is affine on a triangle: 2*d1-d2 predicts d0.
/// Relative residual 1e-3 plus 1e-7 absolute rejects discontinuities while allowing depth quantization.
bool SurfaceDerivativeSide(Texture2D<float> depthTex, int2 pixel, uint2 fullSize,
    float depth, float3 position, int2 step, out float3 derivative, out float depthDelta) {
    derivative = 0.0;
    depthDelta = 0.0;
    int2 first = pixel + step;
    int2 second = pixel + step * 2;
    if (any(first < 0) || any(first >= int2(fullSize)) || any(second < 0) || any(second >= int2(fullSize))) return false;
    float firstDepth = depthTex.Load(int3(first,0));
    float secondDepth = depthTex.Load(int3(second,0));
    if (IsSkyDepth(firstDepth) || IsSkyDepth(secondDepth)) return false;
    float residual = abs(2.0 * firstDepth - secondDepth - depth);
    if (residual > max(depth,max(firstDepth,secondDepth)) * 1e-3 + 1e-7) return false;
    derivative = (SurfacePosition(first,firstDepth,fullSize) - position) * float(step.x + step.y);
    depthDelta = abs(firstDepth - depth);
    return all(isfinite(derivative));
}
bool SurfaceDerivative(Texture2D<float> depthTex, int2 pixel, uint2 fullSize,
    float depth, float3 position, int2 axis, out float3 derivative) {
    float3 negative, positive;
    float negativeDelta, positiveDelta;
    bool hasNegative = SurfaceDerivativeSide(depthTex,pixel,fullSize,depth,position,-axis,negative,negativeDelta);
    bool hasPositive = SurfaceDerivativeSide(depthTex,pixel,fullSize,depth,position,axis,positive,positiveDelta);
    derivative = 0.0;
    if (!hasNegative && !hasPositive) return false;
    derivative = hasNegative && (!hasPositive || negativeDelta <= positiveDelta) ? negative : positive;
    return true;
}
/// @ref Pharr, Jakob and Humphreys 2023 "Physically Based Rendering", geometric versus shading normals.
/// @constants One-pixel axes, squared area 1e-12 degeneracy threshold; no valid geometry returns zero.
float3 SurfaceGeometricNormal(Texture2D<float> depthTex, int2 pixel, uint2 fullSize, float depth, float3 position) {
    float3 dx, dy;
    if (!SurfaceDerivative(depthTex,pixel,fullSize,depth,position,int2(1,0),dx)
        || !SurfaceDerivative(depthTex,pixel,fullSize,depth,position,int2(0,1),dy)) return 0.0;
    float3 normal = cross(dx,dy);
    float area = dot(normal,normal);
    if (!isfinite(area) || area <= 1e-12) return 0.0;
    normal *= rsqrt(area);
    return dot(normal,cameraPos.xyz-position) < 0.0 ? -normal : normal;
}

// A two-pixel extrapolation can cross a triangle edge even when its first neighbor is
// on the receiver. Retry the four one-pixel triangles, accepting only a finite,
// camera-facing plane agreeing with the GBuffer normal and both receiver-plane tests.
// The original estimator and every successfully reconstructed normal stay unchanged.
float3 SurfaceGatherGeometricNormal(Texture2D<float> depthTex, int2 pixel, uint2 fullSize,
                                   float depth, float3 position, float3 shadingNormal) {
    float3 normal = SurfaceGeometricNormal(depthTex,pixel,fullSize,depth,position);
    if (!RejectUnavailableGatherSamples() || dot(normal,normal) > 0.0) return normal;
    float bestAgreement = 0.9;
    for (int y = -1; y <= 1; y += 2) for (int x = -1; x <= 1; x += 2) {
        int2 px = pixel + int2(x,0), py = pixel + int2(0,y);
        if (any(px < 0) || any(py < 0) || any(px >= int2(fullSize)) || any(py >= int2(fullSize))) continue;
        float zx = depthTex.Load(int3(px,0)), zy = depthTex.Load(int3(py,0));
        if (IsSkyDepth(zx) || IsSkyDepth(zy)) continue;
        float3 dx = SurfacePosition(px,zx,fullSize) - position;
        float3 dy = SurfacePosition(py,zy,fullSize) - position;
        float tolerance = SurfacePlaneTolerance(position);
        if (max(abs(dot(dx,shadingNormal)),abs(dot(dy,shadingNormal))) > tolerance) continue;
        float3 candidate = cross(dx * float(x),dy * float(y));
        float area = dot(candidate,candidate);
        if (!isfinite(area) || area <= 1e-12) continue;
        candidate *= rsqrt(area);
        if (dot(candidate,cameraPos.xyz-position) < 0.0) candidate = -candidate;
        float agreement = dot(candidate,shadingNormal);
        if (agreement >= bestAgreement) { normal = candidate; bestAgreement = agreement; }
    }
    return normal;
}
#endif
