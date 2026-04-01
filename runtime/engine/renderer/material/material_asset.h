/// UzEngine Material Asset
///
/// A Material is a shared asset that defines the visual appearance of a surface.
/// It holds default parameter values and texture paths. Multiple meshes can
/// reference the same Material. For per-object parameter overrides, use
/// `MaterialInstance`.
///
/// # Relationship to PBRMaterial
/// `PBRMaterial` (material.h) is a 48-byte GPU-uploadable struct for the
/// StructuredBuffer path. `Material` is the higher-level asset that includes
/// texture paths, UV transforms, rendering flags, and GPU resource indices.
/// The scene renderer reads Material/MaterialInstance parameters and writes
/// them into `PerObjectCB` fields per draw call.
///
/// # Inheritance
/// Material inherits from `MaterialInterface`, the abstract base shared with
/// `MaterialInstance`. This allows polymorphic material slot assignment on
/// mesh components: a slot can hold either a Material or a MaterialInstance.
///
/// # Design Rationale
/// UzEngine uses
/// a fixed PBR parameter set matching common PBR material outputs.
///
/// # Thread Safety
/// NOT thread-safe. Modify from the game thread only. The renderer reads
/// a snapshot of effective parameters when building draw commands.

#pragma once

#include "material_interface.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Material — shared PBR material asset
// ─────────────────────────────────────────────────────────────────────────────

/// Defines the complete visual properties of a PBR surface.
///
/// All scalar parameters use neutral defaults (1.0 for multipliers, 0.0 for
/// offsets/additive) so that an unmodified Material produces the same visual
/// result as the existing hardcoded pipeline.
///
/// # Texture Paths
/// VFS-relative paths (e.g., "textures/brick_albedo.png"). Empty string means
/// no texture — the renderer uses a fallback (white for albedo/roughness/AO,
/// flat blue for normal, black for metallic/emissive).
///
/// # GPU SRV Indices
/// Populated by the scene renderer after texture upload. Game code should NOT
/// set these directly.
struct Material : public MaterialInterface {
    // ── Texture paths (VFS-relative) ─────────────────────────────────────

    std::string albedoPath;      ///< BaseColor map (sRGB). Fallback: white.
    std::string normalPath;      ///< Tangent-space normal map. Fallback: flat (128,128,255).
    std::string roughnessPath;   ///< Roughness map (R channel). Fallback: white (roughness=1.0).
    std::string metallicPath;    ///< Metallic map (R channel). Fallback: black (metallic=0.0).
    std::string aoPath;          ///< Ambient occlusion map (R channel). Fallback: white (ao=1.0).
    std::string emissivePath;    ///< Emissive map (RGB, HDR). Fallback: black (no emission).
    std::string opacityPath;     ///< Opacity/alpha mask texture (R channel). Fallback: white (fully opaque).
    std::string ormPath;         ///< Packed ORM map (R=AO, G=Roughness, B=Metallic). When set, overrides separate R/M/AO.
    uint32_t    ormSrvIndex = 0; ///< SRV heap index for loaded ORM texture.

    /// When true, the material uses a packed ORM texture instead of separate
    /// roughness/metallic/AO maps. The ORM texture is sampled from ormPath
    /// (or aoPath as fallback) and unpacked as R=AO, G=Roughness, B=Metallic.
    bool useORM = false;

    // ── Per-slot sRGB override (optional artist control) ──────────────────
    // Generic map keyed by slot name (e.g., "Albedo", "Normal", "ORM").
    // Values: 0 = force linear, 1 = force sRGB. Absent = auto (default).
    // Populated from .uzmat JSON: "textures": { "Albedo": { "srgb": true } }
    // Works for any shading model's slots without changing this struct.
    std::unordered_map<std::string, bool> srgbOverrides;

    /// Sets an explicit sRGB override for a texture slot by name.
    void SetTextureSlotSRGB(const std::string& slotName, bool isSRGB) {
        srgbOverrides[slotName] = isSRGB;
    }

    /// Resolves sRGB for a slot: explicit override → auto from slot name.
    [[nodiscard]] bool IsTextureSlotSRGB(const std::string& slotName) const override {
        auto it = srgbOverrides.find(slotName);
        if (it != srgbOverrides.end()) return it->second;
        return DefaultSlotIsSRGB(slotName);
    }

    // ── Scalar parameters ────────────────────────────────────────────────

    /// Base color tint (linear RGB). Multiplied with the albedo texture sample.
    /// Default (1,1,1) = no tint, texture color used as-is.
    float baseColorTintR = 1.0f;
    float baseColorTintG = 1.0f;
    float baseColorTintB = 1.0f;

    /// Multiplier applied to the roughness texture sample. Default 1.0 = unmodified.
    /// Range [0, inf) but typically [0, 2]. Values > 1 increase roughness.
    float roughnessMultiplier = 1.0f;

