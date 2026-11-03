/// Hit-vertex sun specular for the GI gather and the radiance-cache probe writer
/// (docs/analysis/lumen-radiance-cache-stability/06-hit-vertex-specular-gather.md, section 4).
///
/// The gather shades every secondary hit with the sun's Lambert term only; the sun's GGX lobe at
/// the hit (the grazing-Fresnel glint of sun-lit floors and walls) is 20% of the path tracer's
/// indirect and 33.5% of the real-time deficit (_evidence/gi/indirect-underlit/report.md section 4a).
/// This header evaluates that lobe for the ray the gather already traced (V = -rayDir, L = the sun,
/// N = the hit normal) and the D7 energy coupling of the path tracer (pathtracer_shared.hlsli
/// EvaluatePBR): the sun Lambert is weighted by `PathDiffuseEnergy(E_spec, NoV)` with E_spec = the
/// specular directional albedo read from the IBL BRDF LUT (`F0 * A + B`), so the hit's directional
/// albedo for the sun stays <= 1 - conserved, never amplified. The analytic Karis fit is NOT used for
/// the weight (D7 rejected it, docs/analysis/path-tracing/10-d7-diffuse-specular-energy.md).
///
/// Only compiled into the `UZ_GI_HIT_SPECULAR=1` permutations (r.GI.FinalGather.HitSpecular); the
/// default pipelines preprocess to the text before this header existed. The hit carries no roughness
/// or metallic (GIMeshMaterial / the SDF clipmap), so the caller passes the scene-constant roughness
/// CVar and the dielectric F0; metals stay under-lit, never over-lit (doc 06 section 8.4).
///
/// @ref Walter et al. 2007 "Microfacet Models for Refraction through Rough Surfaces" (GGX)
/// @ref Heitz 2014 "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
/// @ref Karis 2013 "Real Shading in Unreal Engine 4" (the split-sum LUT the weight reads)
#ifndef UZ_GI_HIT_SPECULAR_HLSLI
#define UZ_GI_HIT_SPECULAR_HLSLI

// brdf.hlsli (D_GGX, V_SmithGGXCorrelated, F_Schlick, RoughnessToAlpha) reaches every includer
// through ../lighting/ibl.hlsli; PathDiffuseEnergy is the path tracer's D7 coupling.
#include "../material/brdf.hlsli"
#include "../pathtracing/pathtracer_math.hlsli"

/// @constants F0 = 0.04: `ComputeF0(albedo, 0)`, the dielectric reflectance (the hit has no metallic;
/// Sponza's is 0 everywhere). MaxBrdfCos = 1.0: the cap on `D * Vis * NoL` (the Fresnel-free part of
/// f_spec * cos) so one hit returns at most `F * E_sun * visibility` <= `E_sun` (a white Lambertian
/// returns E_sun / pi). The exhaustive maximum over the hemisphere (not the H = N case doc 06
/// section 8.1 tabulated) is 1.24 at the default roughness 0.7 (V at 89.5 deg, L at 82 deg on the
/// opposite side), so the cap trims that grazing sliver by at most 19%, is a no-op from roughness
/// 0.74 up and bounds the CVar's low end (32 at 0.4) - _evidence/gi/hit-vertex-specular/firefly_clamp.txt.
static const float3 kGiHitSpecularF0 = float3(0.04, 0.04, 0.04);
static const float kGiHitSpecularMaxBrdfCos = 1.0;

struct GiHitSunSpecular {
    float3 specularCos;    ///< f_spec(V, L) * NoL (the sun lobe toward the ray origin); zero when off.
    float3 diffuseWeight;  ///< Per-channel (1 - E_spec - reserve) weight on the sun Lambert; one when off.
};

