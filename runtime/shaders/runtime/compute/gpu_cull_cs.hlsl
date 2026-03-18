/// GPU Frustum + HiZ Occlusion Culling Compute Shader
///
/// Two-phase visibility test per bounding sphere:
/// 1. Frustum culling: tests sphere against 6 frustum planes (Gribb-Hartmann).
/// 2. HiZ occlusion culling (optional): projects sphere to screen-space AABB,
///    samples the Hi-Z pyramid, and rejects instances behind existing depth.
///
/// @source UE5 HZBOcclusion.usf:13-42 — HZBTestPS (frustum + HZB test pipeline)
/// @source UE5 NaniteHZBCull.ush:565-595 — ProjectSphere, SphereToScreenRect
/// @source UE5 NaniteHZBCull.ush:40-69 — MipLevelForRect
/// @source UE5 NaniteHZBCull.ush:71-107 — GetScreenRect (NDC → pixel → HZB texel)
/// @source UE5 NaniteHZBCull.ush:195-200 — IsVisibleHZB (reversed-Z comparison)
///
/// # Inputs
/// - `g_BoundingSpheres` (t0): StructuredBuffer<float4> — xyz = world-space
///   center, w = radius. One entry per instance.
/// - CullConstants (b1): ViewProjection, View, ViewToClip matrices + counts + HiZ params.
///   Bound via root CBV at slot 1.
///
/// HiZ occlusion requires SM6.6 bindless — per-mip textures are accessed via
/// ResourceDescriptorHeap[] using SRV indices packed in the constant buffer.
/// On SM5, set g_HiZEnabled = 0 (frustum-only culling).
///
/// # Outputs
/// - `g_VisibleIndices` (u0): AppendStructuredBuffer<uint> — indices of visible
///   instances. Consumed by indirect draw calls.
/// - `g_IndirectArgs` (u1): RWByteAddressBuffer — indirect draw argument buffer.
///   Atomically increments `instanceCount` (offset 4) for each visible instance.
///
/// # Thread Group
/// [64, 1, 1] — one thread per instance. Dispatch ceil(instanceCount / 64) groups.

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffer (bound via root CBV at b1, slot 1)
// ─────────────────────────────────────────────────────────────────────────────

/// GPU cull constants. Bound at register b1 via root CBV (slot 1 in the global
/// root signature). This allows > 32 bytes unlike root constants at b0.
///
/// Layout: 3 float4x4 matrices (192 bytes) + integer params + per-mip SRV indices.
/// Total: 256 bytes (16-byte aligned for CBV).
cbuffer CullConstants : register(b1) {
    float4x4 g_ViewProjection;        // Combined view-projection (world → clip).       0
    float4x4 g_ViewMatrix;            // View matrix (world → view).                    64
    float4x4 g_ViewToClip;            // Projection matrix (view → clip).               128
    uint     g_InstanceCount;         // Number of bounding spheres to process.          192
    uint     g_HiZEnabled;            // 0 = frustum only, 1 = frustum + HiZ.           196
    uint     g_HiZWidth;              // Hi-Z mip 0 width in texels.                    200
    uint     g_HiZHeight;             // Hi-Z mip 0 height in texels.                   204
    uint     g_HiZMipCount;           // Number of Hi-Z mip levels available.            208
    uint3    _pad0;                   //                                                 212
    // Per-mip SRV indices for bindless access to individual Hi-Z mip textures.
    // Each mip is a separate R32_FLOAT texture — indices are NOT contiguous.
    uint4    g_HiZMipSrvIndices[3];   // 12 uint32 packed as 3 uint4 (48 bytes).         224
    //                                   Total: 272 → padded to 288 (multiple of 16)
    uint     _pad1[4];               //                                                  272
};                                    //                                                  288

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

StructuredBuffer<float4>       g_BoundingSpheres : register(t0);

