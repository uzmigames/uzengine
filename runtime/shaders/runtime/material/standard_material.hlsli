/// UzEngine standard material evaluation.
///
/// Top-level header that dispatches to the correct shading model (default PBR,
/// clearcoat, cloth/sheen, subsurface, foliage, thin translucent). This is
/// the single entry point that lighting passes call to evaluate a material.
///
/// Equivalent to Falcor's StandardBSDF — same shading models, same BRDF
/// combinations, same energy conservation strategy.

#ifndef UZ_MATERIAL_STANDARD_MATERIAL_HLSLI
#define UZ_MATERIAL_STANDARD_MATERIAL_HLSLI

#include "../common/constants.hlsli"
#include "../common/math.hlsli"
#include "brdf.hlsli"
#include "diffuse.hlsli"
#include "specular.hlsli"
#include "sheen.hlsli"
#include "clearcoat.hlsli"
#include "subsurface.hlsli"

// ─── Shading Model IDs ──────────────────────────────────────────────────────

/// Standard metallic/dielectric PBR (Frostbite diffuse + GGX specular).
static const uint kShadingModelDefault          = 0;

/// Subsurface scattering (wrap diffuse + back lighting + GGX specular).
static const uint kShadingModelSubsurface       = 1;

/// Clearcoat (dual-layer: smooth dielectric coat over base PBR).
static const uint kShadingModelClearcoat        = 2;

/// Cloth (Charlie NDF + Neubelt visibility, replaces GGX specular).
static const uint kShadingModelCloth            = 3;

/// Foliage (two-sided diffuse + GGX specular).
static const uint kShadingModelFoliage          = 4;

/// Thin translucent (exponential transmission + GGX specular).
static const uint kShadingModelThinTranslucent  = 5;

// ─── Material Parameters ─────────────────────────────────────────────────────

/// Complete set of material parameters for all shading models.
/// Fields are set by InitMaterialParams and optionally overridden for
/// specialized models (clearcoat, sheen, subsurface).
struct MaterialParams
{
    // Core PBR
    float3 baseColor;
    float  metallic;
    float  roughness;
    float3 emissive;
    float  ao;

    // Derived (computed by InitMaterialParams)
    float3 f0;              // Specular F0 from baseColor + metallic
    float3 diffuseColor;    // baseColor * (1 - metallic)
    float3 specularColor;   // Alias for f0
    float  alpha;           // roughness^2 clamped to kMinGGXAlpha

    // Shading model selector
    uint   shadingModel;

    // Clearcoat layer
    float  clearcoatRoughness;
    float  clearcoatStrength;

    // Sheen / cloth layer
    float3 sheenColor;
    float  sheenRoughness;

    // Subsurface / translucency
    float3 subsurfaceColor;
    float  thickness;
};

/// Initialize material parameters and compute derived values.
/// @ref Burley 2012 "Physically Based Shading at Disney"
/// @constants kDielectricF0=0.04 (inside ComputeF0), kMinGGXAlpha=0.0064
/// @deviations NONE
///
/// @param baseColor    Surface base color (albedo for dielectrics, reflectance for metals).
/// @param metallic     Metalness in [0, 1].
/// @param roughness    Perceptual roughness in [0, 1].
/// @param emissive     Emissive color (HDR, additive).
/// @param ao           Ambient occlusion factor in [0, 1].
/// @param shadingModel One of kShadingModel* constants.
/// @return             Initialized MaterialParams with derived f0, diffuseColor, alpha.
MaterialParams InitMaterialParams(
    float3 baseColor,
    float  metallic,
    float  roughness,
    float3 emissive,
    float  ao,
    uint   shadingModel)
{
    MaterialParams mat;

    // Core PBR
    mat.baseColor   = baseColor;
    mat.metallic    = metallic;
    mat.roughness   = roughness;
    mat.emissive    = emissive;
    mat.ao          = ao;

    // Derived: specular F0 — dielectrics get 0.04, metals get albedo
    mat.f0            = ComputeF0(baseColor, metallic);
    mat.diffuseColor  = baseColor * (1.0f - metallic);
    mat.specularColor = mat.f0;
    mat.alpha         = RoughnessToAlpha(roughness);

    // Shading model
    mat.shadingModel = shadingModel;

    // Defaults for optional layers (caller overrides as needed)
    mat.clearcoatRoughness = 0.0f;
    mat.clearcoatStrength  = 0.0f;
    mat.sheenColor         = float3(0.0f, 0.0f, 0.0f);
    mat.sheenRoughness     = 0.0f;
    mat.subsurfaceColor    = float3(0.0f, 0.0f, 0.0f);
    mat.thickness          = 0.0f;

    return mat;
}

