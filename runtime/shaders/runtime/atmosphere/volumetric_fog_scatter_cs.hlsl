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
    float3 emissive;
};

/// @ref Hillaire 2015 — exponential depth distribution (must match inject)
float SliceToDepth(uint slice, uint numSlices)
{
    float t = (float(slice) + 0.5) / float(numSlices);
    return nearZ * pow(farZ / max(nearZ, 0.01), t);
}

/// Compute slice thickness for the exponential distribution.
float SliceThickness(uint slice, uint numSlices)
{
    float d0 = (slice > 0) ? SliceToDepth(slice - 1, numSlices) : 0.0;
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
    for (uint z = 0; z < gridZ; ++z) {
        float4 sliceData = inscatteringVol.Load(int4(DTid.xy, z, 0));
        float3 sliceInscatter = sliceData.rgb;
        float  sliceExtinction = sliceData.a;

        // Slice thickness in world units (cm → convert to meaningful distance)
        float thickness = SliceThickness(z, gridZ);

        // Transmittance through this slice
        // @ref Beer-Lambert law: T = exp(-extinction × distance)
        float sliceTransmittance = exp(-sliceExtinction * thickness);

        // Energy-conservative integration (Frostbite method):
        // @ref Hillaire 2015 Eq. 5 — analytical integration within each slice
        // S_int = (S - S * T_step) / max(extinction, epsilon)
        float3 Sint = (sliceInscatter - sliceInscatter * sliceTransmittance)
                    / max(sliceExtinction, 1e-6);

        accumInscatter += accumTransmittance * Sint;
        accumTransmittance *= sliceTransmittance;

        // Write integrated result for this slice
        integratedVol[uint3(DTid.xy, z)] = float4(accumInscatter, accumTransmittance);
    }
}
