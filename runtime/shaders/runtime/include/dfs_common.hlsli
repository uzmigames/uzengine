/// Distance Field Shadow — Shared Types and SDF Sampling Functions
///
/// Contains the GPU-side data structures and sampling functions used by both
/// the object culling and cone tracing compute shaders. Translates UE5's
/// DistanceFieldLightingShared.ush SDF sampling into UzEngine's simpler
/// sparse brick atlas scheme.
///
/// # Atlas Layout
/// The SDF atlas is a single Texture3D (R8_UNORM) divided into 8x8x8 bricks.
/// Each brick has 8 voxels per dimension: 7 unique + 1 border for trilinear
/// filtering. Bricks are laid out linearly in the 3D texture.
///
/// # Quantization
/// Texel value 0.0 = deep inside mesh (most negative distance).
/// Texel value 0.5 (128/255) = zero-crossing (surface).
/// Texel value 1.0 = farthest outside mesh (most positive distance).
///
/// @source UE5 DistanceFieldLightingShared.ush:46-436 — atlas params, FDFAssetData, SampleSparseMeshSignedDistanceField
/// @source UE5 DistanceFieldShadowingShared.ush:66-221 — ShadowRayTraceThroughCulledObjects

#ifndef DFS_COMMON_HLSLI
#define DFS_COMMON_HLSLI

// UE5 flow control hints — map to HLSL attributes
#ifndef LOOP
#define LOOP [loop]
#endif

#ifndef BRANCH
#define BRANCH [branch]
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 DistanceFieldLightingShared.ush:100
/// @constants INVALID_BRICK_INDEX=0xFFFFFFFF (DistanceFieldLightingShared.ush:100)
static const uint INVALID_BRICK_INDEX = 0xFFFFFFFF;

/// @source UE5 DistanceFieldLightingShared.ush:97-98
/// @constants DF_MAX_NUM_MIPS=3 (DistanceFieldLightingShared.ush:98)
static const uint DF_MAX_NUM_MIPS = 3;

/// Brick voxel size — 8x8x8 voxels per brick.
/// @source UE5 DistanceFieldAtlas.h:34 — DistanceField::BrickSize = 8
static const uint SDF_BRICK_SIZE = 8;

/// Unique data voxels per brick dimension (brick size minus 1 border voxel).
/// UE5 uses 7 unique + 1 border = 8 total.
/// @source UE5 DistanceFieldLightingShared.ush:49 — DistanceFieldUniqueDataBrickSize
static const uint SDF_UNIQUE_BRICK_SIZE = 7;

/// @source UE5 DistanceFieldShadowing.usf:22
/// @constants MAX_TRACE_SPHERE_RADIUS=100 (DistanceFieldShadowing.usf:22)
static const float MAX_TRACE_SPHERE_RADIUS = 100.0f;

// ─────────────────────────────────────────────────────────────────────────────
// Per-Object GPU Data — matches uz::SdfObjectGpuData in sdf_types.h
// ─────────────────────────────────────────────────────────────────────────────

/// GPU-side per-object SDF instance data.
///
/// Layout matches `uz::SdfObjectGpuData` (192 bytes = 12 x float4).
/// Loaded from a StructuredBuffer<float4> with 12-float4 stride.
///
/// @source UE5 DistanceFieldLightingShared.ush:232-263 — FDFObjectData
struct SdfObjectData {
    float4x4 worldToLocal;       // Rows 0-3: world-to-local transform (64 bytes)
    float3   localBoundsMin;     // Row 4: local AABB min
    float    pad0;
    float3   localBoundsMax;     // Row 5: local AABB max
    float    pad1;
    float3   worldBoundsMin;     // Row 6: world AABB min
    float    pad2;
    float3   worldBoundsMax;     // Row 7: world AABB max
    uint     atlasOffset;        // Row 7.w: first brick index in atlas
    float    voxelSize;          // Row 8.x: voxel size in world units (cm)
    uint     resX;               // Row 8.y: grid resolution X (in bricks)
    uint     resY;               // Row 8.z: grid resolution Y (in bricks)
    uint     resZ;               // Row 8.w: grid resolution Z (in bricks)
    uint     indirectionOffset;  // Row 9.x: offset into global indirection buffer for this object
                                 // @source UE5 DistanceFieldLightingShared.ush:83 — IndirectionTableOffset
    uint     numMips;            // Row 9.y: number of available MIP levels (1-3)
                                 // @source UE5 DistanceFieldLightingShared.ush:77 — NumMips
    float    pad3;               // Row 9.z: reserved
    float    pad4;               // Row 9.w: reserved
    float4   reserved[2];        // Rows 10-11: padding to 192 bytes
};