/// The sun lobe at a hit, and the D7 weight for the hit's sun Lambert.
///
/// `N` unit, `V` = -rayDir with dot(N, V) > 0 (every site rejects back faces first), `L` the unit sun
/// direction, `NoL` the site's saturated dot(N, L), `roughness` the scene constant (CVar), `F0` the
/// dielectric reflectance, `lutSrv` the IBL BRDF LUT (RG16F: U = NoV, V = roughness, R = scale,
/// G = bias; 0xffffffff before the IBL bake) and `lutSampler` a linear clamp sampler. Identity
/// (no lobe, weight 1 = today's shading) when the LUT is unbound, roughness <= 0, NoV <= 0 or NoL <= 0,
/// so an unbound LUT degrades to today's shading, never to a mis-weighted one.
GiHitSunSpecular EvalGiHitSunSpecular(float3 N, float3 V, float3 L, float NoL, float roughness,
                                      float3 F0, uint lutSrv, SamplerState lutSampler) {
    GiHitSunSpecular result;
    result.specularCos = 0.0;
    result.diffuseWeight = 1.0;
    const float NoV = saturate(dot(N, V));
    if (lutSrv == 0xffffffffu || roughness <= 0.0 || NoV <= 0.0 || NoL <= 0.0) return result;

    // The lobe: GGX with the height-correlated Smith visibility and Schlick Fresnel (F90 = 1), the
    // same expression the deferred sun and the path tracer's primary vertex evaluate.
    const float3 H = normalize(V + L);
    const float NoH = saturate(dot(N, H));
    const float VoH = saturate(dot(V, H));
    const float alpha = RoughnessToAlpha(roughness);
    const float dv = min(D_GGX(alpha, NoH) * V_SmithGGXCorrelated(alpha, NoV, NoL) * NoL,
                         kGiHitSpecularMaxBrdfCos);
    result.specularCos = dv * F_Schlick(F0, VoH);

    // The D7 coupling (pathtracer_shared.hlsli EvaluatePBR): the LUT's directional albedo of this
    // same lobe, clamped to texel centres, and the Lambert's remaining energy budget per channel.
    Texture2D<float2> brdfLut = ResourceDescriptorHeap[lutSrv];
    uint lutWidth, lutHeight;
    brdfLut.GetDimensions(lutWidth, lutHeight);
    const float2 halfTexel = 0.5 / float2(lutWidth, lutHeight);
    const float2 ab = brdfLut.SampleLevel(lutSampler,
        clamp(float2(NoV, roughness), halfTexel, 1.0 - halfTexel), 0);
    const float3 specularAlbedo = F0 * ab.x + ab.y;
    result.diffuseWeight = float3(PathDiffuseEnergy(specularAlbedo.r, NoV),
                                  PathDiffuseEnergy(specularAlbedo.g, NoV),
                                  PathDiffuseEnergy(specularAlbedo.b, NoV));
    return result;
}

#ifdef UZ_GI_HIT_PERMESH
// ─── r.GI.HitMaterial.PerMesh: the same lobe with the LUT's directional albedo exposed ───
// (docs/analysis/lumen-radiance-cache-stability/08-per-mesh-hit-material.md section 6). Only the
// UZ_GI_HIT_PERMESH=1 gather permutations compile this block; EvalGiHitSunSpecular above is left verbatim
// so the HitSpecular-only permutations preprocess to their committed text.

/// @constants MaxBrdfCos = 3.2: the cap on `D * Vis * NoL` for the per-mesh lobe, sized to the lobe's
/// normal-incidence peak at the r.GI.HitMaterial.PerMesh.MinRoughness default 0.4 (1 / (pi alpha^2) * 0.25 = 3.1
/// for alpha = 0.16), so a hit at the floor keeps its energy except in the grazing sliver (peak 10.6 at NoV 0.25,
/// trimmed to 3.2) and one hit returns at most 3.2 * F * E_sun * visibility. Doc 06's cap of 1.0 (kept verbatim for
/// the scene-constant lobe above) was a no-op at its 0.7 roughness but removes 20-45% of a 0.4 lobe's energy and
/// 74-87% of a 0.2 lobe's (_evidence/gi/per-mesh-hit/cap_energy.txt), which is exactly the glint that carries a
/// metal's bounce: measured against the path tracer, 1.0 left the metal-hit ROIs at ~60% of the oracle (no lift) while
/// 3.2 closes 77% / 56% of the gap with the per-ray p99.9 at 1.24x, no sparkles (REPORT.md section 6).
static const float kGiHitPerMeshMaxBrdfCos = 3.2;

