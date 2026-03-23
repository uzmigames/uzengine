/// UzEngine Material Interface
///
/// Abstract base class for all material types. Both `Material` (the shared asset
/// defining default PBR parameters) and `MaterialInstance` (sparse per-object
/// overrides) inherit from `MaterialInterface`, enabling polymorphic material
/// slot assignment on mesh components.
///
/// # Design Rationale
/// IMaterialInterface is the common base for Material and
/// UMaterialInstanceDynamic. UzEngine mirrors this hierarchy with a fixed PBR
/// parameter set instead of a full material graph system.
///
/// # Design Decisions
/// - Pure virtuals for parameter queries — each subclass resolves values
///   through its own mechanism (direct fields for Material, override+parent
///   chain for MaterialInstance).
/// - Convenience getters (GetRoughnessMultiplier, etc.) have default
///   implementations that delegate to the pure virtual GetScalarParam/
///   GetVectorParam. Subclasses may override for performance if needed.
/// - No hashmap storage at this level — Material uses fixed fields,
///   MaterialInstance uses bool flags. Custom parameters can be added
///   via hashmap in a future phase without changing the interface.
///
/// # Thread Safety
/// NOT thread-safe. Modify from the game thread only. The renderer reads
/// a snapshot of effective parameters when building draw commands.

#pragma once

#include <cstdint>
#include <string>

namespace uz {

struct Vec3;

// ─────────────────────────────────────────────────────────────────────────────
// MaterialInterface — abstract base for Material and MaterialInstance
// ─────────────────────────────────────────────────────────────────────────────

/// Base interface for all material types. Both Material and MaterialInstance
/// inherit from this, allowing polymorphic material slot assignment.
///
/// The interface provides two levels of access:
/// 1. **Named parameter queries** — `GetScalarParam`/`GetVectorParam` for
///    generic access (useful for serialization, editors, Lua bindings).
/// 2. **Typed convenience getters** — `GetRoughnessMultiplier()` etc. for
///    hot-path renderer access with zero string lookup overhead.
///
/// Subclasses MUST implement all pure virtual methods. The convenience getters
/// have default implementations delegating to the named queries, but subclasses
/// should override them to read fixed fields directly for performance.
class MaterialInterface {
public:
    virtual ~MaterialInterface() = default;

    // ── Named parameter queries (virtual — overridden by Material and MaterialInstance) ──

    /// Retrieves a scalar (float) parameter by name.
    ///
    /// # Parameters
    /// - `name`: Parameter name (e.g., "RoughnessMultiplier", "Opacity").
    /// - `defaultVal`: Value returned when the parameter is not found.
    ///
    /// # Returns
    /// The parameter value, or `defaultVal` if not found.
    [[nodiscard]] virtual float GetScalarParam(const std::string& name,
                                                float defaultVal) const = 0;

    /// Retrieves a vector (float3) parameter by name.
    ///
    /// Returns components as a simple struct with x, y, z fields. The caller
    /// provides a default value used when the parameter is not found.
    ///
    /// # Parameters
    /// - `name`: Parameter name (e.g., "BaseColorTint", "EmissiveColor").
    /// - `defaultR`, `defaultG`, `defaultB`: Default color returned when not found.
    ///
    /// # Returns
    /// Three floats packed in the output parameters.
    virtual void GetVectorParam(const std::string& name,
                                float& outR, float& outG, float& outB,
                                float defaultR, float defaultG, float defaultB) const = 0;

    /// Retrieves a texture path by name.
    ///
    /// # Parameters
    /// - `name`: Texture slot name (e.g., "Albedo", "Normal", "Roughness").
    ///
    /// # Returns
    /// VFS-relative path, or empty string if no texture is assigned to that slot.
    [[nodiscard]] virtual const std::string& GetTexturePath(const std::string& name) const = 0;