/// Loads an SdfObjectData from a raw float4 StructuredBuffer.
///
/// @source UE5 DistanceFieldLightingShared.ush:265-301 — LoadDFObjectDataFromBuffer
/// @constants DF_OBJECT_DATA_STRIDE=12 (our stride, 192/16=12 float4s)
/// @deviations Simplified layout vs UE5's FDFObjectData (no double-float position,
///             no VolumeToWorld, no draw distance — we use a simpler world AABB scheme)
SdfObjectData LoadSdfObjectData(StructuredBuffer<float4> objectBuffer, uint objectIndex) {
    uint base = objectIndex * 12; // 12 float4s per object (192 bytes)

    SdfObjectData obj;

    // World-to-local 4x4 matrix (rows 0-3)
    float4 r0 = objectBuffer[base + 0];
    float4 r1 = objectBuffer[base + 1];
    float4 r2 = objectBuffer[base + 2];
    float4 r3 = objectBuffer[base + 3];
    obj.worldToLocal = float4x4(r0, r1, r2, r3);

    // Local AABB (rows 4-5)
    float4 v4 = objectBuffer[base + 4];
    obj.localBoundsMin = v4.xyz;
    obj.pad0 = v4.w;

    float4 v5 = objectBuffer[base + 5];
    obj.localBoundsMax = v5.xyz;
    obj.pad1 = v5.w;

    // World AABB (rows 6-7)
    float4 v6 = objectBuffer[base + 6];
    obj.worldBoundsMin = v6.xyz;
    obj.pad2 = v6.w;

    float4 v7 = objectBuffer[base + 7];
    obj.worldBoundsMax = v7.xyz;
    obj.atlasOffset = asuint(v7.w);

    // Voxel size + resolution (row 8)
    float4 v8 = objectBuffer[base + 8];
    obj.voxelSize = v8.x;
    obj.resX = asuint(v8.y);
    obj.resY = asuint(v8.z);
    obj.resZ = asuint(v8.w);

    // Indirection offset + MIP count (row 9)
    float4 v9 = objectBuffer[base + 9];
    obj.indirectionOffset = asuint(v9.x);
    obj.numMips           = asuint(v9.y);
    obj.pad3              = v9.z;
    obj.pad4              = v9.w;

    obj.reserved[0] = objectBuffer[base + 10];
    obj.reserved[1] = objectBuffer[base + 11];

    return obj;
}