AppendStructuredBuffer<uint>   g_VisibleIndices  : register(u0);
RWByteAddressBuffer            g_IndirectArgs    : register(u1);

/// Retrieves the SRV index for the given Hi-Z mip level.
/// The indices are packed into uint4 arrays for efficient CBV layout.
uint GetHiZMipSrvIndex(uint mipLevel)
{
    uint vecIdx  = mipLevel / 4;
    uint compIdx = mipLevel % 4;
    return g_HiZMipSrvIndices[vecIdx][compIdx];
}

// ─────────────────────────────────────────────────────────────────────────────
// Frustum Plane Extraction (Gribb-Hartmann method)
// ─────────────────────────────────────────────────────────────────────────────

/// Extracts 6 frustum planes from a row-major view-projection matrix.
/// Each plane is stored as float4(A, B, C, D) where Ax+By+Cz+D >= 0 means inside.
/// Planes are normalized so that (A,B,C) has unit length.
void ExtractFrustumPlanes(float4x4 vp, out float4 planes[6]) {
    // Left:   row3 + row0
    planes[0] = float4(
        vp[0][3] + vp[0][0],
        vp[1][3] + vp[1][0],
        vp[2][3] + vp[2][0],
        vp[3][3] + vp[3][0]
    );
    // Right:  row3 - row0
    planes[1] = float4(
        vp[0][3] - vp[0][0],
        vp[1][3] - vp[1][0],
        vp[2][3] - vp[2][0],
        vp[3][3] - vp[3][0]
    );
    // Bottom: row3 + row1
    planes[2] = float4(
        vp[0][3] + vp[0][1],
        vp[1][3] + vp[1][1],
        vp[2][3] + vp[2][1],
        vp[3][3] + vp[3][1]
    );
    // Top:    row3 - row1
    planes[3] = float4(
        vp[0][3] - vp[0][1],
        vp[1][3] - vp[1][1],
        vp[2][3] - vp[2][1],
        vp[3][3] - vp[3][1]
    );
    // Near:   row2
    planes[4] = float4(
        vp[0][2],
        vp[1][2],
        vp[2][2],
        vp[3][2]
    );
    // Far:    row3 - row2
    planes[5] = float4(
        vp[0][3] - vp[0][2],
        vp[1][3] - vp[1][2],
        vp[2][3] - vp[2][2],
        vp[3][3] - vp[3][2]
    );

    // Normalize each plane so distance tests are in world units
    [unroll]
    for (uint i = 0; i < 6; ++i) {
        float len = length(planes[i].xyz);
        planes[i] /= len;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Sphere-to-Screen-Rect Projection
// [ Mara & Morgan 2013, "2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere" ]
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 NaniteHZBCull.ush:565-586 — ProjectSphere
/// Projects a sphere's 1D cross-section (x or y) to NDC bounds on that axis.
/// Works in view space where the camera looks along +Z.
///
/// # Arguments
/// * `x` — View-space position on the axis being projected (x or y).
/// * `z` — View-space depth (positive in front of camera).
/// * `r` — Sphere radius.
/// * `resultScale` — Projection matrix diagonal element (ViewToClip[0][0] or ViewToClip[1][1]).
///
/// # Returns
/// float2(min, max) in NDC [-1,1] for this axis.
float2 ProjectSphere(float x, float z, float r, float resultScale)
{
    /// @source UE5 NaniteHZBCull.ush:568 — t = sqrt(x*x + z*z - r*r)
    float t = sqrt(x * x + z * z - r * r);

    /// @source UE5 NaniteHZBCull.ush:570-572 — optimized common denominator form
    float A = (t * z + r * x);
    float B = (t * z - r * x);
    resultScale /= (A * B);

    /// @source UE5 NaniteHZBCull.ush:574-575 — Min/Max tangent points
    float projMin = (t * x - r * z) * B;
    float projMax = (t * x + r * z) * A;

    return float2(projMin, projMax) * resultScale;
}

/// @source UE5 NaniteHZBCull.ush:588-595 — SphereToScreenRect
/// Projects a view-space bounding sphere to an NDC rect (MinX, MinY, MaxX, MaxY).
/// Uses the tight 2D polyhedral bound from Mara & Morgan 2013.
///
/// # Arguments
/// * `viewCenter` — Sphere center in view space.
/// * `radius` — Sphere radius.
/// * `viewToClip` — Projection matrix.
///
/// # Returns
/// float4(minX, minY, maxX, maxY) in NDC [-1,1].
float4 SphereToScreenRect(float3 viewCenter, float radius, float4x4 viewToClip)
{
    /// @source UE5 NaniteHZBCull.ush:591 — ExtentX with ViewToClip[2][0] offset
    float2 extentX = ProjectSphere(viewCenter.x, viewCenter.z, radius, viewToClip[0][0]) + viewToClip[2][0];
    /// @source UE5 NaniteHZBCull.ush:592 — ExtentY with ViewToClip[2][1] offset
    float2 extentY = ProjectSphere(viewCenter.y, viewCenter.z, radius, viewToClip[1][1]) + viewToClip[2][1];

    return float4(extentX.x, extentY.x, extentX.y, extentY.y);
}

// ─────────────────────────────────────────────────────────────────────────────
// HiZ Mip Level Selection
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 NaniteHZBCull.ush:40-69 — MipLevelForRect
/// Selects the coarsest HiZ mip level whose 2x2 footprint can cover the given
/// pixel rect. Uses firstbithigh (full-rate on GCN) instead of log2.
///
/// # Arguments
/// * `rectPixels` — int4(minX, minY, maxX, maxY) in HiZ mip 0 texel coordinates.
///                  Inclusive on both ends: [Min, Max].
///
/// # Returns
/// Mip level index (0 = HiZ mip 0 = half resolution of full depth buffer).
int MipLevelForRect(int4 rectPixels)
{
    /// @source UE5 NaniteHZBCull.ush:54 — firstbithigh of (max - min) per axis
    int2 mipLevelXY = firstbithigh(rectPixels.zw - rectPixels.xy);

    /// @source UE5 NaniteHZBCull.ush:58 — max of both axes, no offset for 2x2 footprint
    /// UE5 uses DesiredFootprintPixels=4 with MipOffset = log2(4)-1 = 1.
    /// For our 2x2 sampling we use DesiredFootprintPixels=2, MipOffset=0.
    int mipLevel = max(max(mipLevelXY.x, mipLevelXY.y), 0);

    /// @source UE5 NaniteHZBCull.ush:66 — alignment check: if rect doesn't fit
    /// in selected mip due to power-of-two quantization, bump up one level
    mipLevel += any((rectPixels.zw >> mipLevel) - (rectPixels.xy >> mipLevel) > 1) ? 1 : 0;

    return mipLevel;
}

// ─────────────────────────────────────────────────────────────────────────────
// HiZ Occlusion Test
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 NaniteHZBCull.ush:135-200 — GetMinDepthFromHZB + IsVisibleHZB
/// @source UE5 HZBOcclusion.usf:30-41 — HZBTestPS sphere→rect→HZB pipeline
///
/// Tests whether a bounding sphere is visible against the Hi-Z pyramid.
///
/// Steps (matching UE5 pipeline):
/// 1. Transform sphere center to view space.
/// 2. Project sphere to tight NDC rect via Mara & Morgan 2013.
/// 3. Convert NDC to HiZ texel coordinates.
/// 4. Select mip level based on rect footprint.
/// 5. Sample 2x2 texels at selected mip and take min depth.
/// 6. Compare sphere's nearest depth against HiZ min (reversed-Z).
///
/// # Arguments
/// * `worldCenter` — World-space sphere center.
/// * `radius` — Sphere radius.
///
/// # Returns
/// `true` if the sphere is potentially visible, `false` if occluded.
bool IsVisibleHiZ(float3 worldCenter, float radius)
{
    // ── Step 1: World → View space ──────────────────────────────────────
    float3 viewCenter = mul(float4(worldCenter, 1.0), g_ViewMatrix).xyz;

    // Sphere entirely behind the near plane — skip HiZ (handled by frustum cull).
    // UE5 uses the bCrossesNearPlane flag for this check.
    if (viewCenter.z + radius <= 0.0)
        return true;

    // Sphere straddles the near plane — cannot project cleanly, consider visible.
    /// @source UE5 NaniteHZBCull.ush:520 — MinW <= 0 && MaxW > 0 → full-screen rect
    if (viewCenter.z - radius <= 0.0)
        return true;

    // ── Step 2: Project sphere to NDC rect ──────────────────────────────
    // Sphere is fully in front of camera (viewCenter.z - radius > 0).
    // Safe to use the tight projection.
    /// @source UE5 NaniteHZBCull.ush:588-595 — SphereToScreenRect
    float4 ndcRect = SphereToScreenRect(viewCenter, radius, g_ViewToClip);

    // Clamp to [-1,1] NDC (sphere may partially extend off-screen).
    ndcRect = clamp(ndcRect, -1.0, 1.0);

    // ── Step 3: NDC → pixel → HiZ texel coordinates ────────────────────
    /// @source UE5 NaniteHZBCull.ush:78 — NDC to UV: X[-1,1]→[0,1], Y[-1,1]→[1,0]
    float4 rectUV = saturate(ndcRect * float2(0.5, -0.5).xyxy + 0.5).xwzy;

    /// @source UE5 NaniteHZBCull.ush:86 — UV to pixel footprint
    float2 hizSize = float2(g_HiZWidth, g_HiZHeight);
    int4 rectPixels = int4(rectUV * hizSize.xyxy + float4(0.5, 0.5, -0.5, -0.5));
    rectPixels.xy = max(rectPixels.xy, int2(0, 0));
    rectPixels.zw = min(rectPixels.zw, int2(g_HiZWidth - 1, g_HiZHeight - 1));

    // Check that the rect covers at least one pixel center.
    /// @source UE5 NaniteHZBCull.ush:91 — bOverlapsPixelCenter
    if (any(rectPixels.zw < rectPixels.xy))
        return true;

    // Make rect valid for HiZ sampling.
    /// @source UE5 NaniteHZBCull.ush:95 — ensure valid even if degenerate
    int4 hzbTexels = int4(rectPixels.xy, max(rectPixels.xy, rectPixels.zw));

    // ── Step 4: Select mip level based on footprint ─────────────────────
    /// @source UE5 NaniteHZBCull.ush:101 — MipLevelForRect
    int mipLevel = MipLevelForRect(hzbTexels);
    mipLevel = min(mipLevel, (int)g_HiZMipCount - 1);

    // Transform texel coordinates to selected mip level.
    /// @source UE5 NaniteHZBCull.ush:104 — HZBTexels >>= HZBLevel
    int4 mipTexels = hzbTexels >> mipLevel;

    // ── Step 5: Load 2x2 texels from the selected mip texture ────────
    // Each Hi-Z mip is a separate R32_FLOAT texture (no mip chain). We use
    // Load() with integer addressing at mip 0 of the per-mip texture.
    // Clamp coordinates to the mip texture dimensions to avoid out-of-bounds.
    /// @source UE5 NaniteHZBCull.ush:161-169 — LoadHZBRow4 with clamped integer coords
    uint mipSrvIndex = GetHiZMipSrvIndex((uint)mipLevel);
    Texture2D<float> hizMipTex = ResourceDescriptorHeap[mipSrvIndex];

    // Clamp to mip texture bounds (mip width/height = hizSize >> mipLevel).
    int2 mipDims = int2(g_HiZWidth >> mipLevel, g_HiZHeight >> mipLevel);
    mipDims = max(mipDims, int2(1, 1));
    int2 maxCoord = mipDims - int2(1, 1);

    // Sample 2x2 neighborhood around the rect, clamped to texture bounds.
    // Matches UE5's 4-tap pattern: (min,min), (max,min), (min,max), (max,max).
    int2 c00 = clamp(mipTexels.xy,             int2(0, 0), maxCoord);
    int2 c10 = clamp(int2(mipTexels.z, mipTexels.y), int2(0, 0), maxCoord);
    int2 c01 = clamp(int2(mipTexels.x, mipTexels.w), int2(0, 0), maxCoord);
    int2 c11 = clamp(mipTexels.zw,             int2(0, 0), maxCoord);

    float4 depth;
    depth.x = hizMipTex.Load(int3(c00, 0));
    depth.y = hizMipTex.Load(int3(c10, 0));
    depth.z = hizMipTex.Load(int3(c01, 0));
    depth.w = hizMipTex.Load(int3(c11, 0));

    /// @source UE5 NaniteHZBCull.ush:186-187 — mask off edge pixels for 1-wide/tall rects
    depth.yz = (mipTexels.x == mipTexels.z) ? 1.0 : depth.yz;
    depth.xy = (mipTexels.y == mipTexels.w) ? 1.0 : depth.xy;

    /// @source UE5 NaniteHZBCull.ush:189 — min of all 4 samples
    float minDepth = min(min(depth.x, depth.y), min(depth.z, depth.w));

    // ── Step 6: Compute sphere's nearest device-Z and compare ───────────
    // The sphere's closest point to the camera in view space is at
    // viewCenter.z - radius. We need its device-Z for comparison.
    /// @source UE5 NaniteHZBCull.ush:527-528 — RectMin.z = MinZ / MaxW
    /// In reversed-Z with our projection: deviceZ = proj[2][2] + proj[3][2] / z
    float nearestViewZ = viewCenter.z - radius;
    float sphereDeviceZ = g_ViewToClip[2][2] + g_ViewToClip[3][2] / nearestViewZ;

    /// @source UE5 NaniteHZBCull.ush:199-200 — IsVisibleHZB: Rect.Depth >= MinDepth
    /// Reversed-Z: larger deviceZ = closer to camera. HiZ stores min (closest visible).
    /// Sphere is visible if its nearest point is at least as close as the HiZ min depth.
    return sphereDeviceZ >= minDepth;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

[numthreads(64, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    uint instanceIdx = dtid.x;
    if (instanceIdx >= g_InstanceCount) return;

    float4 sphere = g_BoundingSpheres[instanceIdx];
    float3 center = sphere.xyz;
    float  radius = sphere.w;

    // ── Phase 1: Frustum culling ────────────────────────────────────────
    // Extract frustum planes from the view-projection matrix
    float4 planes[6];
    ExtractFrustumPlanes(g_ViewProjection, planes);

    // Test sphere against all 6 planes — if the sphere is entirely behind
    // any plane, it is outside the frustum.
    bool visible = true;
    [unroll]
    for (uint i = 0; i < 6; ++i) {
        float dist = dot(planes[i].xyz, center) + planes[i].w;
        if (dist < -radius) {
            visible = false;
            break;
        }
    }

    // ── Phase 2: HiZ occlusion culling (optional) ──────────────────────
    /// @source UE5 HZBOcclusion.usf:34 — bIsVisible && !bCrossesNearPlane → HZB test
    if (visible && g_HiZEnabled != 0) {
        visible = IsVisibleHiZ(center, radius);
    }

    if (visible) {
        g_VisibleIndices.Append(instanceIdx);

        // Atomically increment the instance count in the indirect args buffer.
        // RHIDrawIndexedIndirectArgs layout: [indexCount, instanceCount, startIndex, baseVertex, startInstance]
        // instanceCount is at byte offset 4.
        g_IndirectArgs.InterlockedAdd(4, 1);
    }
}
