/// UzEngine Froxel Volumetric Fog — Temporal filter + scatter integration (compute).
///
/// Each thread handles one XY column, front to back. Per froxel:
///   1. Temporal reprojection: the froxel centre is projected with the previous frame's unjittered
///      view-projection; the previous filtered volume is fetched trilinearly there (view depth ->
///      exponential slice coordinate) and blended in with an exponential moving average
///      (alpha ~0.05 for the new sample). Guards: no history outside the previous frustum or grid
///      (disocclusion), the camera cut / first frame (temporalEnabled = 0), and a variance clamp
///      of the history to the current frame's 3x3x3 neighbourhood (moving lights and shadows do
///      not ghost). The filtered value is the history the next frame reads.
///   2. Integration: accumulate the in-scatter attenuated by the transmittance so far, then the
///      slice's own transmittance (Beer-Lambert), with the energy-conserving per-slice integral.
///
/// @ref Hillaire 2015 "Physically Based & Unified Volumetric Rendering in Frostbite"
///      Section 5.3 — energy-conservative analytical integration; temporal reprojection
/// @ref Karis 2014 "High Quality Temporal Supersampling" — neighbourhood (variance) clamping
/// @ref Salvi 2016 "An Excursion in Temporal Supersampling" — variance clipping

#include "../common/constants.hlsli"

cbuffer VolumetricFogCB : register(b2) {
    row_major float4x4 invViewProj;
    float3 cameraPos;
    float  nearZ;
    float  farZ;
    float  globalDensity;
    float  heightFalloff;
    float  heightOffset;
    float3 albedo;
    float  phaseG;
    float3 sunDir;
    float  sunIntensity;
    float3 sunColor;
    float  temporalAlpha;
    uint   inscatteringUavIdx;
    uint   inscatteringSrvIdx;
    uint   integratedUavIdx;
    uint   integratedSrvIdx;
    uint   depthSrvIdx;
    uint   frameIndex;
    uint   gridX;
    uint   gridY;
    uint   gridZ;
    float  extinctionScale;
    float  emissiveR;                 // scalars: a float3 here would be padded to offset 192 (C++ has 184)
    float  emissiveG;
    float  emissiveB;
    float  fogStartDistance;
    float  nearFadeInDistance;
    float  cameraFarZ;                // camera far plane (cm) for the composite depth linearisation
    uint   sunShadowEnabled;          // r.VolumetricFog.SunShadow
    uint   debugMode;
    uint   historySrvIdx;             // previous frame's filtered volume (read)
    uint   historyUavIdx;             // this frame's filtered volume (write)
    uint   temporalEnabled;           // 1 = blend the reprojected history
    uint   localLightsEnabled;
    uint   localShadowEnabled;
    uint   jitterEnabled;
    uint   debugStage;
    float  temporalClamp;             // history clamp half-width in neighbourhood standard deviations (0 = none)
    float  ambientR;
    float  ambientG;
    float  ambientB;
    float  localLightScale;
    float  _fogPad0b;
    float  _fogPad0c;
    row_major float4x4 prevViewProj;  // previous frame's unjittered view-projection (offset 272)
};

/// s2: bilinear clamp (the shared static sampler the temporal passes use)
SamplerState historySampler : register(s2);

/// @ref Hillaire 2015 — exponential depth distribution (must match inject)
/// @ref UE5 GetVolumetricFogGridZParams — start offset pushes grid away from camera
float StartZ() { return max(nearZ, fogStartDistance); }

float SliceToDepth(uint slice, uint numSlices)
{
    float startZ = StartZ();
    float t = (float(slice) + 0.5) / float(numSlices);
    return startZ * pow(farZ / max(startZ, 0.01), t);
}

/// Inverse of SliceToDepth: the continuous slice coordinate of a view depth ([0, 1] inside the grid).
float DepthToSliceCoord(float viewDepth)
{
    float startZ = StartZ();
    return log(max(viewDepth, 1e-3) / startZ) / log(farZ / startZ);
}

/// Compute slice thickness for the exponential distribution.
/// @ref UE5 — slice 0 starts at startZ, NOT at depth 0.
float SliceThickness(uint slice, uint numSlices)
{
    float d0 = (slice > 0) ? SliceToDepth(slice - 1, numSlices) : StartZ();
    float d1 = SliceToDepth(slice, numSlices);
    return d1 - d0;
}

/// World position of the far-plane point behind an NDC xy (reversed-Z: the far plane is z = 0).
float3 UnprojectFar(float2 ndc)
{
    float4 worldH = mul(float4(ndc, 0.0, 1.0), invViewProj);
    return worldH.xyz / worldH.w;
}

/// The froxel centre of column xy at view depth `viewDepth` (the inject's unjittered position).
float3 FroxelWorldPos(uint2 xy, float viewDepth)
{
    float2 uv = (float2(xy) + 0.5) / float2(gridX, gridY);
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float3 ray     = normalize(UnprojectFar(ndc) - cameraPos);
    float3 forward = normalize(UnprojectFar(float2(0.0, 0.0)) - cameraPos);
    return cameraPos + ray * (viewDepth / max(dot(ray, forward), 1e-3));
}

// ─── Temporal filter ───────────────────────────────────────────────────────