// ─────────────────────────────────────────────────────────────────────────────
// Atlas Parameters — passed via root constants or cbuffer
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the atlas 3D UV from a linear brick index and a local voxel UV [0,1]^3.
///
/// Bricks are tiled linearly in the 3D atlas. Given the atlas dimension in bricks
/// along each axis, we decompose the linear index into 3D coordinates, then
/// compute the atlas UV including the half-texel offset for correct trilinear sampling.
///
/// @source UE5 DistanceFieldLightingShared.ush:420-433 — SampleSparseMeshSignedDistanceField, atlas UV computation
/// @constants None — all parameters passed in
/// @deviations NONE
float3 BrickIndexToAtlasUV(uint brickIndex, float3 localUV, uint3 atlasSizeInBricks, float3 atlasTexelSize) {
    // Decompose linear brick index into 3D brick coordinate
    /// @source UE5 DistanceFieldLightingShared.ush:422-426 — BrickOffset decomposition via mask and shift
    uint3 brickCoord;
    brickCoord.x = brickIndex % atlasSizeInBricks.x;
    brickCoord.y = (brickIndex / atlasSizeInBricks.x) % atlasSizeInBricks.y;
    brickCoord.z = brickIndex / (atlasSizeInBricks.x * atlasSizeInBricks.y);

    // Each brick occupies SDF_BRICK_SIZE texels in the atlas.
    // The unique data region is SDF_UNIQUE_BRICK_SIZE voxels, centered within the brick.
    // Half-texel offset ensures sampling lands on texel centers.
    /// @source UE5 DistanceFieldLightingShared.ush:429 — AtlasUV = BrickOffset * BrickOffsetToAtlasUVScale + BrickLocalUV * UniqueDataBrickSizeInAtlasTexels + HalfTexelSize
    float3 brickOrigin = float3(brickCoord) * float(SDF_BRICK_SIZE) * atlasTexelSize;
    float3 uniqueSize  = float3(SDF_UNIQUE_BRICK_SIZE, SDF_UNIQUE_BRICK_SIZE, SDF_UNIQUE_BRICK_SIZE) * atlasTexelSize;
    float3 halfTexel   = atlasTexelSize * 0.5f;

    return brickOrigin + localUV * uniqueSize + halfTexel;
}

// ─────────────────────────────────────────────────────────────────────────────
// MIP Level Selection
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the optimal SDF MIP level based on the current cone/step radius.
///
/// When the cone is wide (far from the ray origin), we can use a coarser MIP
/// level (lower resolution, wider band) for faster tracing without loss of
/// visual quality. When the cone is narrow (near the surface), we need the
/// highest resolution MIP for accurate shadow edges.
///
/// UE5's shadow tracing (DistanceFieldShadowingShared.ush:132) always uses
/// the highest resolution MIP via `LoadDFAssetDataHighestResolution`. However,
/// the standalone distance query (DistanceFieldLightingShared.ush:458-469)
/// cascades from coarse to fine: it starts at MIP 0 (coarsest) and refines
/// only when the distance is within 25% of the band width.
///
/// For cone tracing, we select MIP based on the ratio of step distance to
/// voxel size: when the step is large relative to the voxel grid, a coarser
/// MIP provides the same information at lower cost.
///
/// @source UE5 DistanceFieldShadowingShared.ush:132 — LoadDFAssetDataHighestResolution (shadow always uses highest)
/// @source UE5 DistanceFieldLightingShared.ush:458-469 — MIP cascade for culling queries
/// @constants DF_MAX_NUM_MIPS=3 (DistanceFieldLightingShared.ush:98)
/// @deviations UE5 shadow tracing uses highest MIP only; we add optional MIP
///             selection for coarse tracing optimization. When numMips==1, this
///             returns 0 and behaves identically to UE5.
uint SelectSdfMipLevel(float stepDistance, float voxelSize, uint numMips) {
    // Compute the MIP level where the voxel size matches the step distance.
    // Each MIP doubles the voxel size (2x downsampling per level).
    // mipLevel = log2(stepDistance / voxelSize), clamped to [0, numMips-1]
    /// @source UE5 DistanceFieldLightingShared.ush:465 — abs(DistanceField) > .25 * MaxEncodedDistance threshold for MIP cascade
    float ratio = stepDistance / max(voxelSize, 0.0001f);
    // Use log2 of the ratio: when step distance == voxelSize, MIP 0 (finest).
    // When step distance == 2*voxelSize, MIP 1. When == 4*voxelSize, MIP 2.
    int mipLevel = max(int(log2(max(ratio, 1.0f))), 0);
    return uint(min(uint(mipLevel), numMips - 1));
}

