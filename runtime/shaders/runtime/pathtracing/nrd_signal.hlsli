#ifndef UZ_NRD_SIGNAL
#define UZ_NRD_SIGNAL
#include "nrd_math.hlsli"
#include "../common/encoding.hlsli"
cbuffer NrdSignalRoot : register(b2) {
    uint nrdEnabled, nrdDiffuseUav, nrdFractionUav, nrdAlbedoUav;
    uint nrdSpecularUav, nrdModulationUav, nrdVirtualPositionUav, nrdGuideUav;
};
#ifndef RESTIR_COMPUTE
// Primary diffuse attribution uses f_diffuse/f_total at the SAME sampled
// direction, preserving the mixture estimator and its original radiance.
/// @ref Veach 1997 "Robust Monte Carlo Methods for Light Transport Simulation"
/// @constants 1e-20 guards zero-contribution color channels; no estimator clamp.
float3 NrdDiffuseBRDF(float3 N, float3 V, float3 L, float3 albedo, float metallic) {
    if (dot(N, V) <= 0.0 || dot(N, L) <= 0.0) return 0.0;
    float3 H = normalize(V + L);
    float3 F = F_Schlick(ComputeF0(albedo, metallic), saturate(dot(V, H)));
    return (1.0 - F) * (1.0 - metallic) * albedo * saturate(dot(N, L)) / PI;
}
void NrdAddDiffuse(float3 contribution) {
    RWTexture2D<float4> diffuse = ResourceDescriptorHeap[nrdDiffuseUav];
    uint2 pixel = DispatchRaysIndex().xy;
    diffuse[pixel] = float4(diffuse[pixel].rgb + contribution, 1.0);
}
float3 NrdFraction() {
    RWTexture2D<float4> fraction = ResourceDescriptorHeap[nrdFractionUav];
    return fraction[DispatchRaysIndex().xy].rgb;
}
void NrdAddSpecular(float3 contribution) {
    RWTexture2D<float4> specular = ResourceDescriptorHeap[nrdSpecularUav];
    uint2 pixel = DispatchRaysIndex().xy;
    specular[pixel] = float4(specular[pixel].rgb + contribution, 1.0);
}
float3 NrdVirtualPosition(float3 position, float3 direction, float3 normal, float distance) {
    float d = dot(direction, normal);
    return float3(NrdVirtualComponent(position.x, direction.x, normal.x, d, distance),
        NrdVirtualComponent(position.y, direction.y, normal.y, d, distance),
        NrdVirtualComponent(position.z, direction.z, normal.z, d, distance));
}
#endif
#endif
