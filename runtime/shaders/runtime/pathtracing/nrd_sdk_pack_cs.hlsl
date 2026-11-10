#include "nrd_sdk_common.hlsli"

/// Packs the stage-2 path-tracer signal into the NVIDIA NRD front-end layout:
/// IN_MV (screen pixels, previous - current), IN_NORMAL_ROUGHNESS, IN_VIEWZ,
/// IN_DIFF_RADIANCE_HITDIST (REBLUR: YCoCg + normalized hit distance) and
/// IN_SPEC_RADIANCE_HITDIST (RELAX: linear + raw hit distance). Diffuse is
/// demodulated by the primary albedo and specular by the primary F0, exactly
/// as nrd_common.hlsli (NrdCurrent) does, so nrd_sdk_modulate_cs.hlsl can
/// remodulate with the same floors.
/// @ref NRD.hlsli v4.17.3 "INPUT PARAMETERS"; NRDDescs.h ResourceType
/// @constants 8 = tile; 1e-3 = demodulation floor (nrd_common.hlsli);
/// 1.0 = diffuse roughness for the hit-distance normalization (NRD: isDiffuse ? 1 : roughness)
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= width || tid.y >= height) return;
    int2 p = int2(tid.xy);
    Texture2D<float4> guides = ResourceDescriptorHeap[guideSrv];
    Texture2D<float4> diffuse = ResourceDescriptorHeap[diffuseSrv];
    Texture2D<float4> albedo = ResourceDescriptorHeap[albedoSrv];
    Texture2D<float4> specular = ResourceDescriptorHeap[specularSrv];
    Texture2D<float4> modulation = ResourceDescriptorHeap[modulationSrv];
    Texture2D<float4> positions = ResourceDescriptorHeap[virtualPositionSrv];
    Texture2D<float2> velocity = ResourceDescriptorHeap[velocitySrv];
    RWTexture2D<float2> motion = ResourceDescriptorHeap[motionUav];
    RWTexture2D<float4> normalRoughness = ResourceDescriptorHeap[normalRoughnessUav];
    RWTexture2D<float> viewZOut = ResourceDescriptorHeap[viewZUav];
    RWTexture2D<float4> diffuseInput = ResourceDescriptorHeap[diffuseInputUav];
    RWTexture2D<float4> specularInput = ResourceDescriptorHeap[specularInputUav];

    // NRD: pixelUvPrev = pixelUv + mv * motionVectorScale (scale = 1 / extent).
    // The GBuffer velocity is current - previous in pixels (nrd_math NrdHistoryUV).
    motion[p] = -velocity.Load(int3(p, 0));

    float4 guide = guides.Load(int3(p, 0));
    if (guide.z <= 0.0) {
        // Primary miss: background stays in the unfiltered residual. A viewZ
        // beyond the denoising range makes NRD skip the pixel entirely.
        normalRoughness[p] = float4(0.0, 0.0, 1.0, 1.0);
        viewZOut[p] = skyViewZ;
        diffuseInput[p] = 0.0;
        specularInput[p] = 0.0;
        return;
    }
    // Primary hit from the DXR guide depth (reversed-Z clip depth), as
    // nrd_reproject_cs.hlsl reconstructs it.
    float2 uv = (float2(p) + 0.5) / float2(width, height);
    float4 world = mul(float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), guide.z, 1.0), inverseViewProj);
    world /= world.w;
    float viewZ = mul(float4(world.xyz, 1.0), worldToView).z;
    normalRoughness[p] = NrdSdkPackNormalRoughness(OctDecode(guide.xy), guide.w);
    viewZOut[p] = viewZ;

    // The unfolded virtual point keeps the secondary segment length: its
    // distance from the primary hit is the first-bounce hit distance (10000
    // for an environment miss, 0 when the path ended at the primary vertex,
    // which NRD reads as "no data").
    float4 virtualPoint = positions.Load(int3(p, 0));
    float hitDist = virtualPoint.w > 0.0 ? length(virtualPoint.xyz - world.xyz) : 0.0;
    hitDist = (isnan(hitDist) || isinf(hitDist)) ? 0.0 : clamp(hitDist, 0.0, 65504.0);
    float3 diff = NrdSdkSanitize(diffuse.Load(int3(p, 0)).rgb / max(albedo.Load(int3(p, 0)).rgb, 1e-3));
    float3 spec = NrdSdkSanitize(specular.Load(int3(p, 0)).rgb / max(modulation.Load(int3(p, 0)).rgb, 1e-3));
    float normHitDist = NrdSdkNormHitDist(hitDist,
        NrdSdkHitDistNormalization(viewZ, hitDistA, hitDistB, hitDistC, 1.0));
    diffuseInput[p] = float4(NrdSdkLinearToYCoCg(diff), normHitDist);
    specularInput[p] = float4(spec, hitDist);
}
