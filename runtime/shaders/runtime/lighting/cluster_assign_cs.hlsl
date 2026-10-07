/// UzEngine light cull — assigns the frame's point and spot lights to the clusters of the view frustum
/// (lighting block B.7, 2026-10-07; this file was a one-line stub since the clustered-forward-plus task).
///
/// The cluster grid is `clusterTilesX x clusterTilesY` screen tiles of `clusterTileSize` pixels by
/// `clusterSlices` logarithmic depth slices (LightingCB, constant_buffers.h). One thread per cluster:
/// the cluster's eight corners are the camera rays through the tile's corners (one pixel of margin:
/// the TAA jitter moves the frustum by half a pixel) at the slice's two depths, their world-space
/// AABB is the conservative cell, and every light whose range sphere touches the AABB is appended to
/// the cluster's record: points first, then spots, each in index order, so the lighting sums them in
/// the order the per-pixel loop did. A spot light is tested with its range sphere (no cone test yet).
/// More lights than slots sets the overflow bit; the lighting then evaluates every light.
///
/// Dispatch X = ceil(clusters / 64).
///
/// @ref Olsson, Billeter, Assarsson 2012 "Clustered Deferred and Forward Shading"
/// @ref UE5 LightGridInjection.usf — logarithmic slices, conservative sphere test

#include "../common/constants.hlsli"
#include "light_types.hlsli"

// The leading fields of PerFrameCB (constant_buffers.h); the rest is not needed here.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;    // c0-c3
    float4 cameraPos;               // c4
    float4 timeParams;              // c5
    float4 lightDir;                // c6
    float4 lightColor;              // c7
    float4 ambientColor;            // c8
    row_major float4x4 invViewProj; // c9-c12
    float  nearPlane;               // c13.x
    float  farPlane;                // c13.y
};

/// The camera ray through an NDC point: two unprojected points of the line (any NDC depth in (0, 1)
/// is in front of the camera under either depth convention), ordered by view depth. `cameraPos` is
/// not the ray's origin: the first version built rays as `unproject(ndc, 0.5) - cameraPos`, and a
/// centimetre between that constant and the projection's eye bends a ray reconstructed from a point
/// ten centimetres away by ten per cent, which displaced every cell towards the image edges (measured
/// against a CPU replica: boxes right at the centre, 11 % too far out at the sides).
void CameraRay(float2 ndc, float3 forward, out float3 origin, out float3 dir)
{
    float4 a = mul(float4(ndc, 0.25, 1.0), invViewProj);
    float4 b = mul(float4(ndc, 0.75, 1.0), invViewProj);
    a.xyz /= a.w;
    b.xyz /= b.w;
    const float da = dot(a.xyz - cameraPos.xyz, forward);
    const float db = dot(b.xyz - cameraPos.xyz, forward);
    origin = (da < db) ? a.xyz : b.xyz;
    dir    = normalize((da < db) ? (b.xyz - a.xyz) : (a.xyz - b.xyz));
}

float SliceDepth(uint s)
{
    if (s == 0u) return 0.0;
    if (s >= clusterSlices) return max(clusterFar, farPlane);
    return clusterNear * exp(float(s) / clusterLogScale);
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint clusterCount = clusterTilesX * clusterTilesY * clusterSlices;
    const uint c = DTid.x;
    if (c >= clusterCount) return;
    if (clusterListUav == kSentinelSrvIndex) return;

    const uint tx = c % clusterTilesX;
    const uint ty = (c / clusterTilesX) % clusterTilesY;
    const uint s  = c / (clusterTilesX * clusterTilesY);

    // Tile bounds in pixels with one pixel of margin, to NDC (y up).
    const float2 view = float2(clusterViewWidth, clusterViewHeight);
    const float2 p0 = float2(float(tx * clusterTileSize), float(ty * clusterTileSize)) - 1.0;
    const float2 p1 = p0 + float(clusterTileSize) + 2.0;
    const float nx0 = p0.x / view.x * 2.0 - 1.0;
    const float nx1 = p1.x / view.x * 2.0 - 1.0;
    const float ny0 = 1.0 - p1.y / view.y * 2.0;
    const float ny1 = 1.0 - p0.y / view.y * 2.0;
    const float d0 = SliceDepth(s);
    const float d1 = SliceDepth(s + 1u);

    // The cell's world-space AABB from its eight corners (conservative: the AABB contains the cell).
    const float3 forward = normalize(clusterForward);
    float3 lo = float3(1e30, 1e30, 1e30);
    float3 hi = float3(-1e30, -1e30, -1e30);
    const float2 corners[4] = { float2(nx0, ny0), float2(nx1, ny0), float2(nx0, ny1), float2(nx1, ny1) };
    [unroll]
    for (uint k = 0u; k < 4u; ++k) {
        float3 origin, dir;
        CameraRay(corners[k], forward, origin, dir);
        const float along  = max(dot(dir, forward), 1e-3);              // depth per unit of ray length
        const float depth0 = dot(origin - cameraPos.xyz, forward);       // the lighting's depth of the origin
        const float3 a = origin + dir * ((d0 - depth0) / along);
        const float3 b = origin + dir * ((d1 - depth0) / along);
        lo = min(lo, min(a, b));
        hi = max(hi, max(a, b));
    }

    RWStructuredBuffer<uint> list = ResourceDescriptorHeap[clusterListUav];
    const uint base = c * kClusterRecordSize;
    const uint slots = clamp(clusterSlots, 1u, kClusterSlotsMax);
    uint count = 0u;

    // Sphere vs AABB: the squared distance from the light to the closest point of the box.
    [loop]
    for (uint pi = 0u; pi < pointLightCount; ++pi) {
        const float3 p = pointLights[pi].position;
        const float3 q = clamp(p, lo, hi) - p;
        if (dot(q, q) <= pointLights[pi].radius * pointLights[pi].radius) {
            if (count < slots) list[base + 1u + count] = pi;
            ++count;
        }
    }
    [loop]
    for (uint si = 0u; si < spotLightCount; ++si) {
        const float3 p = spotLights[si].position;
        const float3 q = clamp(p, lo, hi) - p;
        if (dot(q, q) <= spotLights[si].radius * spotLights[si].radius) {
            if (count < slots) list[base + 1u + count] = si | kClusterSpotBit;
            ++count;
        }
    }
    list[base] = (count > slots) ? (kClusterOverflow | count) : count;

    // r.Lights.ClusterDebug: the cell's AABB in the record's last six slots (when they are free), read
    // back by the lighting's debug modes 2 and 3 into the region dump.
    if ((clusterFlags & kClusterFlag_Debug) != 0u && count <= kClusterSlotsMax - 6u) {
        list[base + kClusterSlotsMax - 5u] = asuint(lo.x);
        list[base + kClusterSlotsMax - 4u] = asuint(lo.y);
        list[base + kClusterSlotsMax - 3u] = asuint(lo.z);
        list[base + kClusterSlotsMax - 2u] = asuint(hi.x);
        list[base + kClusterSlotsMax - 1u] = asuint(hi.y);
        list[base + kClusterSlotsMax]      = asuint(hi.z);
    }
}
