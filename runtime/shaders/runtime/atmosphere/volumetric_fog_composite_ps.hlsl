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
    uint   inscatteringSrvIdx;
    uint   integratedUavIdx;
    uint   _integratedSrvIdx;
    uint   _depthSrvIdx;
    uint   frameIndex;
    uint   gridX;
    uint   gridY;
    uint   gridZ;
    float  extinctionScale;
    float  _emissiveR;                // scalars: a float3 here would be padded to offset 192 (C++ has 184)
    float  _emissiveG;
    float  _emissiveB;
    float  fogStartDistance;
    float  nearFadeInDistance;
    float  cameraFarZ;                // camera far plane (cm): finite reversed-Z linearisation
    uint   sunShadowEnabled;
    uint   debugMode;                 // r.VolumetricFog.Debug: 1 = (view depth cm, slice coordinate, transmittance), 2 = (raw depth, in-scatter r, g)
};

SamplerState linearSampler : register(s0);

/// @ref Hillaire 2015 — exponential depth distribution (must match inject/scatter)
/// @ref UE5 — startZ = max(nearZ, fogStartDistance)
float SliceToDepth(uint slice, uint numSlices)
{
    float startZ = max(nearZ, fogStartDistance);
    float t = (float(slice) + 0.5) / float(numSlices);
    return startZ * pow(farZ / max(startZ, 0.01), t);
}

/// Convert a view-space depth to UVW in the froxel volume.
/// XY = screen UV, Z = normalized depth mapped to exponential slice distribution.
/// Reversed-Z device depth (near 1, far 0; Mat4::Perspective) to view depth in cm, the form GTAO / SSR use.
/// depth_common.hlsli's LinearizeDepthReversedZ carries the standard-Z formula (side finding of block B.4:
/// it returns ~nearZ for every depth; the contact shadows are its other user).
float ViewDepthFromReversedZ(float depth, float nearPlane, float farPlane)
{
    return (nearPlane * farPlane) / (nearPlane + depth * (farPlane - nearPlane));
}

float3 DepthToFroxelUVW(float2 screenUV, float viewDepthCm)
{
    // Must match the inject/scatter depth distribution exactly.
    // depth = startZ * (farZ/startZ)^t  →  t = log(depth/startZ) / log(farZ/startZ)
    float startZ = max(nearZ, fogStartDistance);
    float t = log(max(viewDepthCm, startZ) / startZ) / log(farZ / startZ);
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

    // Linearize reversed-Z depth → view-space distance in cm with the camera's finite far plane
    // (nearZ / depth assumes an infinite far plane: 11 % off at 10 m, 2x at 50 m with far 10000).
    float viewDepthCm = ViewDepthFromReversedZ(depth, nearZ, cameraFarZ);

    // Sample integrated froxel volume
    float3 uvw = DepthToFroxelUVW(input.texcoord, viewDepthCm);

#ifdef UZ_BINDLESS
    Texture3D<float4> integratedVol = ResourceDescriptorHeap[integratedSrvIdx];
    float4 fogData = integratedVol.SampleLevel(linearSampler, uvw, 0);
#else
    float4 fogData = float4(0, 0, 0, 1); // No volumetric fog on SM5
#endif

    if (debugMode == 1u) return float4(viewDepthCm, uvw.z, fogData.a, 1.0);   // opaque: replaces the HDR pixel
    if (debugMode == 2u) return float4(depth, fogData.r, fogData.g, 1.0);
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