    /// Retrieves the GPU SRV index for a named texture slot.
    ///
    /// # Parameters
    /// - `name`: Texture slot name (e.g., "Albedo", "Normal").
    ///
    /// # Returns
    /// SRV heap index, or 0 if the texture is not loaded.
    [[nodiscard]] virtual uint32_t GetTextureSrvIndex(const std::string& name) const = 0;

    // ── Convenience getters for standard PBR parameters ─────────────────────
    // Default implementations delegate to the named queries. Subclasses should
    // override these to read fixed fields directly for hot-path performance.

    [[nodiscard]] virtual float GetRoughnessMultiplier() const { return GetScalarParam("RoughnessMultiplier", 1.0f); }
    [[nodiscard]] virtual float GetMetallicMultiplier() const  { return GetScalarParam("MetallicMultiplier", 1.0f); }
    [[nodiscard]] virtual float GetNormalIntensity() const     { return GetScalarParam("NormalIntensity", 1.0f); }
    [[nodiscard]] virtual float GetAoIntensity() const         { return GetScalarParam("AoIntensity", 1.0f); }
    [[nodiscard]] virtual float GetOpacity() const             { return GetScalarParam("Opacity", 1.0f); }
    [[nodiscard]] virtual float GetEmissiveIntensity() const   { return GetScalarParam("EmissiveIntensity", 0.0f); }
    [[nodiscard]] virtual float GetUvScaleX() const            { return GetScalarParam("UvScaleX", 1.0f); }
    [[nodiscard]] virtual float GetUvScaleY() const            { return GetScalarParam("UvScaleY", 1.0f); }
    [[nodiscard]] virtual float GetUvOffsetX() const           { return GetScalarParam("UvOffsetX", 0.0f); }
    [[nodiscard]] virtual float GetUvOffsetY() const           { return GetScalarParam("UvOffsetY", 0.0f); }

    /// Dielectric specular reflectance scale. Controls F0 for non-metallic surfaces:
    /// F0 = 0.08 * specular. Default 0.5 produces F0 = 0.04 (IOR ~1.5).
    /// Range [0, 1]. 0 = no specular reflection, 1 = F0 = 0.08 (IOR ~1.8).
    /// @ref UE5 "Material Inputs — Specular" — same 0.08 * specular formula.
    [[nodiscard]] virtual float GetSpecular() const            { return GetScalarParam("Specular", 0.5f); }

    /// Per-material IBL intensity multiplier. 0 = no environment lighting,
    /// 1 = full skylight intensity, >1 = boosted. Packed into materialFlags
    /// bits[16:23] with 8-bit precision (range 0.0-2.0, 128 = 1.0).
    [[nodiscard]] virtual float GetEnvMultiplier() const       { return GetScalarParam("EnvMultiplier", 1.0f); }

    /// Base color tint — multiplied with the albedo texture sample.
    /// Returns (R, G, B) in linear sRGB. Default (1,1,1) = no tint.
    virtual void GetBaseColorTint(float& r, float& g, float& b) const {
        GetVectorParam("BaseColorTint", r, g, b, 1.0f, 1.0f, 1.0f);
    }

    /// Emissive color — added to final output (self-lit, bypasses lighting).
    /// Returns (R, G, B) in linear sRGB. Default (0,0,0) = no emission.
    virtual void GetEmissiveColor(float& r, float& g, float& b) const {
        GetVectorParam("EmissiveColor", r, g, b, 0.0f, 0.0f, 0.0f);
    }

    // ── Rendering flags ─────────────────────────────────────────────────────

    /// When true, disables backface culling. Used for foliage, cloth, thin geometry.
    [[nodiscard]] virtual bool IsTwoSided() const { return false; }

    /// When true, enables alpha testing (discard pixels below clip threshold).
    [[nodiscard]] virtual bool IsAlphaTest() const { return false; }

    /// Alpha clip threshold for masked blend mode.
    [[nodiscard]] virtual float GetAlphaClipThreshold() const { return 0.3333f; }

    // ── Texture path convenience ────────────────────────────────────────────

