/// UzEngine Material Type Enumerations
///
/// Defines the core enums that classify materials by shading model, domain,
/// and blend mode. These enums are used throughout the renderer to select
/// shader permutations, sort draw calls, and configure pipeline state.

#pragma once

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// EShadingModel — determines which BRDF / lighting path is used
// ─────────────────────────────────────────────────────────────────────────────

/// Selects the shading model for a material surface.
///
/// Each value maps to a unique lighting evaluation path in the deferred
/// lighting shader. The numeric IDs are stored in the G-buffer shading
/// model channel and must remain stable across engine versions.
///
/// @ref Industry-standard shading model IDs used in modern deferred renderers.
///      DefaultLit = standard Cook-Torrance PBR; Subsurface/SubsurfaceProfile
///      per Burley 2015 "Extending the Disney BRDF to a BSDF with Integrated
///      Subsurface Scattering"; ClearCoat per Karis 2013 "Real Shading in
///      Unreal Engine 4"; Hair per Marschner 2003 "Light Scattering from Human
///      Hair Fibers"; Cloth per Estevez & Kulla 2017 "Production Friendly
///      Microfacet Sheen BRDF"; Eye per Jimenez 2012 "Next-Generation
///      Character Rendering".
enum class EShadingModel : uint8_t {
    Unlit              = 0,  ///< No lighting — emissive only.
    DefaultLit         = 1,  ///< Standard metallic/roughness PBR (Cook-Torrance GGX).
    Subsurface         = 2,  ///< Wrap-diffuse subsurface approximation.
    SubsurfaceProfile  = 3,  ///< Screen-space subsurface scattering with diffusion profile.
    ClearCoat          = 4,  ///< Dual-lobe: base layer + clear coat specular.
    TwoSidedFoliage    = 5,  ///< Transmission through thin surfaces (leaves, petals).
    Hair               = 6,  ///< Anisotropic hair fiber shading (Marschner model).
    Cloth              = 7,  ///< Fabric sheen BRDF (Estevez-Kulla or Ashikhmin).
    Eye                = 8,  ///< Specialized eye shading with iris caustics.
    ThinTranslucent    = 9,  ///< Single-layer thin-film translucency.
    Count              = 10, ///< Total number of shading models. Not a valid model.
};

// ─────────────────────────────────────────────────────────────────────────────
// EMaterialDomain — determines where in the pipeline a material is evaluated
// ─────────────────────────────────────────────────────────────────────────────

/// Classifies materials by their rendering domain.
///
/// The domain controls which render pass evaluates the material and what
/// outputs the pixel shader must produce (e.g., G-buffer channels for
/// Surface, blended color for PostProcess).
enum class EMaterialDomain : uint8_t {
    Surface       = 0,  ///< Standard 3D surface — writes to G-buffer or forward pass.
    Decal         = 1,  ///< Projected decal — modifies existing G-buffer channels.
    PostProcess   = 2,  ///< Full-screen post-process effect.
    LightFunction = 3,  ///< Light attenuation modifier (IES profile, gobo, etc.).
    UI            = 4,  ///< 2D user interface element — screen-space, no lighting.
    Volume        = 5,  ///< Volumetric material (fog cards, participating media).
    Count         = 6, ///< Total number of material domains. Not a valid domain.
};

// ─────────────────────────────────────────────────────────────────────────────
// EBlendMode — determines alpha blending behavior
// ─────────────────────────────────────────────────────────────────────────────

/// Selects the alpha blending mode for a material.
///
/// Opaque and Masked materials are rendered in the deferred G-buffer pass.
/// Translucent, Additive, and Modulate materials are rendered in a separate
/// forward pass with depth reads but no depth writes.
enum class EBlendMode : uint8_t {
    Opaque      = 0,  ///< Fully opaque — no blending.
    Masked      = 1,  ///< Alpha-tested — pixel is either fully opaque or discarded.
    Translucent = 2,  ///< Standard alpha blending (SrcAlpha, InvSrcAlpha).
    Additive    = 3,  ///< Additive blending (One, One) — used for glow, particles.
    Modulate    = 4,  ///< Multiplicative blending (DestColor, Zero) — used for shadows.
    Count       = 5, ///< Total number of blend modes. Not a valid mode.
};

// ─────────────────────────────────────────────────────────────────────────────
// EDecalBlendMode — controls which DBuffer channels a decal writes
// ─────────────────────────────────────────────────────────────────────────────

/// Selects which G-buffer channels a decal material modifies.
///
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall" — decals can selectively write to color, normal,
///      and/or roughness channels. This avoids unnecessary G-buffer writes
///      (e.g., a blood splatter only needs color, not normal/roughness).
///
/// The renderer uses this to set DBuffer write masks and optimize per-pixel
/// shader work (skip texture sampling for channels not being written).
enum class EDecalBlendMode : uint8_t {
    Translucent                  = 0,  ///< Color only (alpha-blended onto albedo).
    Stain                        = 1,  ///< Modulate existing albedo (multiply blend).
    Normal                       = 2,  ///< Normal only (bump decals, no color change).
    Emissive                     = 3,  ///< Emissive only (glowing decals, no surface modification).

    DBuffer_ColorNormalRoughness = 4,  ///< Full DBuffer: writes color + normal + roughness.
    DBuffer_Color                = 5,  ///< DBuffer: color channel only.
    DBuffer_ColorNormal          = 6,  ///< DBuffer: color + normal channels.
    DBuffer_ColorRoughness       = 7,  ///< DBuffer: color + roughness channels.
    DBuffer_Normal               = 8,  ///< DBuffer: normal channel only.
    DBuffer_NormalRoughness      = 9,  ///< DBuffer: normal + roughness channels.
    DBuffer_Roughness            = 10, ///< DBuffer: roughness channel only.

