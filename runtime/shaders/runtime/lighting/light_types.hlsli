/// UzEngine light data structures, attenuation and the clustered light lists (block B.7).
/// @ref Karis 2013 "Real Shading in Unreal Engine 4" — windowed inverse-square attenuation, sphere lights
/// @ref Olsson, Billeter, Assarsson 2012 "Clustered Deferred and Forward Shading" — the cluster grid
/// @ref UE5 LightGridInjection.usf — logarithmic depth slices

#ifndef UZ_LIGHTING_LIGHT_TYPES_HLSLI
#define UZ_LIGHTING_LIGHT_TYPES_HLSLI

static const uint kMaxPointLights = 512;   // block B.7: was 64 (the cbuffer holds 43 KB of the 64 KB allowed)
static const uint kMaxSpotLights  = 128;   // was 16
static const uint kLightFlag_InverseSquared = 1u;

// Cluster list record (cluster_assign_cs.hlsl): [count | flags] then kClusterSlotsMax light slots. A slot
// is the light's index in its array, bit 15 set for a spot light, points first then spots, each in index
// order (the order the per-pixel loop used, so the sums are identical). Count bit 31 = overflow: more
// lights touched the cluster than it has slots; the lighting then evaluates every light of the frame.
static const uint kClusterSlotsMax     = 32u;
static const uint kClusterRecordSize   = kClusterSlotsMax + 1u;
static const uint kClusterSpotBit      = 0x8000u;
static const uint kClusterOverflow     = 0x80000000u;
static const uint kClusterFlag_Enabled = 1u;
static const uint kClusterFlag_Debug   = 2u;

struct PointLightData   // 64 bytes — matches C++ exactly
{
    float3 position;     float radius;
    float3 color;        float sourceRadius;
    float falloffExponent; float specularScale; float diffuseScale; uint flags;
    uint shadowIndex;    float3 _pad;
};

struct SpotLightData    // 80 bytes — matches C++ exactly
{
    float3 position;     float radius;
    float3 color;        float cosOuterAngle;
    float3 direction;    float invCosConeDifference;
    float falloffExponent; float specularScale; float diffuseScale; uint flags;
    uint shadowIndex;    float3 _spotPad;
};

// LightingCB at b2 — guarded so mesh_ps can include this without b2 conflict
#ifndef UZ_LIGHT_TYPES_NO_CB
cbuffer LightingCB : register(b2)
{
    PointLightData pointLights[kMaxPointLights]; // 32768 bytes
    uint pointLightCount;                        // 32768
    uint spotLightCount;                         // 32772
    uint2 _lightPad;
    SpotLightData spotLights[kMaxSpotLights];    // 32784, 10240 bytes

    // ── Clustered light lists (block B.7), offset 43024; mirrors LightingCB in constant_buffers.h ──
    float3 clusterForward;     // camera forward, world space: view depth = dot(p - cameraPos, forward)
    float  clusterNear;        // depth of the first slice boundary (cm); nearer pixels fall in slice 0
    float  clusterFar;         // depth of the last slice boundary (cm)
    float  clusterLogScale;    // slices / log(far / near)
    uint   clusterTileSize;    // tile side in pixels
    uint   clusterTilesX;
    uint   clusterTilesY;
    uint   clusterSlices;
    uint   clusterListSrv;     // bindless SRV of the list buffer (kSentinelSrvIndex = per-pixel loop)
    uint   clusterListUav;     // bindless UAV of the list buffer (the cull pass)
    uint   clusterSlots;       // light slots per cluster this frame (1..kClusterSlotsMax, r.Lights.ClusterSlots)
    uint   clusterFlags;       // kClusterFlag_*
    float  clusterViewWidth;   // render resolution the tiles cover
    float  clusterViewHeight;
    float2 _lightPadB;
    float  _lightPad2[43];     // pad; the C++ struct is 43264 bytes
};

/// The cluster a pixel of the lighting pass belongs to: its 32-pixel tile and its logarithmic depth
/// slice. Depths below `clusterNear` fall into slice 0, beyond `clusterFar` into the last slice.
uint ClusterIndexForPixel(uint2 px, float viewDepth)
{
    const uint tx = min(px.x / clusterTileSize, clusterTilesX - 1u);
    const uint ty = min(px.y / clusterTileSize, clusterTilesY - 1u);
    const float d = max(viewDepth, clusterNear);
    const uint s = min(uint(log(d / clusterNear) * clusterLogScale), clusterSlices - 1u);
    return (s * clusterTilesY + ty) * clusterTilesX + tx;
}
#endif

/// @ref Karis 2013 — windowed inverse-square falloff
float DistanceAttenuation(float distSq, float invRadiusSq)
{
    float f = distSq * invRadiusSq;
    float s = saturate(1.0 - f * f);
    return s * s / max(distSq, 1e-4);
}

float SpotAngleAttenuation(float cosAngle, float cosOuterAngle, float invCosConeDiff)
{
    float t = saturate((cosAngle - cosOuterAngle) * invCosConeDiff);
    return t * t;
}

#endif