/// The reprojected, clamped history blended with this frame's injected value, or the value itself
/// when no valid history covers the froxel.
float4 TemporalFilter(Texture3D<float4> injected, Texture3D<float4> history, uint3 froxel, float4 cur)
{
    // Reproject the froxel centre into the previous frame's grid.
    float3 wp = FroxelWorldPos(froxel.xy, SliceToDepth(froxel.z, gridZ));
    float4 prevClip = mul(float4(wp, 1.0), prevViewProj);
    if (prevClip.w <= 1e-3) return cur;                               // behind the previous camera
    float2 prevNdc = prevClip.xy / prevClip.w;
    float3 prevUVW = float3(prevNdc.x * 0.5 + 0.5, 0.5 - prevNdc.y * 0.5, DepthToSliceCoord(prevClip.w));
    if (any(prevUVW < 0.0) || any(prevUVW > 1.0)) return cur;         // outside the previous frustum / grid: disocclusion

    float4 hist = history.SampleLevel(historySampler, prevUVW, 0);

    // Neighbourhood clamp (3x3x3 of this frame's injected volume): the history is held inside the
    // intersection of the neighbourhood's min/max box and its mean +- temporalClamp sigma band, so a
    // moving light or shadow bounds it to what this frame sees around the froxel and it cannot trail
    // behind (the box alone reacts fastest next to a bright core, the band alone rejects outliers).
    if (temporalClamp > 0.0) {
        float4 m1 = float4(0, 0, 0, 0);
        float4 m2 = float4(0, 0, 0, 0);
        float4 lo = cur;
        float4 hi = cur;
        [unroll] for (int dz = -1; dz <= 1; ++dz)
        [unroll] for (int dy = -1; dy <= 1; ++dy)
        [unroll] for (int dx = -1; dx <= 1; ++dx) {
            int3 p = clamp(int3(froxel) + int3(dx, dy, dz), int3(0, 0, 0), int3(gridX, gridY, gridZ) - 1);
            float4 s = injected.Load(int4(p, 0));
            m1 += s;
            m2 += s * s;
            lo = min(lo, s);
            hi = max(hi, s);
        }
        float4 mean  = m1 / 27.0;
        float4 sigma = sqrt(max(m2 / 27.0 - mean * mean, 0.0));
        lo = max(lo, mean - temporalClamp * sigma);
        hi = min(hi, mean + temporalClamp * sigma);
        hist = clamp(hist, lo, hi);
    }

    // Exponential moving average: temporalAlpha of the new sample (0.05: ~20 frames of jittered
    // samples average inside each froxel).
    return lerp(hist, cur, temporalAlpha);
}

// ─── Main compute — front-to-back accumulation per column ──────────────────

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= gridX || DTid.y >= gridY) return;

    // Read inscattering volume via SRV (inject output)
    Texture3D<float4> inscatteringVol = ResourceDescriptorHeap[inscatteringSrvIdx];
    // Write integrated volume via UAV (scatter output)
    RWTexture3D<float4> integratedVol = ResourceDescriptorHeap[integratedUavIdx];

    const bool temporal = temporalEnabled != 0u;
    const bool writeHistory = historyUavIdx != kSentinelSrvIndex;

    float3 accumInscatter = float3(0, 0, 0);
    float  accumTransmittance = 1.0;

    // Front-to-back: slice 0 is nearest to camera
    float accumDepth = 0.0;

    for (uint z = 0; z < gridZ; ++z) {
        const uint3 froxel = uint3(DTid.xy, z);
        float4 sliceData = inscatteringVol.Load(int4(froxel, 0));

        if (temporal) {
            Texture3D<float4> historyVol = ResourceDescriptorHeap[historySrvIdx];
            sliceData = TemporalFilter(inscatteringVol, historyVol, froxel, sliceData);
        }
        if (writeHistory) {
            RWTexture3D<float4> historyOut = ResourceDescriptorHeap[historyUavIdx];
            historyOut[froxel] = sliceData;
        }

        float3 sliceInscatter = sliceData.rgb;
        float  sliceExtinction = sliceData.a;

        float thickness = SliceThickness(z, gridZ);
        accumDepth += thickness;

        // ── Near-camera fade-in (UE5 pattern) ────────────────────────
        // @ref UE5 VolumetricFog.usf — FadeInLerpValue suppresses fog
        // near the camera to prevent the "white blob" artifact.
        float fadeIn = (nearFadeInDistance > 0.0)
            ? saturate(accumDepth / nearFadeInDistance)
            : 1.0;

        // Transmittance through this slice
        // @ref Beer-Lambert law: T = exp(-extinction × distance)
        float sliceTransmittance = exp(-sliceExtinction * thickness);

        // Energy-conservative integration (Frostbite method):
        // @ref Hillaire 2015 Eq. 5 — analytical integration within each slice
        // S_int = (S - S * T_step) / max(extinction, epsilon)
        // Modulated by fadeIn to suppress near-camera contribution.
        float3 Sint = fadeIn * (sliceInscatter - sliceInscatter * sliceTransmittance)
                    / max(sliceExtinction, 1e-6);

        accumInscatter += accumTransmittance * Sint;
        accumTransmittance *= lerp(1.0, sliceTransmittance, fadeIn);

        // Write integrated result for this slice
        integratedVol[froxel] = float4(accumInscatter, accumTransmittance);
    }
}
