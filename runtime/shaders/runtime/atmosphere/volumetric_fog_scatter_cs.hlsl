/// UzEngine Froxel Volumetric Fog — Scatter integration (compute).
///
/// Front-to-back integration per column: accumulates inscattering and
/// transmittance from near to far slices. Each thread handles one XY column.
///
/// @ref Hillaire 2015 "Physically Based & Unified Volumetric Rendering in Frostbite"
///      Section 5.3 — Energy-conservative analytical integration

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
};

/// @ref Hillaire 2015 — exponential depth distribution (must match inject)
/// @ref UE5 GetVolumetricFogGridZParams — start offset pushes grid away from camera
float SliceToDepth(uint slice, uint numSlices)
{
    float startZ = max(nearZ, fogStartDistance);
    float t = (float(slice) + 0.5) / float(numSlices);
    return startZ * pow(farZ / max(startZ, 0.01), t);
}

/// Compute slice thickness for the exponential distribution.
/// @ref UE5 — slice 0 starts at startZ, NOT at depth 0.
float SliceThickness(uint slice, uint numSlices)
{
    float startZ = max(nearZ, fogStartDistance);
    float d0 = (slice > 0) ? SliceToDepth(slice - 1, numSlices) : startZ;
    float d1 = SliceToDepth(slice, numSlices);
    return d1 - d0;
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

    float3 accumInscatter = float3(0, 0, 0);
    float  accumTransmittance = 1.0;

    // Front-to-back: slice 0 is nearest to camera
    float accumDepth = 0.0;

    for (uint z = 0; z < gridZ; ++z) {
        float4 sliceData = inscatteringVol.Load(int4(DTid.xy, z, 0));
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
        integratedVol[uint3(DTid.xy, z)] = float4(accumInscatter, accumTransmittance);
    }
}