    [[nodiscard]] const std::string& GetAlbedoPath() const    { return GetTexturePath("Albedo"); }
    [[nodiscard]] const std::string& GetNormalPath() const     { return GetTexturePath("Normal"); }
    [[nodiscard]] const std::string& GetRoughnessPath() const  { return GetTexturePath("Roughness"); }
    [[nodiscard]] const std::string& GetMetallicPath() const   { return GetTexturePath("Metallic"); }
    [[nodiscard]] const std::string& GetAoPath() const         { return GetTexturePath("Ao"); }
    [[nodiscard]] const std::string& GetEmissivePath() const   { return GetTexturePath("Emissive"); }
    [[nodiscard]] const std::string& GetOrmPath() const       { return GetTexturePath("ORM"); }

    // ── Per-slot sRGB resolution ─────────────────────────────────────────────

    /// Returns whether a texture slot should be created as sRGB (true) or
    /// linear (false). Queried by name so any shading model's slots work
    /// without changing this interface (PBR, Hair, Cloth, SSS, etc.).
    ///
    /// Default logic: slot names containing "Color", "Albedo", "BaseColor",
    /// "Emissive", "Diffuse", "Scatter", "Fuzz", "Subsurface" are sRGB.
    /// Everything else (Normal, Roughness, Metallic, AO, ORM, Thickness,
    /// Opacity, Heightmap, Mask, etc.) is linear.
    ///
    /// Subclasses may override for per-slot artist overrides from .uzmat.
    [[nodiscard]] virtual bool IsTextureSlotSRGB(const std::string& slotName) const {
        return DefaultSlotIsSRGB(slotName);
    }

    /// Static helper: determines default sRGB based on slot naming convention.
    /// Color-carrying slots are sRGB; data/scalar slots are linear.
    [[nodiscard]] static bool DefaultSlotIsSRGB(const std::string& slotName) {
        // Color data slots → sRGB (GPU auto-converts gamma→linear on sample)
        if (slotName == "Albedo" || slotName == "BaseColor" || slotName == "Diffuse") return true;
        if (slotName == "Emissive") return true;
        if (slotName == "ScatterColor" || slotName == "SubsurfaceColor") return true;
        if (slotName == "FuzzColor" || slotName == "ClothColor") return true;
        if (slotName == "DetailAlbedo") return true;
        // Everything else → linear (normal, roughness, metallic, AO, ORM,
        // opacity, thickness, heightmap, mask, tangent, clearcoat, etc.)
        return false;
    }

    // ── SRV index convenience ───────────────────────────────────────────────

    [[nodiscard]] uint32_t GetAlbedoSrvIndex() const    { return GetTextureSrvIndex("Albedo"); }
    [[nodiscard]] uint32_t GetNormalSrvIndex() const     { return GetTextureSrvIndex("Normal"); }
    [[nodiscard]] uint32_t GetRoughnessSrvIndex() const  { return GetTextureSrvIndex("Roughness"); }
    [[nodiscard]] uint32_t GetMetallicSrvIndex() const   { return GetTextureSrvIndex("Metallic"); }
    [[nodiscard]] uint32_t GetAoSrvIndex() const         { return GetTextureSrvIndex("Ao"); }
    [[nodiscard]] uint32_t GetEmissiveSrvIndex() const   { return GetTextureSrvIndex("Emissive"); }
    [[nodiscard]] uint32_t GetOrmSrvIndex() const        { return GetTextureSrvIndex("ORM"); }

    // ── ORM packed mode ─────────────────────────────────────────────────────

    [[nodiscard]] virtual bool IsORM() const { return false; }

    // ── GPU resource state ──────────────────────────────────────────────────

    /// True after the renderer has uploaded all referenced textures to the GPU.
    [[nodiscard]] virtual bool IsGpuResourcesLoaded() const { return false; }

    /// Called by the renderer after uploading textures. Subclasses should set
    /// their internal loaded flag.
    virtual void SetGpuResourcesLoaded(bool loaded) { (void)loaded; }
};

} // namespace uz
