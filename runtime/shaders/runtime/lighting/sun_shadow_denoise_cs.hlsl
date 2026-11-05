/// Sun-shadow denoiser (r.Shadows.SoftPCSS.Denoise), stage 2 of 2: the PCSS visibility of sun_shadow_raw_cs.hlsl,
/// accumulated where each surface point was on the previous frame, after an edge-aware pre-filter whose footprint
/// follows the penumbra width and shrinks as the accumulation deepens.
///
/// @ref Schied et al. 2017 "Spatiotemporal Variance-Guided Filtering" — reprojected, count-based accumulation;
///      depth / normal edge-stopping weights
/// @ref Salvi 2016 "An Excursion in Temporal Supersampling" — variance clipping of the history (mean +- gamma sigma)
/// @ref Karis 2014 "High Quality Temporal Supersampling" — Catmull-Rom history fetch clamped to the bilinear taps
/// @ref Fernando 2005 "Percentage-Closer Soft Shadows" — the penumbra width (the PCSS filter radius)
///
/// 1. History. The previous frame's denoised visibility is fetched where this surface point was (the velocity
///    buffer, TAA's reprojection; a still camera fetches its own texel exactly). The four bilinear taps are kept
///    only where they were accumulated on this receiver: the rule of env_mis_reproject.hlsli (the shading normal
///    within 0.9, the view distance the previous camera had to this point within 5 % + 2 cm, a sample count above
///    0, i.e. a sun receiver), so a disocclusion, the far side of a silhouette or a surface that was back-facing to
///    the sun restarts from the current estimate. With all four taps on the receiver the fetch is the 4x4
///    Catmull-Rom one, clamped to the four taps' range: a bilinear re-fetch every frame would blur the penumbra by
///    about 2 px under steady motion (the variance f (1 - f) per frame summed over the history's length). With none
///    of them on it, the 3x3 texels around the nearest one are searched with the same rule (SVGF's fallback): a
///    pixel on a geometric edge that TAA's jitter moves onto the neighbouring surface every other frame would
///    otherwise restart from a single estimate each time and flicker between it and its neighbours' converged value.
/// 2. Spatial pre-filter. This frame's estimate is averaged over the neighbours on the receiver's own surface (on
///    its tangent plane within a pixel footprint per pixel of offset, normal within 0.9: no value crosses a depth
///    step, a crease or a silhouette, so no light leaks around a corner) with a world-space Gaussian of sigma
///    Spatial x the PCSS filter radius / count: a broad penumbra is smoothed while the accumulation is short (a
///    disocclusion, a restart), a contact shadow (a radius of a fraction of a texel) is left as it is, and once the
///    history is deep the footprint is below a pixel and the estimate is the receiver's own, so the converged
///    penumbra keeps the width and position of the PCSS estimate.
/// 3. Accumulation. The blend weight follows the count (1/2, 1/3, ... down to 1/MaxCount). Geometry validation
///    cannot see a shadow that moved over a still receiver (a moving occluder or light), so the history must also
///    stay within a band around this frame's estimates on the receiver's surface neighbourhood (variance
///    clipping), and a history outside it is pulled to its edge and restarted at count 2. Plain variance clipping
///    sizes the band by the neighbourhood's spread, which across a penumbra is mostly the penumbra's own slope: a
///    slowly moving edge then stays inside the band and trails by the band's width over the slope, and the residue
///    decays at 1/MaxCount once it stops. Here the band is centred on a least-squares plane through the
///    neighbourhood and sized by the plane's residual spread (the estimate's noise, Clamp standard deviations) plus
///    half a pixel of the plane's slope (TAA's sub-pixel jitter moves the sample that far between frames), so a
///    shadow that moved more than about half a pixel, plus its noise, is followed at once. The neighbourhood is 5x5
///    where the PCSS radius is 3 pixels or more (the statistics then span several kernel-rotation cells) and 3x3
///    below, so a hard moving edge is held within a pixel of the current estimate.
///
/// The kernel rotation advances every frame (sun_shadow_raw_cs.hlsl), so the accumulation integrates the kernel
/// over many rotations: the penumbra converges to the kernel's integral without the 16-tap grain, and since the
/// history follows the surface, it no longer changes when the camera moves.
///
/// Outputs: the denoised visibility (R32F: fp32 because the per-frame step at the count cap is below an fp16 ulp)
/// read by the UZ_SHADOW_DENOISE permutation of deferred_lighting_ps.hlsl, and the geometry history (RGBA16F:
/// octahedral shading normal, view distance in metres, sample count; count 0 = not a sun receiver).
///
/// Root constants at b0 (21): see RootConstants. No PerFrameCB: the camera comes with the constants.