// ─── IBL Parameters ──────────────────────────────────────────────────────────

/// Parameters needed for the split-sum IBL approximation.
/// Extracted from MaterialParams so the IBL evaluation code doesn't need
/// the full material struct.
struct IBLParams
{
    float3 f0;
    float3 diffuseColor;
    float  roughness;
    float  NdotV;
};

/// Extract IBL evaluation parameters from a material.
/// @ref Karis 2013 "Real Shading in Unreal Engine 4" — split-sum approximation
/// @constants None
/// @deviations NONE
///
/// @param mat  Initialized material parameters.
/// @param NdotV Cosine of angle between normal and view direction.
/// @return     IBLParams for use with the split-sum lookup.
IBLParams GetIBLParams(MaterialParams mat, float NdotV)
{
    IBLParams ibl;
    ibl.f0           = mat.f0;
    ibl.diffuseColor = mat.diffuseColor;
    ibl.roughness    = mat.roughness;
    ibl.NdotV        = NdotV;
    return ibl;
}

// ─── Shading Model Evaluation ────────────────────────────────────────────────

/// Evaluate the default PBR shading model (Frostbite diffuse + GGX specular).
/// @ref Lagarde & de Rousiers 2014 "Moving Frostbite to PBR"
/// @ref Walter et al. 2007 "Microfacet Models for Refraction through Rough Surfaces"
/// @constants None (delegates to EvalFrostbiteDiffuse, EvalSpecularDVF)
/// @deviations NONE
float3 EvalDefaultModel(MaterialParams mat, float NdotV, float NdotL, float NdotH, float VdotH)
{
    float3 diffuse  = EvalFrostbiteDiffuse(mat.diffuseColor, mat.roughness, NdotV, NdotL, VdotH);
    float3 specular = EvalSpecularDVF(mat.alpha, NdotH, NdotV, NdotL, VdotH, mat.f0);
    return diffuse + specular;
}

/// Evaluate the clearcoat shading model.
/// The base layer is the standard PBR model attenuated by the clearcoat
/// Fresnel to conserve energy.
/// @ref Karis 2013 "Real Shading in Unreal Engine 4"
/// @ref Lagarde & de Rousiers 2014 "Moving Frostbite to PBR"
/// @constants kClearcoatF0=0.04, kClearcoatIOR=1.5
/// @deviations NONE
float3 EvalClearcoatModel(MaterialParams mat, float NdotV, float NdotL, float NdotH, float VdotH)
{
    // Base PBR layer
    float3 base = EvalDefaultModel(mat, NdotV, NdotL, NdotH, VdotH);

    // Clearcoat layer
    float Fc;
    float coat = EvalClearcoat(mat.clearcoatRoughness, NdotH, NdotV, NdotL, VdotH, Fc);

    // Attenuate base by (1 - Fc) to conserve energy, scale coat by strength
    return base * (1.0f - Fc * mat.clearcoatStrength) + coat * mat.clearcoatStrength;
}

/// Evaluate the cloth shading model.
/// Replaces GGX specular with the Charlie/Neubelt sheen BRDF. Diffuse is
/// still Frostbite.
/// @ref Estevez & Kulla 2017 "Production Friendly Microfacet Sheen BRDF"
/// @ref Lagarde & de Rousiers 2014 "Moving Frostbite to PBR"
/// @constants None
/// @deviations NONE
float3 EvalClothModel(MaterialParams mat, float NdotV, float NdotL, float NdotH, float VdotH)
{
    float3 diffuse = EvalFrostbiteDiffuse(mat.diffuseColor, mat.roughness, NdotV, NdotL, VdotH);
    float3 sheen   = EvalClothSheen(mat.sheenColor, mat.sheenRoughness, NdotH, NdotV, NdotL);
    return diffuse + sheen;
}