// ─────────────────────────────────────────────────────────────────────────────
// SDF Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Samples the sparse mesh SDF at a volume-space position.
///
/// Transforms the volume-space position into indirection space, looks up the
/// brick index from the indirection buffer, then samples the 3D atlas texture
/// with trilinear filtering.
///
/// Returns the signed distance in volume-space units. If the brick is invalid
/// (empty space far from any surface), returns the maximum encodable distance.
///
/// @source UE5 DistanceFieldLightingShared.ush:392-436 — SampleSparseMeshSignedDistanceField
/// @constants INVALID_BRICK_INDEX=0xFFFFFFFF (DistanceFieldLightingShared.ush:100)
/// @deviations Simplified indirection: uses flat 3D table indexed by (z*resY+y)*resX+x,
///             same as UE5 OFFSET_DATA_STRUCT==0 path (ByteAddressBuffer).
///             ScaleBias computed from voxelSize instead of per-mip FDFAssetData.
float SampleSparseMeshSdf(
    float3 volumePos,
    SdfObjectData obj,
    StructuredBuffer<uint> indirectionBuffer,
    uint indirectionOffset,
    Texture3D<float> sdfAtlas,
    SamplerState atlasSampler,
    uint3 atlasSizeInBricks,
    float3 atlasTexelSize)
{
    // Compute the maximum encodable distance for this SDF (the narrow band width)
    // The distance range is ±(voxelSize * SDF_BRICK_SIZE / 2) = the narrow band
    /// @source UE5 DistanceFieldLightingShared.ush:412 — MaxEncodedDistance = DistanceFieldToVolumeScaleBias.x + DistanceFieldToVolumeScaleBias.y
    float maxEncodedDistance = obj.voxelSize * float(SDF_BRICK_SIZE) * 0.5f;

    // Compute the volume extent (half-size of the local AABB)
    float3 volumeExtent = (obj.localBoundsMax - obj.localBoundsMin) * 0.5f;
    float3 volumeCenter = (obj.localBoundsMax + obj.localBoundsMin) * 0.5f;

    // Transform from volume space [-extent, +extent] centered at volumeCenter
    // to normalized [0, 1] space within the SDF volume
    /// @source UE5 DistanceFieldLightingShared.ush:395 — IndirectionPos = SampleVolumePosition * VolumeToIndirectionScale + VolumeToIndirectionAdd
    float3 normalizedPos = (volumePos - volumeCenter + volumeExtent) / (2.0f * volumeExtent);

    // Guard: if the sample position is outside the SDF volume [0,1]^3,
    // return max encoded distance (far from any surface).
    // UE5 handles this via VolumePositionExtent clamping in the caller
    // (DistanceFieldShadowingShared.ush:164) and relies on the indirection
    // table boundary; we add an explicit early-out for safety.
    /// @source UE5 DistanceFieldLightingShared.ush:413-416 — out-of-bounds returns MaxEncodedDistance
    /// @deviations Explicit bounds check added as safety guard; UE5 relies on caller clamping
    if (any(normalizedPos < 0.0f) || any(normalizedPos > 1.0f)) {
        return maxEncodedDistance;
    }

    float3 indirectionPos = normalizedPos * float3(obj.resX, obj.resY, obj.resZ);

    // Clamp to valid indirection grid range (prevent out-of-bounds indexing at edges)
    indirectionPos = clamp(indirectionPos, 0.0f, float3(obj.resX, obj.resY, obj.resZ) - 0.001f);

    int3 indirectionCoord = int3(indirectionPos);

    // Look up brick index from indirection table
    /// @source UE5 DistanceFieldLightingShared.ush:399-402 — IndirectionIndex, BrickIndex load
    uint indirectionIndex = (uint(indirectionCoord.z) * obj.resY + uint(indirectionCoord.y)) * obj.resX + uint(indirectionCoord.x);
    uint brickIndex = indirectionBuffer[indirectionOffset + indirectionIndex];

    // If the brick is invalid (INVALID_BRICK_INDEX = 0xFFFFFFFF), the region was empty
    // during baking and had distance > bandWidth. Return max distance.
    /// @source UE5 DistanceFieldLightingShared.ush:413-416 — invalid brick returns MaxEncodedDistance
    if (brickIndex == INVALID_BRICK_INDEX) {
        return maxEncodedDistance;
    }

    // Remap the brick index relative to this object's atlas region
    uint absoluteBrickIndex = obj.atlasOffset + brickIndex;

    // Compute local UV within the brick [0, 1]
    /// @source UE5 DistanceFieldLightingShared.ush:419 — BrickLocalUV = IndirectionPos - IndirectionCoord
    float3 brickLocalUV = indirectionPos - float3(indirectionCoord);

    // Sample the atlas with trilinear filtering
    /// @source UE5 DistanceFieldLightingShared.ush:429-432 — AtlasUV computation and sample
    float3 atlasUV = BrickIndexToAtlasUV(absoluteBrickIndex, brickLocalUV, atlasSizeInBricks, atlasTexelSize);
    float encodedDistance = sdfAtlas.SampleLevel(atlasSampler, atlasUV, 0).r;

    // Decode from [0, 1] to signed distance:
    // encoded 0.0 -> -maxEncodedDistance (deep inside)
    // encoded 0.5 -> 0.0 (surface)
    // encoded 1.0 -> +maxEncodedDistance (outside)
    /// @source UE5 DistanceFieldLightingShared.ush:432 — DistanceField = EncodedDistanceField * ScaleBias.x + ScaleBias.y
    float distanceField = encodedDistance * (2.0f * maxEncodedDistance) - maxEncodedDistance;

    // Clamp the returned distance to the valid band [-bandWidth, +bandWidth].
    // UE5's encoding inherently constrains this via the R8_UNORM [0,1] range,
    // but floating-point imprecision in the decode can produce values slightly
    // outside the band. Explicit clamping ensures downstream cone tracing
    // never sees distances beyond the reliable SDF range.
    /// @source UE5 DistanceFieldLightingShared.ush:392 — "clamped to the band of the current mip"
    /// @deviations Explicit clamp added for robustness; UE5 relies on UNORM encoding precision
    distanceField = clamp(distanceField, -maxEncodedDistance, maxEncodedDistance);

    return distanceField;
}