    Count                        = 11, ///< Total number of blend modes. Not a valid mode.
};

/// Returns a human-readable string for the given decal blend mode.
[[nodiscard]] inline const char* DecalBlendModeToString(EDecalBlendMode m) noexcept {
    switch (m) {
        case EDecalBlendMode::Translucent:                  return "Translucent";
        case EDecalBlendMode::Stain:                        return "Stain";
        case EDecalBlendMode::Normal:                       return "Normal";
        case EDecalBlendMode::Emissive:                     return "Emissive";
        case EDecalBlendMode::DBuffer_ColorNormalRoughness: return "DBuffer_ColorNormalRoughness";
        case EDecalBlendMode::DBuffer_Color:                return "DBuffer_Color";
        case EDecalBlendMode::DBuffer_ColorNormal:          return "DBuffer_ColorNormal";
        case EDecalBlendMode::DBuffer_ColorRoughness:       return "DBuffer_ColorRoughness";
        case EDecalBlendMode::DBuffer_Normal:               return "DBuffer_Normal";
        case EDecalBlendMode::DBuffer_NormalRoughness:      return "DBuffer_NormalRoughness";
        case EDecalBlendMode::DBuffer_Roughness:            return "DBuffer_Roughness";
        case EDecalBlendMode::Count:                        return "Count";
    }
    return "Unknown";
}

/// Returns true if the decal blend mode writes to the color (albedo) channel.
[[nodiscard]] inline bool DecalWritesColor(EDecalBlendMode m) noexcept {
    return m == EDecalBlendMode::Translucent
        || m == EDecalBlendMode::Stain
        || m == EDecalBlendMode::DBuffer_ColorNormalRoughness
        || m == EDecalBlendMode::DBuffer_Color
        || m == EDecalBlendMode::DBuffer_ColorNormal
        || m == EDecalBlendMode::DBuffer_ColorRoughness;
}

/// Returns true if the decal blend mode writes to the normal channel.
[[nodiscard]] inline bool DecalWritesNormal(EDecalBlendMode m) noexcept {
    return m == EDecalBlendMode::Normal
        || m == EDecalBlendMode::DBuffer_ColorNormalRoughness
        || m == EDecalBlendMode::DBuffer_ColorNormal
        || m == EDecalBlendMode::DBuffer_Normal
        || m == EDecalBlendMode::DBuffer_NormalRoughness;
}

/// Returns true if the decal blend mode writes to the roughness channel.
[[nodiscard]] inline bool DecalWritesRoughness(EDecalBlendMode m) noexcept {
    return m == EDecalBlendMode::DBuffer_ColorNormalRoughness
        || m == EDecalBlendMode::DBuffer_ColorRoughness
        || m == EDecalBlendMode::DBuffer_NormalRoughness
        || m == EDecalBlendMode::DBuffer_Roughness;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper functions
// ─────────────────────────────────────────────────────────────────────────────

/// Returns a human-readable string for the given shading model.
[[nodiscard]] inline const char* ShadingModelToString(EShadingModel m) noexcept {
    switch (m) {
        case EShadingModel::Unlit:             return "Unlit";
        case EShadingModel::DefaultLit:        return "DefaultLit";
        case EShadingModel::Subsurface:        return "Subsurface";
        case EShadingModel::SubsurfaceProfile: return "SubsurfaceProfile";
        case EShadingModel::ClearCoat:         return "ClearCoat";
        case EShadingModel::TwoSidedFoliage:   return "TwoSidedFoliage";
        case EShadingModel::Hair:              return "Hair";
        case EShadingModel::Cloth:             return "Cloth";
        case EShadingModel::Eye:               return "Eye";
        case EShadingModel::ThinTranslucent:   return "ThinTranslucent";
        case EShadingModel::Count:             return "Count";
    }
    return "Unknown";
}

/// Returns a human-readable string for the given material domain.
[[nodiscard]] inline const char* MaterialDomainToString(EMaterialDomain d) noexcept {
    switch (d) {
        case EMaterialDomain::Surface:       return "Surface";
        case EMaterialDomain::Decal:         return "Decal";
        case EMaterialDomain::PostProcess:   return "PostProcess";
        case EMaterialDomain::LightFunction: return "LightFunction";
        case EMaterialDomain::UI:            return "UI";
        case EMaterialDomain::Volume:        return "Volume";
        case EMaterialDomain::Count:         return "Count";
    }
    return "Unknown";
}

/// Returns a human-readable string for the given blend mode.
[[nodiscard]] inline const char* BlendModeToString(EBlendMode b) noexcept {
    switch (b) {
        case EBlendMode::Opaque:      return "Opaque";
        case EBlendMode::Masked:      return "Masked";
        case EBlendMode::Translucent: return "Translucent";
        case EBlendMode::Additive:    return "Additive";
        case EBlendMode::Modulate:    return "Modulate";
        case EBlendMode::Count:       return "Count";
    }
    return "Unknown";
}

/// Returns true if the shading model requires lighting evaluation.
///
/// Unlit materials skip all lighting calculations (direct, indirect, shadows).
/// Every other shading model participates in the lighting pass.
[[nodiscard]] inline bool IsLitShadingModel(EShadingModel m) noexcept {
    return m != EShadingModel::Unlit;
}

/// Returns true if the blend mode requires forward translucency rendering.
///
/// Translucent, Additive, and Modulate blend modes are rendered in a separate
/// forward pass with sorted draw order rather than the deferred G-buffer pass.
[[nodiscard]] inline bool IsTranslucentBlendMode(EBlendMode b) noexcept {
    return b == EBlendMode::Translucent
        || b == EBlendMode::Additive
        || b == EBlendMode::Modulate;
}

} // namespace uz
