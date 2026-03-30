/// BRDF Integration LUT compute shader — generates split-sum scale+bias+Ems terms.
///
/// Precomputes a 2D lookup table where:
///   - X axis = NdotV (cosine of view angle)
///   - Y axis = roughness
///   - Output R = scale factor for F0 (Ess scale)
///   - Output G = bias (added to F0 * scale) (Ess bias)
///   - Output B = multi-scatter energy compensation Ems = 1 - (R + G)
///
/// The LUT is used at runtime as:
///   Ess = F0 * lut.r + lut.g                     (single-scatter)
///   Ems = lut.b                                    (multi-scatter complement)
///   Fms = F0 * (1 - Ess) / max(Ess, 1e-4)        (multi-scatter Fresnel)
///   energyCompensation = 1.0 + Fms * Ems / (1 - Ems) (Turquin 2019)
///
/// Uses 1024 Hammersley samples with VNDF importance sampling for lower
/// variance at grazing angles compared to standard NDF sampling.
///
/// Output format: RGBA16F (R=scale, G=bias, B=Ems, A=unused).
///
/// @ref Karis 2013 "Real Shading in Unreal Engine 4" — split-sum approximation
/// @ref Heitz 2018 "Sampling the GGX Distribution of Visible Normals" — VNDF sampling
/// @ref Turquin 2019 "Practical Multiple Scattering Compensation for Microfacet Models" — Ems

#include "../include/ibl.hlsli"
#include "../include/pbr_common.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants at b0 — output dimensions for UV calculation.
cbuffer RootConstants : register(b0) {
    uint outputWidth;
    uint outputHeight;
};

RWTexture2D<float4> outputLUT : register(u0);

// ─────────────────────────────────────────────────────────────────────────────
// Geometry Term for IBL
// ─────────────────────────────────────────────────────────────────────────────

/// Smith geometry term with IBL k remapping.
///
/// For IBL, k = roughness^2 / 2 (different from direct lighting where k = roughness^2 / 2
/// but roughness is remapped as (roughness+1)^2 / 8 for analytic lights).
/// Here we use the simpler IBL formulation.
///
/// # Parameters
/// - NdotV: saturated dot(N, V).
/// - NdotL: saturated dot(N, L).
/// - roughness: perceptual roughness [0, 1].
///
/// # Returns
/// Combined masking-shadowing attenuation in (0, 1].
float GeometrySmithIBL(float NdotV, float NdotL, float roughness) {
    float a = roughness;
    float k = (a * a) / 2.0f;

    float ggxV = NdotV / (NdotV * (1.0f - k) + k);
    float ggxL = NdotL / (NdotL * (1.0f - k) + k);

    return ggxV * ggxL;
}

// ─────────────────────────────────────────────────────────────────────────────
// BRDF Integration
// ─────────────────────────────────────────────────────────────────────────────

/// Integrates the split-sum BRDF for a given NdotV and roughness.
///
/// Uses VNDF importance sampling for lower variance at grazing angles.
/// Computes multi-scatter energy compensation (Ems) in the B channel.
///
/// @ref Heitz 2018 "Sampling the GGX Distribution of Visible Normals"
/// @ref Turquin 2019 "Practical Multiple Scattering Compensation for Microfacet Models"
///
/// # Returns
/// float3(scale, bias, Ems) — split-sum terms + multi-scatter complement.
float3 IntegrateBRDF(float NdotV, float roughness) {
    float3 V;
    V.x = sqrt(1.0f - NdotV * NdotV);
    V.y = 0.0f;
    V.z = NdotV;

    float alpha = roughness * roughness;

    float scale = 0.0f;
    float bias  = 0.0f;

    static const uint SAMPLE_COUNT = 1024u;

    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);

        /// @ref Heitz 2018 — VNDF sampling produces half-vectors proportional to
        /// D(h)*G1(v,h), drastically reducing variance at grazing angles.
        float3 H = ImportanceSampleGGX_VNDF(Xi, V, alpha, alpha);
        float3 L = normalize(2.0f * dot(V, H) * H - V);

        float NdotL = max(L.z, 0.0f);
        float NdotH = max(H.z, 0.0f);
        float VdotH = max(dot(V, H), 0.0f);

        if (NdotL > 0.0f) {
            float G    = GeometrySmithIBL(max(V.z, 0.0f), NdotL, roughness);
            float G_Vis = (G * VdotH) / (NdotH * max(V.z, 0.001f));
            float Fc   = Pow5(1.0f - VdotH);

            scale += (1.0f - Fc) * G_Vis;
            bias  += Fc * G_Vis;
        }
    }

    scale /= float(SAMPLE_COUNT);
    bias  /= float(SAMPLE_COUNT);

    /// @ref Turquin 2019 — Ems = 1 - Ess, where Ess = scale + bias (single-scatter energy)
    float Ess = scale + bias;
    float Ems = 1.0f - Ess;

    return float3(scale, bias, Ems);
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= outputWidth || dtid.y >= outputHeight) return;

    // Map pixel coordinates to NdotV and roughness
    // Add 0.5 for pixel center sampling
    float NdotV     = (float(dtid.x) + 0.5f) / float(outputWidth);
    float roughness = (float(dtid.y) + 0.5f) / float(outputHeight);

    // Clamp NdotV away from zero to avoid division instabilities
    NdotV = max(NdotV, 0.001f);
    // Clamp roughness to avoid degenerate GGX lobe at roughness=0
    roughness = max(roughness, MIN_ROUGHNESS);

    float3 result = IntegrateBRDF(NdotV, roughness);
    outputLUT[dtid.xy] = float4(result, 0.0f);
}