#ifndef UZ_BINDLESS
[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {}
#else

#include "../common/constants.hlsli"
#include "../common/encoding.hlsli"

// ─── Root Constants (b0) ──────────────────────────────────────────────────
cbuffer RootConstants : register(b0) {
    uint  g_rawSrvIdx;              ///< This frame's estimate (sun_shadow_raw_cs.hlsl, RG16F)
    uint  g_normalIdx;              ///< G-buffer normal (octahedral RG)
    uint  g_positionIdx;            ///< G-buffer world position (RGBA32F)
    uint  g_velocitySrvIdx;         ///< Velocity buffer (RG16F, pixels, this frame minus the previous; TAA's convention)
    uint  g_historySrvIdx;          ///< The previous frame's denoised visibility (R32F; UINT32_MAX = none: restart)
    uint  g_historyGeometrySrvIdx;  ///< The previous frame's geometry history (RGBA16F; UINT32_MAX = none: restart)
    uint  g_outputUavIdx;           ///< This frame's denoised visibility (R32F UAV)
    uint  g_geometryUavIdx;         ///< This frame's geometry history (RGBA16F UAV)
    uint  g_width;
    uint  g_height;
    uint  g_historyReusable;        ///< 1 when the history was written on the previous frame under a continuous camera
    float g_prevCameraPosX;         ///< The previous frame's camera position (cm): the view distance the taps were stored with
    float g_prevCameraPosY;
    float g_prevCameraPosZ;
    float g_cameraPosX;             ///< This frame's camera position (cm): the view distance stored for the next frame
    float g_cameraPosY;
    float g_cameraPosZ;
    float g_pixelAngle;             ///< 2 tan(fovY / 2) / height: the world size of a pixel per unit of view distance
    uint  g_maxCount;               ///< r.Shadows.SoftPCSS.Denoise.MaxCount: the count cap of a broad penumbra
    float g_spatialScale;           ///< r.Shadows.SoftPCSS.Denoise.Spatial: pre-filter sigma at count 1 / PCSS filter radius
    float g_clampSigma;             ///< r.Shadows.SoftPCSS.Denoise.Clamp: history band half-width in neighbourhood sigmas
};

/// History validation: env_mis_reproject.hlsli's receiver rule (normal cosine, view-distance tolerance and floor).
static const float kNormalCos      = 0.9;
static const float kDistanceTol    = 0.05;
static const float kDistanceFloorM = 0.02;
/// Spatial footprint cap: a 9x9 window (2 sigma reaches 4 px).
static const int   kMaxRadius   = 4;
/// Variance-clipping neighbourhood: 5x5 for a broad penumbra, so the statistics span several kernel-rotation cells
/// (r.Shadows.SoftPCSS.StableKernel 2 cells are 2-3 px wide at room scale); 3x3 for a PCSS radius under
/// kBroadPenumbraPx, so a hard moving edge cannot trail by more than a pixel.
static const int   kStatsRadiusBroad  = 2;
static const int   kStatsRadiusNarrow = 1;
static const float kBroadPenumbraPx   = 3.0;
static const int   kGroupSize   = 8;
static const int   kTileSize    = kGroupSize + 2 * kMaxRadius;
/// Same-surface test of a neighbour: its distance to the receiver's tangent plane, in pixel footprints per pixel of
/// offset (a plane is at 0, a curved column far below 1 over the window, a step or an object in front far above).
static const float kPlaneTolerance = 1.0;
/// Normal edge-stopping exponent on N.Nq (SVGF's weight; the 0.9 cut above rejects creases outright).
/// @constants 32: a 5 degree turn keeps 0.88 of the weight, 15 degrees 0.33
static const float kNormalPower = 32.0;
/// Count after a clipped history: the next estimate weighs 1/2 and the history is rebuilt from there.
static const float kRestartCount = 2.0;
/// Absolute floor of the band (fp16 rounding of the raw estimate).
static const float kBandFloor = 0.005;

groupshared float4 s_posVis[kTileSize * kTileSize];  ///< xyz world position, w this frame's visibility
groupshared float4 s_normal[kTileSize * kTileSize];  ///< xyz shading normal, w the PCSS radius (cm; < 0: not a sun receiver)

/// The previous frame's visibility at this surface point and its sample count; false when no bilinear tap was
/// accumulated on this receiver (off-screen, a disocclusion, another surface): the caller restarts.
bool ReprojectSunShadowHistory(int2 px, float3 worldPos, float3 N, out float history, out float historyCount) {
    history = 0.0;
    historyCount = 0.0;
    Texture2D<float2> velocityTex = ResourceDescriptorHeap[g_velocitySrvIdx];
    Texture2D<float>  historyTex  = ResourceDescriptorHeap[g_historySrvIdx];
    Texture2D<float4> geometryTex = ResourceDescriptorHeap[g_historyGeometrySrvIdx];

    float2 prevPixel = float2(px) - velocityTex.Load(int3(px, 0));
    if (any(prevPixel < -0.5) || any(prevPixel > float2(g_width, g_height) - 0.5)) return false;
    int2   base = int2(floor(prevPixel));
    float2 f = prevPixel - float2(base);
    float3 prevCameraPos = float3(g_prevCameraPosX, g_prevCameraPosY, g_prevCameraPosZ);
    float  expectedM = length(prevCameraPos - worldPos) * 0.01;

    float valueSum = 0.0, countSum = 0.0, weightSum = 0.0;
    float lo = 1.0, hi = 0.0;
    uint  validTaps = 0u;
    [unroll] for (int y = 0; y < 2; ++y) {
        [unroll] for (int x = 0; x < 2; ++x) {
            int2 hp = base + int2(x, y);
            if (any(hp < 0) || any(hp >= int2(g_width, g_height))) continue;
            float4 g = geometryTex.Load(int3(hp, 0));
            if (g.w <= 0.0) continue;
            if (dot(N, OctDecode(g.xy)) < kNormalCos) continue;
            if (abs(g.z - expectedM) > kDistanceTol * expectedM + kDistanceFloorM) continue;
            float w = (x == 0 ? 1.0 - f.x : f.x) * (y == 0 ? 1.0 - f.y : f.y);
            float v = historyTex.Load(int3(hp, 0));
            valueSum  += v * w;
            countSum  += g.w * w;
            weightSum += w;
            lo = min(lo, v);
            hi = max(hi, v);
            validTaps += 1u;
        }
    }
    if (weightSum <= 1e-4) {
        // No bilinear tap on this receiver: the 3x3 texels around the nearest one, same rule, equal weights.
        const int2 nearest = int2(floor(prevPixel + 0.5));
        [unroll] for (int sy = -1; sy <= 1; ++sy) {
            [unroll] for (int sx = -1; sx <= 1; ++sx) {
                int2 hp = nearest + int2(sx, sy);
                if (any(hp < 0) || any(hp >= int2(g_width, g_height))) continue;
                float4 g = geometryTex.Load(int3(hp, 0));
                if (g.w <= 0.0) continue;
                if (dot(N, OctDecode(g.xy)) < kNormalCos) continue;
                if (abs(g.z - expectedM) > kDistanceTol * expectedM + kDistanceFloorM) continue;
                valueSum  += historyTex.Load(int3(hp, 0));
                countSum  += g.w;
                weightSum += 1.0;
            }
        }
        if (weightSum <= 0.0) return false;
        history      = valueSum / weightSum;
        historyCount = countSum / weightSum;
        return true;
    }
    history      = valueSum / weightSum;
    historyCount = countSum / weightSum;

    if (validTaps == 4u) {
        // Catmull-Rom over the 4x4 footprint (edge texels repeated at the border), clamped to the validated taps.
        float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
        float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
        float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
        float2 w3 = f * f * (-0.5 + 0.5 * f);
        float wx[4] = { w0.x, w1.x, w2.x, w3.x };
        float wy[4] = { w0.y, w1.y, w2.y, w3.y };
        float cubic = 0.0;
        [unroll] for (int j = 0; j < 4; ++j) {
            int ty = clamp(base.y - 1 + j, 0, int(g_height) - 1);
            float row = 0.0;
            [unroll] for (int i = 0; i < 4; ++i) {
                int tx = clamp(base.x - 1 + i, 0, int(g_width) - 1);
                row += wx[i] * historyTex.Load(int3(tx, ty, 0));
            }
            cubic += wy[j] * row;
        }
        history = clamp(cubic, lo, hi);
    }
    return true;
}

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID,
          uint GI : SV_GroupIndex) {
    Texture2D<float2> rawTex   = ResourceDescriptorHeap[g_rawSrvIdx];
    Texture2D<float4> gbNormal = ResourceDescriptorHeap[g_normalIdx];
    Texture2D<float4> gbPos    = ResourceDescriptorHeap[g_positionIdx];

    // ── The group's tile with a kMaxRadius apron: position, visibility and normal of every receiver ──
    const int2 tileOrigin = int2(Gid.xy) * kGroupSize - kMaxRadius;
    for (uint t = GI; t < uint(kTileSize * kTileSize); t += uint(kGroupSize * kGroupSize)) {
        int2 tp = tileOrigin + int2(int(t) % kTileSize, int(t) / kTileSize);
        float4 posVis = float4(0.0, 0.0, 0.0, 0.0);
        float4 normal = float4(0.0, 0.0, 1.0, -1.0);
        if (all(tp >= 0) && all(tp < int2(g_width, g_height))) {
            float2 r = rawTex.Load(int3(tp, 0));
            if (r.y >= 0.0) {
                posVis = float4(gbPos.Load(int3(tp, 0)).xyz, r.x);
                normal = float4(OctDecode(gbNormal.Load(int3(tp, 0)).rg), r.y);
            }
        }
        s_posVis[t] = posVis;
        s_normal[t] = normal;
    }
    GroupMemoryBarrierWithGroupSync();

    if (DTid.x >= g_width || DTid.y >= g_height) return;
    int2 px = int2(DTid.xy);

    RWTexture2D<float>  outputTex   = ResourceDescriptorHeap[g_outputUavIdx];
    RWTexture2D<float4> geometryOut = ResourceDescriptorHeap[g_geometryUavIdx];

    // Not a sun receiver (sky, back-facing): the lighting does not read it; count 0, no neighbour inherits it.
    float2 raw = rawTex.Load(int3(px, 0));
    if (raw.y < 0.0) {
        outputTex[px]   = raw.x;
        geometryOut[px] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    const int    ci = (int(GTid.y) + kMaxRadius) * kTileSize + int(GTid.x) + kMaxRadius;
    const float3 worldPos  = s_posVis[ci].xyz;
    const float3 N         = s_normal[ci].xyz;
    const float3 cameraPos = float3(g_cameraPosX, g_cameraPosY, g_cameraPosZ);
    const float  viewDist  = length(cameraPos - worldPos);
    const float  footprint = max(g_pixelAngle * viewDist, 1e-4);

    // ── 1. History ───────────────────────────────────────────────────────
    float history = 0.0, historyCount = 0.0;
    bool valid = false;
    if (g_historyReusable != 0u && g_historySrvIdx != kSentinelSrvIndex && g_historyGeometrySrvIdx != kSentinelSrvIndex) {
        valid = ReprojectSunShadowHistory(px, worldPos, N, history, historyCount);
    }
    float count = valid ? min(historyCount + 1.0, float(g_maxCount)) : 1.0;

    // ── 2. Spatial pre-filter and the receiver's neighbourhood statistics ──
    const float sigmaWorld   = g_spatialScale * raw.y / count;
    const int   radius       = min(int(2.0 * sigmaWorld / footprint), kMaxRadius);   // 0: the receiver alone
    const int   statsRadius  = raw.y >= kBroadPenumbraPx * footprint ? kStatsRadiusBroad : kStatsRadiusNarrow;
    const int   window       = max(radius, statsRadius);
    const float invTwoSigma2 = radius > 0 ? 0.5 / (sigmaWorld * sigmaWorld) : 0.0;
    float sum = 0.0, weightSum = 0.0;
    // Plane-fit moments of the neighbourhood (pixel offsets x, y; visibility v).
    float s1 = 0.0, sx = 0.0, sy = 0.0, sxx = 0.0, syy = 0.0, sxy = 0.0, sv = 0.0, sxv = 0.0, syv = 0.0, svv = 0.0;
    for (int dy = -window; dy <= window; ++dy) {
        for (int dx = -window; dx <= window; ++dx) {
            const int    qi = ci + dy * kTileSize + dx;
            const float4 nq = s_normal[qi];
            if (nq.w < 0.0) continue;
            const float4 pv   = s_posVis[qi];
            const float3 d    = pv.xyz - worldPos;
            const int    ring = max(abs(dx), abs(dy));
            if (abs(dot(N, d)) > kPlaneTolerance * footprint * float(max(ring, 1))) continue;
            const float cosN = dot(N, nq.xyz);
            if (cosN < kNormalCos) continue;
            if (ring <= statsRadius) {
                const float fx = float(dx), fy = float(dy);
                s1  += 1.0;
                sx  += fx;
                sy  += fy;
                sxx += fx * fx;
                syy += fy * fy;
                sxy += fx * fy;
                sv  += pv.w;
                sxv += fx * pv.w;
                syv += fy * pv.w;
                svv += pv.w * pv.w;
            }
            if (ring > radius) continue;
            const float w = exp(-dot(d, d) * invTwoSigma2) * pow(saturate(cosN), kNormalPower);
            sum       += w * pv.w;
            weightSum += w;
        }
    }
    // The receiver passes its own tests with weight 1, so neither sum is empty.
    const float current = sum / weightSum;

    // ── 3. Accumulation ──────────────────────────────────────────────────
    float result = current;
    if (valid) {
        // Least-squares plane v = a + b x + c y (Cramer's rule on the normal equations); with fewer than 6 taps or a
        // degenerate layout (all on a line) the plain mean and spread.
        const float c00 = sxx * syy - sxy * sxy;
        const float c01 = sxy * sy - sx * syy;
        const float c02 = sx * sxy - sxx * sy;
        const float det = s1 * c00 + sx * c01 + sy * c02;
        float a = sv / s1, b = 0.0, c = 0.0, dof = max(s1 - 1.0, 1.0);
        if (s1 >= 6.0 && abs(det) > 1e-3 * s1 * s1 * s1) {
            const float c11 = s1 * syy - sy * sy;
            const float c12 = sy * sx - s1 * sxy;
            const float c22 = s1 * sxx - sx * sx;
            a = (c00 * sv + c01 * sxv + c02 * syv) / det;
            b = (c01 * sv + c11 * sxv + c12 * syv) / det;
            c = (c02 * sv + c12 * sxv + c22 * syv) / det;
            dof = s1 - 3.0;
        }
        // Residual sum of squares of the fit: sum v^2 - (a sum v + b sum xv + c sum yv).
        const float noise    = sqrt(max(svv - (a * sv + b * sxv + c * syv), 0.0) / dof);
        const float halfBand = g_clampSigma * noise + 0.5 * (abs(b) + abs(c)) + kBandFloor;
        const float kept     = clamp(history, a - halfBand, a + halfBand);
        if (kept != history) count = min(count, kRestartCount);
        result = lerp(kept, current, 1.0 / count);
    }
    outputTex[px]   = result;
    geometryOut[px] = float4(OctEncode(N), viewDist * 0.01, count);
}

#endif // UZ_BINDLESS