// ─────────────────────────────────────────────────────────────────────────────
// Ray-Box Intersection
// ─────────────────────────────────────────────────────────────────────────────

/// Ray-AABB intersection returning normalized entry/exit times along the ray.
///
/// @source UE5 Common.ush — LineBoxIntersect (used throughout DFS)
/// @constants None
/// @deviations NONE
float2 LineBoxIntersect(float3 rayOrigin, float3 rayEnd, float3 boxMin, float3 boxMax) {
    float3 invRayDir = 1.0f / (rayEnd - rayOrigin);

    float3 firstPlane  = (boxMin - rayOrigin) * invRayDir;
    float3 secondPlane = (boxMax - rayOrigin) * invRayDir;

    float3 closest  = min(firstPlane, secondPlane);
    float3 furthest = max(firstPlane, secondPlane);

    float2 intersections;
    intersections.x = max(closest.x,  max(closest.y,  closest.z));
    intersections.y = min(furthest.x, min(furthest.y, furthest.z));

    return saturate(intersections);
}

// ─────────────────────────────────────────────────────────────────────────────
// World Position Reconstruction from Depth
// ─────────────────────────────────────────────────────────────────────────────

/// Reconstructs world-space position from screen UV and reversed-Z depth.
///
/// Uses the inverse view-projection matrix to unproject from clip space.
/// UzEngine uses reversed-Z (near=1.0, far=0.0).
///
/// @source UE5 DistanceFieldShadowing.usf:647 — OpaqueTranslatedWorldPosition reconstruction
/// @constants None
/// @deviations Uses reversed-Z directly (UE5 uses ConvertFromDeviceZ first)
float3 ReconstructWorldPosition(float2 screenUV, float rawDepth, float4x4 invViewProj) {
    // Convert UV [0,1] to NDC [-1,1] (flip Y for DX convention)
    float2 ndc = float2(screenUV.x * 2.0f - 1.0f, 1.0f - screenUV.y * 2.0f);

    // Reversed-Z: rawDepth is in [1, 0] (near=1, far=0)
    float4 clipPos = float4(ndc, rawDepth, 1.0f);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

#endif // DFS_COMMON_HLSLI