    /// Multiplier applied to the metallic texture sample. Default 1.0 = unmodified.
    /// Range [0, inf) but typically [0, 2].
    float metallicMultiplier = 1.0f;

    /// Normal map strength. 0 = flat surface (ignores normal map), 1 = full effect.
    /// Interpolates: finalNormal.xy = normalTS.xy * normalIntensity.
    float normalIntensity = 1.0f;

    /// AO map strength. Lerps between 1.0 (no occlusion) and the AO texture value.
    /// ao_final = lerp(1.0, ao_texture, aoIntensity). Default 1.0 = full AO effect.
    float aoIntensity = 1.0f;

    /// Overall surface opacity. 1.0 = fully opaque (default).
    /// Used only for translucent/masked blend modes.
    float opacity = 1.0f;

    /// Dielectric specular reflectance scale. F0 = 0.08 * specular.
    /// Default 0.5 produces F0 = 0.04 (standard IOR ~1.5 for most dielectrics).
    /// Range [0, 1]. Organic materials (skin, fabric, wood) typically use 0.25-0.5.
    /// @ref Schlick 1994 "An Inexpensive BRDF Model for Physically-based Rendering" — F0 for dielectrics
    /// @ref Lagarde 2014 "Moving Frostbite to Physically Based Rendering" — 0.08 * specular formula
    float specular = 0.5f;

    /// Per-material IBL intensity multiplier. Controls how much environment
    /// lighting (sky light, reflection probes) affects this surface.
    /// 0 = no IBL, 1 = full skylight contribution (default), >1 = boosted.
    /// Packed into materialFlags bits[16:23] with range 0.0-2.0.
    float envMultiplier = 1.0f;

    // ── Emissive parameters ──────────────────────────────────────────────

    /// Emissive tint color (linear RGB). Default (0,0,0) = no emission.
    float emissiveColorR = 0.0f;
    float emissiveColorG = 0.0f;
    float emissiveColorB = 0.0f;

    /// Emissive brightness multiplier. 0 = no emission. Values > 1.0 drive bloom.
    float emissiveIntensity = 0.0f;

    // ── UV Transform ─────────────────────────────────────────────────────

    /// Texture tiling (scale) and offset. Applied to UV coordinates before sampling.
    /// Default (1,1,0,0) = unmodified UVs.
    float uvScaleX  = 1.0f;   ///< Texture tiling X (1.0 = no tiling).
    float uvScaleY  = 1.0f;   ///< Texture tiling Y (1.0 = no tiling).
    float uvOffsetX = 0.0f;   ///< Texture offset X.
    float uvOffsetY = 0.0f;   ///< Texture offset Y.

    // ── Subsurface scattering parameters ────────────────────────────────

    /// Subsurface scattering color (linear RGB). Default warm skin tone.
    /// @ref Burley 2015 "Extending the Disney BRDF to a BSDF with Integrated SSS"
    float subsurfaceColorR = 1.0f;
    float subsurfaceColorG = 0.2f;
    float subsurfaceColorB = 0.1f;

    /// Subsurface radius — controls wrap diffuse width and SSS blur kernel.
    /// @ref Burley 2015 — subsurface radius in world units (cm).
    float subsurfaceRadius = 1.0f;

    /// Thickness texture path (R channel). 0 = thick (no transmission), 1 = thin (full).
    std::string thicknessPath;
    uint32_t    thicknessSrvIndex = 0;

    // ── Clear coat parameters ─────────────────────────────────────────────

    /// Clear coat layer intensity. 0 = no coating (default), 1 = full second specular lobe.
    /// @ref Karis 2013 "Real Shading in Unreal Engine 4" — clear coat shading model.
    float clearCoat = 0.0f;

    /// Clear coat layer roughness. Controls smoothness of the top specular lobe.
    /// Default 0.04 = near-mirror coating (automotive paint, varnish).
    /// @ref Karis 2013 "Real Shading in Unreal Engine 4" — ClearCoatRoughness parameter.
    float clearCoatRoughness = 0.04f;

    /// Optional separate normal map for the clear coat layer.
    /// When set, the coat specular lobe uses this normal instead of the base normal.
    /// Useful for orange-peel coating, carbon fiber clear coat, etc.
    /// @ref Karis 2013 "Real Shading in Unreal Engine 4" — ClearCoatBottomNormal.
    std::string clearCoatNormalPath;
    uint32_t    clearCoatNormalSrvIndex = 0;

    // ── Cloth parameters ─────────────────────────────────────────────────

    /// Cloth sheen tint R. @ref Estevez & Kulla 2017 "Production Friendly Microfacet Sheen BRDF"
    float sheenColorR = 0.04f;
    float sheenColorG = 0.04f;
    float sheenColorB = 0.04f;