struct GiHitMaterialSpecular {
    float3 specularCos;    ///< f_spec(V, L) * NoL (the sun lobe toward the ray origin); zero when off.
    float3 diffuseWeight;  ///< Per-channel (1 - E_spec - reserve) weight on the hit's Lambert terms; one when off.
    float3 specularAlbedo; ///< E_spec = F0 * A + B at (NoV, roughness), the lobe's hemispherical albedo; zero when off.
};

/// The sun lobe at a hit with the hit's own roughness and F0 (`ComputeF0(baseColor, metallic)`), the D7
/// weight for its Lambert terms, and `E_spec` itself so the probe-irradiance term can be coupled the same
/// way: `(diffuseAlbedo * diffuseWeight + specularAlbedo) * E/pi` is this BRDF integrated against uniform
/// incident radiance (the assumption the Lambert already makes of the SH probe), so a metal in shadow
/// reflects the probe irradiance instead of going black. The early-outs are split: a sun below the hit's
/// horizon (NoL <= 0, the common shadowed case) zeroes the lobe only and still reads the LUT, which depends
/// on NoV and roughness, not on L; an unbound LUT, roughness <= 0 or NoV <= 0 is the full identity.
GiHitMaterialSpecular EvalGiHitMaterialSpecular(float3 N, float3 V, float3 L, float NoL, float roughness,
                                                float3 F0, uint lutSrv, SamplerState lutSampler) {
    GiHitMaterialSpecular result;
    result.specularCos = 0.0;
    result.diffuseWeight = 1.0;
    result.specularAlbedo = 0.0;
    const float NoV = saturate(dot(N, V));
    if (lutSrv == 0xffffffffu || roughness <= 0.0 || NoV <= 0.0) return result;

    if (NoL > 0.0) {
        // The lobe: GGX with the height-correlated Smith visibility and Schlick Fresnel (F90 = 1), capped at
        // the per-mesh bound (one hit returns at most kGiHitPerMeshMaxBrdfCos * F * E_sun * visibility).
        const float3 H = normalize(V + L);
        const float NoH = saturate(dot(N, H));
        const float VoH = saturate(dot(V, H));
        const float alpha = RoughnessToAlpha(roughness);
        const float dv = min(D_GGX(alpha, NoH) * V_SmithGGXCorrelated(alpha, NoV, NoL) * NoL,
                             kGiHitPerMeshMaxBrdfCos);
        result.specularCos = dv * F_Schlick(F0, VoH);
    }

    // The D7 coupling (pathtracer_shared.hlsli EvaluatePBR): the LUT's directional albedo of this same
    // lobe, clamped to texel centres, and the Lambert's remaining energy budget per channel.
    Texture2D<float2> brdfLut = ResourceDescriptorHeap[lutSrv];
    uint lutWidth, lutHeight;
    brdfLut.GetDimensions(lutWidth, lutHeight);
    const float2 halfTexel = 0.5 / float2(lutWidth, lutHeight);
    const float2 ab = brdfLut.SampleLevel(lutSampler,
        clamp(float2(NoV, roughness), halfTexel, 1.0 - halfTexel), 0);
    result.specularAlbedo = F0 * ab.x + ab.y;
    result.diffuseWeight = float3(PathDiffuseEnergy(result.specularAlbedo.r, NoV),
                                  PathDiffuseEnergy(result.specularAlbedo.g, NoV),
                                  PathDiffuseEnergy(result.specularAlbedo.b, NoV));
    return result;
}
#endif // UZ_GI_HIT_PERMESH

#endif // UZ_GI_HIT_SPECULAR_HLSLI
