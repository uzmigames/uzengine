/// UzEngine Froxel Volumetric Fog — Composite (pixel shader).
///
/// Fullscreen pass that samples the integrated froxel volume at scene depth
/// and applies inscattering + transmittance via alpha blending.
///
/// PSO uses SrcAlpha / InvSrcAlpha blending:
///   output.rgb = inscattering (fog color)
///   output.a   = 1 - transmittance (fog opacity)
/// Blend result: final = inscattering + sceneColor * transmittance
///
/// @ref Hillaire 2015 "Physically Based & Unified Volumetric Rendering in Frostbite"

#include "../common/constants.hlsli"

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

#ifdef UZ_BINDLESS
cbuffer FogCompositeIndices : register(b0) {
    uint depthSrvIdx;
    uint integratedSrvIdx;
};
#else
Texture2D<float> sceneDepth : register(t0);
#endif

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
    uint   _integratedSrvIdx;
    uint   _depthSrvIdx;
    uint   frameIndex;
    uint   gridX;
    uint   gridY;
    uint   gridZ;
};

SamplerState linearSampler : register(s0);

/// @ref Hillaire 2015 — exponential depth distribution (must match inject/scatter)
float SliceToDepth(uint slice, uint numSlices)
{
    float t = (float(slice) + 0.5) / float(numSlices);
    return nearZ * pow(farZ / max(nearZ, 0.01), t);
}

/// Convert a view-space depth to UVW in the froxel volume.
/// XY = screen UV, Z = normalized depth mapped to exponential slice distribution.
float3 DepthToFroxelUVW(float2 screenUV, float viewDepthCm)
{
    // Invert the exponential distribution:
    // depth = nearZ * (farZ/nearZ)^t  →  t = log(depth/nearZ) / log(farZ/nearZ)
    float t = log(max(viewDepthCm, nearZ) / nearZ) / log(farZ / nearZ);
    return float3(screenUV, saturate(t));
}

float4 main(FSInput input) : SV_Target
{
    int2 px = int2(input.positionCS.xy);

    // Read scene depth
#ifdef UZ_BINDLESS
    Texture2D<float> depthTex = ResourceDescriptorHeap[depthSrvIdx];
    float depth = depthTex.Load(int3(px, 0)).r;
#else
    float depth = sceneDepth.Load(int3(px, 0)).r;
#endif

    // Skip sky pixels — fog applies to geometry only
    // (Sky fog is handled separately by the height fog / atmosphere passes)
    if (depth <= 0.0001) discard;

    // Linearize reversed-Z depth → view-space distance in cm
    // reversed-Z: near=1, far=0 → linear = near / depth
    float viewDepthCm = nearZ / max(depth, 1e-7);

    // Sample integrated froxel volume
    float3 uvw = DepthToFroxelUVW(input.texcoord, viewDepthCm);

#ifdef UZ_BINDLESS
    Texture3D<float4> integratedVol = ResourceDescriptorHeap[integratedSrvIdx];
    float4 fogData = integratedVol.SampleLevel(linearSampler, uvw, 0);
#else
    float4 fogData = float4(0, 0, 0, 1); // No volumetric fog on SM5
#endif

    float3 inscattering = fogData.rgb;
    float  transmittance = fogData.a;

    // Output for alpha blending (SrcAlpha / InvSrcAlpha):
    // finalColor = output.rgb * output.a + sceneColor * (1 - output.a)
    //            = inscattering * (1 - transmittance) + sceneColor * transmittance
    // Wait — that's not right. We want:
    //   finalColor = sceneColor * transmittance + inscattering
    // With SrcAlpha/InvSrcAlpha:
    //   finalColor = output.rgb * output.a + sceneColor * (1 - output.a)
    // So: output.a = 1 - transmittance, output.rgb = inscattering / (1 - transmittance)
    // But that requires division by potentially zero. Better:
    //
    // Use One/SrcAlpha blend mode instead:
    //   finalColor = output.rgb * 1 + sceneColor * output.a
    //   → output.rgb = inscattering, output.a = transmittance
    // This is SrcBlend=One, DstBlend=SrcAlpha.
    //
    // OR use SrcAlpha/InvSrcAlpha with the standard fog encoding:
    //   output.rgb = inscattering / max(1 - transmittance, epsilon)
    //   output.a = 1 - transmittance
    //   → finalColor = inscattering + sceneColor * transmittance ✓

    float opacity = 1.0 - transmittance;
    float3 premultInscatter = (opacity > 0.001)
        ? inscattering / opacity
        : float3(0, 0, 0);

    return float4(premultInscatter, opacity);
}