/// Evaluate the subsurface shading model.
/// Uses wrap diffuse + back lighting for the diffuse term, plus standard
/// GGX specular.
/// @ref Valve 2006 "Half-Life 2 Shading"
/// @ref Brisebois & Bouchard 2011 "Approximating Translucency"
/// @constants None
/// @deviations NONE
float3 EvalSubsurfaceModel(MaterialParams mat, float3 N, float3 V, float3 L, float NdotV, float NdotL, float NdotH, float VdotH)
{
    float3 sss      = EvalSubsurface(N, L, V, mat.subsurfaceColor, mat.thickness);
    float3 specular = EvalSpecularDVF(mat.alpha, NdotH, NdotV, NdotL, VdotH, mat.f0);
    return sss + specular;
}

/// Evaluate the foliage shading model.
/// Two-sided diffuse (front + transmitted back) plus GGX specular on the
/// front face only.
/// @ref Habel et al. 2007 "Physically Based Real-Time Translucency for Leaves"
/// @constants None
/// @deviations NONE
float3 EvalFoliageModel(MaterialParams mat, float3 N, float3 V, float3 L, float NdotV, float NdotL, float NdotH, float VdotH)
{
    float3 foliage  = EvalFoliage(N, L, V, mat.subsurfaceColor);
    float3 specular = EvalSpecularDVF(mat.alpha, NdotH, NdotV, NdotL, VdotH, mat.f0);
    return foliage + specular;
}

/// Evaluate the thin translucent shading model.
/// Exponential transmission through thin geometry plus GGX specular.
/// @ref Jimenez et al. 2010 "Real-Time Realistic Skin Translucency"
/// @constants None
/// @deviations NONE
float3 EvalThinTranslucentModel(MaterialParams mat, float3 N, float3 V, float3 L, float NdotV, float NdotL, float NdotH, float VdotH)
{
    float3 transmission = EvalThinTranslucent(N, L, V, mat.subsurfaceColor, mat.thickness);
    float3 specular     = EvalSpecularDVF(mat.alpha, NdotH, NdotV, NdotL, VdotH, mat.f0);
    return transmission + specular;
}

// ─── Main Evaluation Dispatch ────────────────────────────────────────────────

/// Evaluate the standard material for a single light direction.
/// Dispatches to the correct shading model based on mat.shadingModel.
///
/// Returns the BRDF value f(V,L) — the caller multiplies by NdotL and
/// light intensity.
///
/// @ref Lagarde & de Rousiers 2014 "Moving Frostbite to PBR"
/// @ref Walter et al. 2007 "Microfacet Models for Refraction through Rough Surfaces"
/// @ref Estevez & Kulla 2017 "Production Friendly Microfacet Sheen BRDF"
/// @constants None (dispatches to model-specific functions)
/// @deviations NONE
///
/// @param mat   Initialized material parameters.
/// @param N     Surface normal (world space).
/// @param V     View direction (world space, surface to camera).
/// @param L     Light direction (world space, surface to light).
/// @param NdotV Cosine of angle between normal and view direction.
/// @param NdotL Cosine of angle between normal and light direction.
/// @param NdotH Cosine of angle between normal and half-vector.
/// @param VdotH Cosine of angle between view and half-vector.
/// @return      BRDF value f(V,L) for this light (NOT multiplied by NdotL).
float3 EvalStandardMaterial(
    MaterialParams mat,
    float3 N,
    float3 V,
    float3 L,
    float  NdotV,
    float  NdotL,
    float  NdotH,
    float  VdotH)
{
    switch (mat.shadingModel)
    {
    case kShadingModelDefault:
        return EvalDefaultModel(mat, NdotV, NdotL, NdotH, VdotH);

    case kShadingModelSubsurface:
        return EvalSubsurfaceModel(mat, N, V, L, NdotV, NdotL, NdotH, VdotH);

    case kShadingModelClearcoat:
        return EvalClearcoatModel(mat, NdotV, NdotL, NdotH, VdotH);

    case kShadingModelCloth:
        return EvalClothModel(mat, NdotV, NdotL, NdotH, VdotH);

    case kShadingModelFoliage:
        return EvalFoliageModel(mat, N, V, L, NdotV, NdotL, NdotH, VdotH);

    case kShadingModelThinTranslucent:
        return EvalThinTranslucentModel(mat, N, V, L, NdotV, NdotL, NdotH, VdotH);

    default:
        return EvalDefaultModel(mat, NdotV, NdotL, NdotH, VdotH);
    }
}

#endif // UZ_MATERIAL_STANDARD_MATERIAL_HLSLI
