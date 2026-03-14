/// BRDF Integration LUT compute shader — generates split-sum scale+bias terms.
///
/// Precomputes a 2D lookup table where:
///   - X axis = NdotV (cosine of view angle)
///   - Y axis = roughness
///   - Output R = scale factor for F0
///   - Output G = bias (added to F0 * scale)
///
/// The LUT is used at runtime as: envBRDF = F0 * lut.r + lut.g
///
/// Uses 1024 Hammersley samples per pixel with GGX importance sampling
/// for high-quality integration. Output format: R16G16_FLOAT (512x512).
///
/// # References
/// - Karis 2013, "Real Shading in Unreal Engine 4" — split-sum approximation
/// - Epic Games, "Real Shading in Unreal Engine 4" course notes

#include "../include/ibl.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants at b0 — output dimensions for UV calculation.
cbuffer RootConstants : register(b0) {
    uint outputWidth;
    uint outputHeight;
};

RWTexture2D<float2> outputLUT : register(u0);

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
/// Performs Monte Carlo integration of the GGX specular BRDF, separating
/// the F0-dependent scale term from the F0-independent bias term.
///
/// # Parameters
/// - NdotV:     cosine of view angle [0, 1].
/// - roughness: perceptual roughness [0, 1].
///
/// # Returns
/// float2(scale, bias) — the two terms of the split-sum approximation.
float2 IntegrateBRDF(float NdotV, float roughness) {
    // Reconstruct V from NdotV (assume N = (0,0,1) in tangent space)
    float3 V;
    V.x = sqrt(1.0f - NdotV * NdotV); // sin(theta)
    V.y = 0.0f;
    V.z = NdotV;                        // cos(theta)

    float3 N = float3(0.0f, 0.0f, 1.0f);

    float scale = 0.0f;
    float bias  = 0.0f;

    static const uint SAMPLE_COUNT = 1024u;

    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H  = ImportanceSampleGGX(Xi, N, roughness);
        float3 L  = normalize(2.0f * dot(V, H) * H - V);

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

    return float2(scale, bias) / float(SAMPLE_COUNT);
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

    outputLUT[dtid.xy] = IntegrateBRDF(NdotV, roughness);
}