    /// Cloth sheen roughness. Controls spread of fabric highlight. Default 0.5.
    float sheenRoughness = 0.5f;

    // ── Shading model / domain / blend mode ─────────────────────────────

    /// Shading model for this material. Determines which BRDF is evaluated
    /// in the lighting pass. Default: DefaultLit (Cook-Torrance GGX PBR).
    EShadingModel shadingModel = EShadingModel::DefaultLit;

    /// Material domain. Determines which render pass evaluates this material.
    /// Default: Surface (standard G-buffer or forward geometry).
    EMaterialDomain materialDomain = EMaterialDomain::Surface;

    /// Blend mode. Determines alpha blending and pass routing.
    /// Default: Opaque (deferred G-buffer, no blending).
    EBlendMode blendMode = EBlendMode::Opaque;

    /// Decal blend mode — controls which DBuffer channels this decal writes.
    /// Only meaningful when materialDomain == EMaterialDomain::Decal.
    /// Selectively write color/normal/roughness DBuffer channels.
    EDecalBlendMode decalBlendMode = EDecalBlendMode::DBuffer_ColorNormalRoughness;

    // ── Rendering flags ──────────────────────────────────────────────────

    /// When true, disables backface culling. Used for foliage, cloth, thin geometry.
    bool twoSided = false;

    /// When true, enables alpha testing (discard pixels below alphaClipThreshold).
    /// Used for foliage, fences, grates, hair cards.
    bool alphaTest = false;

    /// Alpha clip threshold for masked blend mode. Pixels with alpha below this
    /// value are discarded. Default is 0.3333.
    float alphaClipThreshold = 0.3333f;

    // ── GPU resource indices (populated by renderer, read-only for game code) ──

    uint32_t albedoSrvIndex    = 0;  ///< SRV heap index for loaded albedo texture.
    uint32_t normalSrvIndex    = 0;  ///< SRV heap index for loaded normal map.
    uint32_t roughnessSrvIndex = 0;  ///< SRV heap index for loaded roughness map.
    uint32_t metallicSrvIndex  = 0;  ///< SRV heap index for loaded metallic map.
    uint32_t aoSrvIndex        = 0;  ///< SRV heap index for loaded AO map.
    uint32_t emissiveSrvIndex  = 0;  ///< SRV heap index for loaded emissive map.
    uint32_t opacitySrvIndex   = 0;  ///< SRV heap index for opacity/alpha mask texture (0 = none).

    /// True after the scene renderer has uploaded all referenced textures.
    bool gpuResourcesLoaded = false;

    // ── MaterialInterface implementation ─────────────────────────────────

    /// Retrieves a scalar parameter by name from the fixed field set.
    ///
    /// Supported names: "RoughnessMultiplier", "MetallicMultiplier",
    /// "NormalIntensity", "AoIntensity", "Opacity", "EmissiveIntensity",
    /// "UvScaleX", "UvScaleY", "UvOffsetX", "UvOffsetY", "AlphaClipThreshold".
    [[nodiscard]] float GetScalarParam(const std::string& name,
                                        float defaultVal) const override {
        if (name == "RoughnessMultiplier") return roughnessMultiplier;
        if (name == "MetallicMultiplier")  return metallicMultiplier;
        if (name == "NormalIntensity")     return normalIntensity;
        if (name == "AoIntensity")         return aoIntensity;
        if (name == "Opacity")             return opacity;
        if (name == "EmissiveIntensity")   return emissiveIntensity;
        if (name == "UvScaleX")            return uvScaleX;
        if (name == "UvScaleY")            return uvScaleY;
        if (name == "UvOffsetX")           return uvOffsetX;
        if (name == "UvOffsetY")           return uvOffsetY;
        if (name == "AlphaClipThreshold")  return alphaClipThreshold;
        if (name == "Specular")            return specular;
        if (name == "EnvMultiplier")       return envMultiplier;
        if (name == "ClearCoat")           return clearCoat;
        if (name == "ClearCoatRoughness")  return clearCoatRoughness;
        return defaultVal;
    }

    /// Retrieves a vector parameter by name from the fixed field set.
    ///
    /// Supported names: "BaseColorTint", "EmissiveColor".
    void GetVectorParam(const std::string& name,
                        float& outR, float& outG, float& outB,
                        float defaultR, float defaultG, float defaultB) const override {
        if (name == "BaseColorTint") {
            outR = baseColorTintR;
            outG = baseColorTintG;
            outB = baseColorTintB;
        } else if (name == "EmissiveColor") {
            outR = emissiveColorR;
            outG = emissiveColorG;
            outB = emissiveColorB;
        } else {
            outR = defaultR;
            outG = defaultG;
            outB = defaultB;
        }
    }

