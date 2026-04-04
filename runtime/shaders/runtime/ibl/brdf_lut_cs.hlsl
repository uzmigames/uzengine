/// UzEngine BRDF integration LUT — split-sum approximation.
/// @ref Karis 2013 "Real Shading in Unreal Engine 4" — IntegrateBRDF
/// @ref Falcor IsotropicGGX.slang — GGX VNDF sampling reference
///
/// Computes a 2D LUT where:
///   U = NdotV [0,1], V = roughness [0,1]
///   R = scale (F0 multiplier), G = bias (additive term)
/// Used by split-sum IBL: specular = prefilteredColor * (F0 * R + G)
///
/// Root constants at b0: { width, height }
/// Output: RWTexture2D<float2> at u0 (via descriptor table slot 4)

#include "../common/constants.hlsli"
#include "../common/sampling.hlsli"
#include "../material/brdf.hlsli"

cbuffer RootConstants : register(b0) {
    uint outputWidth;
    uint outputHeight;
};

#ifdef UZ_BINDLESS
RWTexture2D<float2> outputLUT : register(u0);
#else
RWTexture2D<float2> outputLUT : register(u0);
#endif

/// @ref Karis 2013 — IntegrateBRDF function
/// Importance-samples the GGX NDF and integrates the split-sum BRDF terms.
float2 IntegrateBRDF(float NdotV, float roughness) {
    float alpha = roughness * roughness;

    // View vector in tangent space (N = (0,0,1))
    float3 V;
    V.x = sqrt(1.0 - NdotV * NdotV); // sin(theta)
    V.y = 0.0;
    V.z = NdotV;                       // cos(theta)

    float scale = 0.0; // F0 multiplier
    float bias  = 0.0; // additive term

    const uint SAMPLE_COUNT = 1024;

    for (uint i = 0; i < SAMPLE_COUNT; ++i) {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H  = ImportanceSampleGGX(Xi, roughness);
        float3 L  = 2.0 * dot(V, H) * H - V;

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0) {
            // @ref Karis 2013 — Smith G visibility for IBL integration
            float G = G_SmithGGXCorrelated(max(alpha, kMinGGXAlpha), NdotV, NdotL);
            // G_Vis = G * VdotH / (NdotH * NdotV) — geometric visibility term
            float G_Vis = (G * VdotH) / max(NdotH * NdotV, 1e-4);
            // Fc = (1 - VdotH)^5 — Fresnel power term
            float Fc = Pow5(1.0 - VdotH);

            scale += (1.0 - Fc) * G_Vis;
            bias  += Fc * G_Vis;
        }
    }

    return float2(scale, bias) / float(SAMPLE_COUNT);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= outputWidth || DTid.y >= outputHeight) return;

    float NdotV    = (float(DTid.x) + 0.5) / float(outputWidth);
    float roughness = (float(DTid.y) + 0.5) / float(outputHeight);

    // Clamp NdotV away from 0 to avoid singularities
    NdotV = max(NdotV, 0.001);

    float2 result = IntegrateBRDF(NdotV, roughness);
    outputLUT[DTid.xy] = result;
}