    /// Returns the VFS-relative texture path for a named slot.
    ///
    /// Supported names: "Albedo", "Normal", "Roughness", "Metallic", "Ao", "Emissive".
    [[nodiscard]] const std::string& GetTexturePath(const std::string& name) const override {
        if (name == "Albedo")    return albedoPath;
        if (name == "Normal")    return normalPath;
        if (name == "Roughness") return roughnessPath;
        if (name == "Metallic")  return metallicPath;
        if (name == "Ao")        return aoPath;
        if (name == "Emissive")  return emissivePath;
        if (name == "Opacity")   return opacityPath;
        if (name == "ORM")       return ormPath;
        if (name == "Thickness") return thicknessPath;
        if (name == "ClearCoatNormal") return clearCoatNormalPath;
        return s_emptyString;
    }

    /// Returns the GPU SRV index for a named texture slot.
    [[nodiscard]] uint32_t GetTextureSrvIndex(const std::string& name) const override {
        if (name == "Albedo")    return albedoSrvIndex;
        if (name == "Normal")    return normalSrvIndex;
        if (name == "Roughness") return roughnessSrvIndex;
        if (name == "Metallic")  return metallicSrvIndex;
        if (name == "Ao")        return aoSrvIndex;
        if (name == "Emissive")  return emissiveSrvIndex;
        if (name == "ORM")       return ormSrvIndex;
        if (name == "Thickness") return thicknessSrvIndex;
        if (name == "ClearCoatNormal") return clearCoatNormalSrvIndex;
        if (name == "Opacity")  return opacitySrvIndex;
        return 0;
    }

    // ── Performance overrides (avoid string lookup on hot path) ──────────

    [[nodiscard]] float GetRoughnessMultiplier() const override { return roughnessMultiplier; }
    [[nodiscard]] float GetMetallicMultiplier() const override  { return metallicMultiplier; }
    [[nodiscard]] float GetNormalIntensity() const override     { return normalIntensity; }
    [[nodiscard]] float GetAoIntensity() const override         { return aoIntensity; }
    [[nodiscard]] float GetOpacity() const override             { return opacity; }
    [[nodiscard]] float GetEmissiveIntensity() const override   { return emissiveIntensity; }
    [[nodiscard]] float GetUvScaleX() const override            { return uvScaleX; }
    [[nodiscard]] float GetUvScaleY() const override            { return uvScaleY; }
    [[nodiscard]] float GetUvOffsetX() const override           { return uvOffsetX; }
    [[nodiscard]] float GetUvOffsetY() const override           { return uvOffsetY; }
    [[nodiscard]] float GetSpecular() const override            { return specular; }
    [[nodiscard]] float GetEnvMultiplier() const override       { return envMultiplier; }

    void GetBaseColorTint(float& r, float& g, float& b) const override {
        r = baseColorTintR;
        g = baseColorTintG;
        b = baseColorTintB;
    }

    void GetEmissiveColor(float& r, float& g, float& b) const override {
        r = emissiveColorR;
        g = emissiveColorG;
        b = emissiveColorB;
    }

    [[nodiscard]] bool IsTwoSided() const override { return twoSided; }
    [[nodiscard]] bool IsAlphaTest() const override { return alphaTest; }
    [[nodiscard]] float GetAlphaClipThreshold() const override { return alphaClipThreshold; }

    void GetSubsurfaceColor(float& r, float& g, float& b) const override {
        r = subsurfaceColorR; g = subsurfaceColorG; b = subsurfaceColorB;
    }
    [[nodiscard]] float GetSubsurfaceRadius() const override { return subsurfaceRadius; }

    [[nodiscard]] float GetClearCoat() const override { return clearCoat; }
    [[nodiscard]] float GetClearCoatRoughness() const override { return clearCoatRoughness; }

    void GetSheenColor(float& r, float& g, float& b) const override {
        r = sheenColorR; g = sheenColorG; b = sheenColorB;
    }
    [[nodiscard]] float GetSheenRoughness() const override { return sheenRoughness; }

    [[nodiscard]] EShadingModel GetShadingModel() const override { return shadingModel; }
    [[nodiscard]] EMaterialDomain GetMaterialDomain() const override { return materialDomain; }
    [[nodiscard]] EBlendMode GetBlendMode() const override { return blendMode; }
    [[nodiscard]] EDecalBlendMode GetDecalBlendMode() const override { return decalBlendMode; }

    [[nodiscard]] bool IsORM() const override { return useORM; }
    [[nodiscard]] bool IsGpuResourcesLoaded() const override { return gpuResourcesLoaded; }
    void SetGpuResourcesLoaded(bool loaded) override { gpuResourcesLoaded = loaded; }

private:
    /// Returned by GetTexturePath when the requested slot name is unknown.
    static inline const std::string s_emptyString;
};

} // namespace uz
