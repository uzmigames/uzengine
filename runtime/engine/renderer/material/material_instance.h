/// UzEngine Material Instance
///
/// A MaterialInstance references a parent `MaterialInterface` and overrides
/// specific parameters. This avoids duplicating the entire Material for
/// per-object variations (e.g., different tint on the same brick material).
///
/// # Override Mechanism
/// Each overridable parameter has a corresponding `bool has_*` flag. When the
/// flag is false, `GetEffective*()` / the virtual getter returns the parent's
/// value. When true, the local override value is used.
///
/// NaN-based sentinels were considered but rejected because:
/// - NaN comparison is unreliable across compilers/platforms
/// - Explicit flags are clearer and cheaper (bool vs isnan() per param)
/// - Flags allow distinguishing "explicitly set to parent's default" from "inherited"
///
/// # UE5 Equivalent
/// @source UE5 MaterialInstanceDynamic.h — UMaterialInstanceDynamic
/// UE5 uses sparse FHashMap<FName, value> for overrides. UzEngine uses a fixed
/// parameter set with bool flags — simpler, zero allocations, cache-friendly.
///
/// # Inheritance Chain
/// @source UE5 MaterialInstance.h:23 — Parent is UMaterialInterface*
/// The `parent` pointer is `MaterialInterface*` (not `Material*`), allowing
/// multi-level chaining: MaterialInstance -> MaterialInstance -> Material.
/// Each level only stores its own overrides; queries propagate up the chain
/// through the virtual interface.
///
/// # Thread Safety
/// NOT thread-safe. Modify from the game thread only.

#pragma once

#include "material_interface.h"

#include <cstdint>
#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// MaterialInstance — per-object parameter overrides for a parent material
// ─────────────────────────────────────────────────────────────────────────────

/// Overrides specific parameters from a parent `MaterialInterface`.
///
/// Use `SetXxx()` to override a parameter and `ClearXxx()` to revert to
/// the parent's value. The virtual getters from `MaterialInterface` return
/// the resolved value (override if set, parent default otherwise).
///
/// # Example
/// ```cpp
/// Material baseBrick;
/// baseBrick.roughnessMultiplier = 0.8f;
/// baseBrick.baseColorTintR = 0.9f;
///
/// MaterialInstance wetBrick;
/// wetBrick.SetParent(&baseBrick);
/// wetBrick.SetRoughnessMultiplier(0.3f);  // Override: wet = smoother
/// // wetBrick.GetRoughnessMultiplier() returns 0.3f (overridden)
/// // wetBrick.GetNormalIntensity() returns 1.0f (inherited from parent)
///
/// // Multi-level chaining:
/// MaterialInstance veryWetBrick;
/// veryWetBrick.SetParent(&wetBrick);
/// veryWetBrick.SetMetallicMultiplier(0.9f);
/// // veryWetBrick.GetRoughnessMultiplier() returns 0.3f (from wetBrick)
/// // veryWetBrick.GetMetallicMultiplier() returns 0.9f (local override)
/// ```
struct MaterialInstance : public MaterialInterface {
    // ── Parent ────────────────────────────────────────────────────────────

    /// Sets the parent material providing default values for non-overridden
    /// parameters. Must not be null when calling any getter.
    ///
    /// @source UE5 MaterialInstance.h:23 — Parent is UMaterialInterface*
    void SetParent(MaterialInterface* p) noexcept { parent_ = p; }

    /// Returns the parent material, or nullptr if none is set.
    [[nodiscard]] MaterialInterface* GetParent() const noexcept { return parent_; }

    // ── Scalar overrides ─────────────────────────────────────────────────
    // Each parameter has a value + has_* flag. When has_* is false, the
    // corresponding getter returns the parent's value via the virtual chain.

    // ── Setters (set value + mark as overridden) ─────────────────────────

    void SetBaseColorTint(float r, float g, float b) noexcept {
        baseColorTintR_ = r; hasBaseColorTintR_ = true;
        baseColorTintG_ = g; hasBaseColorTintG_ = true;
        baseColorTintB_ = b; hasBaseColorTintB_ = true;
    }
    void SetRoughnessMultiplier(float v) noexcept { roughnessMultiplier_ = v; hasRoughnessMultiplier_ = true; }
    void SetMetallicMultiplier(float v) noexcept  { metallicMultiplier_ = v;  hasMetallicMultiplier_ = true; }
    void SetNormalIntensity(float v) noexcept     { normalIntensity_ = v;     hasNormalIntensity_ = true; }
    void SetAoIntensity(float v) noexcept         { aoIntensity_ = v;         hasAoIntensity_ = true; }
    void SetOpacity(float v) noexcept             { opacity_ = v;             hasOpacity_ = true; }

    void SetEmissiveColor(float r, float g, float b) noexcept {
        emissiveColorR_ = r; hasEmissiveColorR_ = true;
        emissiveColorG_ = g; hasEmissiveColorG_ = true;
        emissiveColorB_ = b; hasEmissiveColorB_ = true;
    }
    void SetEmissiveIntensity(float v) noexcept { emissiveIntensity_ = v; hasEmissiveIntensity_ = true; }

    void SetUvScale(float x, float y) noexcept {
        uvScaleX_ = x; hasUvScaleX_ = true;
        uvScaleY_ = y; hasUvScaleY_ = true;
    }
    void SetUvOffset(float x, float y) noexcept {
        uvOffsetX_ = x; hasUvOffsetX_ = true;
        uvOffsetY_ = y; hasUvOffsetY_ = true;
    }

    void SetTwoSided(bool v) noexcept     { twoSided_ = v;     hasTwoSided_ = true; }
    void SetAlphaTest(bool v) noexcept     { alphaTest_ = v;     hasAlphaTest_ = true; }
    void SetAlphaClipThreshold(float v) noexcept { alphaClipThreshold_ = v; hasAlphaClipThreshold_ = true; }

    void SetAlbedoPath(const std::string& path)    { albedoPath_ = path;    hasAlbedoPath_ = true; }
    void SetNormalPath(const std::string& path)     { normalPath_ = path;    hasNormalPath_ = true; }
    void SetRoughnessPath(const std::string& path)  { roughnessPath_ = path; hasRoughnessPath_ = true; }
    void SetMetallicPath(const std::string& path)   { metallicPath_ = path;  hasMetallicPath_ = true; }
    void SetAoPath(const std::string& path)         { aoPath_ = path;        hasAoPath_ = true; }
    void SetEmissivePath(const std::string& path)   { emissivePath_ = path;  hasEmissivePath_ = true; }

    // ── Clear (revert to parent value) ───────────────────────────────────

    void ClearBaseColorTint() noexcept     { hasBaseColorTintR_ = hasBaseColorTintG_ = hasBaseColorTintB_ = false; }
    void ClearRoughnessMultiplier() noexcept { hasRoughnessMultiplier_ = false; }
    void ClearMetallicMultiplier() noexcept  { hasMetallicMultiplier_ = false; }
    void ClearNormalIntensity() noexcept     { hasNormalIntensity_ = false; }
    void ClearAoIntensity() noexcept         { hasAoIntensity_ = false; }
    void ClearOpacity() noexcept             { hasOpacity_ = false; }
    void ClearEmissiveColor() noexcept       { hasEmissiveColorR_ = hasEmissiveColorG_ = hasEmissiveColorB_ = false; }
    void ClearEmissiveIntensity() noexcept   { hasEmissiveIntensity_ = false; }
    void ClearUvScale() noexcept             { hasUvScaleX_ = hasUvScaleY_ = false; }
    void ClearUvOffset() noexcept            { hasUvOffsetX_ = hasUvOffsetY_ = false; }
    void ClearTwoSided() noexcept            { hasTwoSided_ = false; }
    void ClearAlphaTest() noexcept           { hasAlphaTest_ = false; }
    void ClearAlphaClipThreshold() noexcept  { hasAlphaClipThreshold_ = false; }

    void ClearAlbedoPath()    { hasAlbedoPath_ = false;    albedoPath_.clear(); }
    void ClearNormalPath()    { hasNormalPath_ = false;     normalPath_.clear(); }
    void ClearRoughnessPath() { hasRoughnessPath_ = false;  roughnessPath_.clear(); }
    void ClearMetallicPath()  { hasMetallicPath_ = false;   metallicPath_.clear(); }
    void ClearAoPath()        { hasAoPath_ = false;         aoPath_.clear(); }
    void ClearEmissivePath()  { hasEmissivePath_ = false;   emissivePath_.clear(); }

    /// Clears all overrides, reverting every parameter to the parent's value.
    void ClearAll() noexcept {
        hasBaseColorTintR_ = hasBaseColorTintG_ = hasBaseColorTintB_ = false;
        hasRoughnessMultiplier_ = hasMetallicMultiplier_ = false;
        hasNormalIntensity_ = hasAoIntensity_ = hasOpacity_ = false;
        hasEmissiveColorR_ = hasEmissiveColorG_ = hasEmissiveColorB_ = false;
        hasEmissiveIntensity_ = false;
        hasUvScaleX_ = hasUvScaleY_ = hasUvOffsetX_ = hasUvOffsetY_ = false;
        hasTwoSided_ = hasAlphaTest_ = hasAlphaClipThreshold_ = false;
        hasAlbedoPath_ = hasNormalPath_ = hasRoughnessPath_ = false;
        hasMetallicPath_ = hasAoPath_ = hasEmissivePath_ = false;
    }

    /// Returns the number of parameters that are locally overridden.
    [[nodiscard]] uint32_t GetOverrideCount() const noexcept {
        uint32_t count = 0;
        if (hasBaseColorTintR_)      ++count;
        if (hasBaseColorTintG_)      ++count;
        if (hasBaseColorTintB_)      ++count;
        if (hasRoughnessMultiplier_) ++count;
        if (hasMetallicMultiplier_)  ++count;
        if (hasNormalIntensity_)     ++count;
        if (hasAoIntensity_)         ++count;
        if (hasOpacity_)             ++count;
        if (hasEmissiveColorR_)      ++count;
        if (hasEmissiveColorG_)      ++count;
        if (hasEmissiveColorB_)      ++count;
        if (hasEmissiveIntensity_)   ++count;
        if (hasUvScaleX_)            ++count;
        if (hasUvScaleY_)            ++count;
        if (hasUvOffsetX_)           ++count;
        if (hasUvOffsetY_)           ++count;
        if (hasAlbedoPath_)          ++count;
        if (hasNormalPath_)          ++count;
        if (hasRoughnessPath_)       ++count;
        if (hasMetallicPath_)        ++count;
        if (hasAoPath_)              ++count;
        if (hasEmissivePath_)        ++count;
        if (hasTwoSided_)            ++count;
        if (hasAlphaTest_)           ++count;
        if (hasAlphaClipThreshold_)  ++count;
        return count;
    }

    // ── MaterialInterface implementation ─────────────────────────────────
    // Each getter checks the local override flag first, then delegates to
    // the parent via the virtual interface (enabling multi-level chaining).

    [[nodiscard]] float GetScalarParam(const std::string& name,
                                        float defaultVal) const override {
        if (name == "RoughnessMultiplier") return hasRoughnessMultiplier_ ? roughnessMultiplier_ : parent_->GetScalarParam(name, defaultVal);
        if (name == "MetallicMultiplier")  return hasMetallicMultiplier_  ? metallicMultiplier_  : parent_->GetScalarParam(name, defaultVal);
        if (name == "NormalIntensity")     return hasNormalIntensity_     ? normalIntensity_     : parent_->GetScalarParam(name, defaultVal);
        if (name == "AoIntensity")         return hasAoIntensity_         ? aoIntensity_         : parent_->GetScalarParam(name, defaultVal);
        if (name == "Opacity")             return hasOpacity_             ? opacity_             : parent_->GetScalarParam(name, defaultVal);
        if (name == "EmissiveIntensity")   return hasEmissiveIntensity_   ? emissiveIntensity_   : parent_->GetScalarParam(name, defaultVal);
        if (name == "UvScaleX")            return hasUvScaleX_            ? uvScaleX_            : parent_->GetScalarParam(name, defaultVal);
        if (name == "UvScaleY")            return hasUvScaleY_            ? uvScaleY_            : parent_->GetScalarParam(name, defaultVal);
        if (name == "UvOffsetX")           return hasUvOffsetX_           ? uvOffsetX_           : parent_->GetScalarParam(name, defaultVal);
        if (name == "UvOffsetY")           return hasUvOffsetY_           ? uvOffsetY_           : parent_->GetScalarParam(name, defaultVal);
        if (name == "AlphaClipThreshold")  return hasAlphaClipThreshold_  ? alphaClipThreshold_  : parent_->GetScalarParam(name, defaultVal);
        return parent_ ? parent_->GetScalarParam(name, defaultVal) : defaultVal;
    }

    void GetVectorParam(const std::string& name,
                        float& outR, float& outG, float& outB,
                        float defaultR, float defaultG, float defaultB) const override {
        if (name == "BaseColorTint") {
            // Each component can be independently overridden for fine-grained control.
            if (hasBaseColorTintR_ && hasBaseColorTintG_ && hasBaseColorTintB_) {
                outR = baseColorTintR_;
                outG = baseColorTintG_;
                outB = baseColorTintB_;
            } else if (parent_) {
                parent_->GetVectorParam(name, outR, outG, outB, defaultR, defaultG, defaultB);
                if (hasBaseColorTintR_) outR = baseColorTintR_;
                if (hasBaseColorTintG_) outG = baseColorTintG_;
                if (hasBaseColorTintB_) outB = baseColorTintB_;
            } else {
                outR = defaultR; outG = defaultG; outB = defaultB;
            }
        } else if (name == "EmissiveColor") {
            if (hasEmissiveColorR_ && hasEmissiveColorG_ && hasEmissiveColorB_) {
                outR = emissiveColorR_;
                outG = emissiveColorG_;
                outB = emissiveColorB_;
            } else if (parent_) {
                parent_->GetVectorParam(name, outR, outG, outB, defaultR, defaultG, defaultB);
                if (hasEmissiveColorR_) outR = emissiveColorR_;
                if (hasEmissiveColorG_) outG = emissiveColorG_;
                if (hasEmissiveColorB_) outB = emissiveColorB_;
            } else {
                outR = defaultR; outG = defaultG; outB = defaultB;
            }
        } else {
            if (parent_) {
                parent_->GetVectorParam(name, outR, outG, outB, defaultR, defaultG, defaultB);
            } else {
                outR = defaultR; outG = defaultG; outB = defaultB;
            }
        }
    }

    [[nodiscard]] const std::string& GetTexturePath(const std::string& name) const override {
        if (name == "Albedo"    && hasAlbedoPath_)    return albedoPath_;
        if (name == "Normal"    && hasNormalPath_)     return normalPath_;
        if (name == "Roughness" && hasRoughnessPath_)  return roughnessPath_;
        if (name == "Metallic"  && hasMetallicPath_)   return metallicPath_;
        if (name == "Ao"        && hasAoPath_)         return aoPath_;
        if (name == "Emissive"  && hasEmissivePath_)   return emissivePath_;
        return parent_ ? parent_->GetTexturePath(name) : s_emptyString_;
    }

    [[nodiscard]] uint32_t GetTextureSrvIndex(const std::string& name) const override {
        // MaterialInstance doesn't own SRV indices directly — textures are
        // loaded per-path by the renderer. If this instance overrides the path,
        // the renderer will load that texture and store the SRV on the draw
        // command. If not overridden, delegate to the parent.
        return parent_ ? parent_->GetTextureSrvIndex(name) : 0;
    }

    // ── Performance overrides (avoid string lookup on hot path) ──────────

    [[nodiscard]] float GetRoughnessMultiplier() const override {
        return hasRoughnessMultiplier_ ? roughnessMultiplier_ : (parent_ ? parent_->GetRoughnessMultiplier() : 1.0f);
    }
    [[nodiscard]] float GetMetallicMultiplier() const override {
        return hasMetallicMultiplier_ ? metallicMultiplier_ : (parent_ ? parent_->GetMetallicMultiplier() : 1.0f);
    }
    [[nodiscard]] float GetNormalIntensity() const override {
        return hasNormalIntensity_ ? normalIntensity_ : (parent_ ? parent_->GetNormalIntensity() : 1.0f);
    }
    [[nodiscard]] float GetAoIntensity() const override {
        return hasAoIntensity_ ? aoIntensity_ : (parent_ ? parent_->GetAoIntensity() : 1.0f);
    }
    [[nodiscard]] float GetOpacity() const override {
        return hasOpacity_ ? opacity_ : (parent_ ? parent_->GetOpacity() : 1.0f);
    }
    [[nodiscard]] float GetEmissiveIntensity() const override {
        return hasEmissiveIntensity_ ? emissiveIntensity_ : (parent_ ? parent_->GetEmissiveIntensity() : 0.0f);
    }
    [[nodiscard]] float GetUvScaleX() const override {
        return hasUvScaleX_ ? uvScaleX_ : (parent_ ? parent_->GetUvScaleX() : 1.0f);
    }
    [[nodiscard]] float GetUvScaleY() const override {
        return hasUvScaleY_ ? uvScaleY_ : (parent_ ? parent_->GetUvScaleY() : 1.0f);
    }
    [[nodiscard]] float GetUvOffsetX() const override {
        return hasUvOffsetX_ ? uvOffsetX_ : (parent_ ? parent_->GetUvOffsetX() : 0.0f);
    }
    [[nodiscard]] float GetUvOffsetY() const override {
        return hasUvOffsetY_ ? uvOffsetY_ : (parent_ ? parent_->GetUvOffsetY() : 0.0f);
    }

    void GetBaseColorTint(float& r, float& g, float& b) const override {
        if (hasBaseColorTintR_ && hasBaseColorTintG_ && hasBaseColorTintB_) {
            r = baseColorTintR_;
            g = baseColorTintG_;
            b = baseColorTintB_;
        } else if (parent_) {
            parent_->GetBaseColorTint(r, g, b);
            if (hasBaseColorTintR_) r = baseColorTintR_;
            if (hasBaseColorTintG_) g = baseColorTintG_;
            if (hasBaseColorTintB_) b = baseColorTintB_;
        } else {
            r = g = b = 1.0f;
        }
    }

    void GetEmissiveColor(float& r, float& g, float& b) const override {
        if (hasEmissiveColorR_ && hasEmissiveColorG_ && hasEmissiveColorB_) {
            r = emissiveColorR_;
            g = emissiveColorG_;
            b = emissiveColorB_;
        } else if (parent_) {
            parent_->GetEmissiveColor(r, g, b);
            if (hasEmissiveColorR_) r = emissiveColorR_;
            if (hasEmissiveColorG_) g = emissiveColorG_;
            if (hasEmissiveColorB_) b = emissiveColorB_;
        } else {
            r = g = b = 0.0f;
        }
    }

    [[nodiscard]] bool IsTwoSided() const override {
        return hasTwoSided_ ? twoSided_ : (parent_ ? parent_->IsTwoSided() : false);
    }
    [[nodiscard]] bool IsAlphaTest() const override {
        return hasAlphaTest_ ? alphaTest_ : (parent_ ? parent_->IsAlphaTest() : false);
    }
    [[nodiscard]] float GetAlphaClipThreshold() const override {
        return hasAlphaClipThreshold_ ? alphaClipThreshold_ : (parent_ ? parent_->GetAlphaClipThreshold() : 0.3333f);
    }

    [[nodiscard]] bool IsGpuResourcesLoaded() const override {
        return parent_ ? parent_->IsGpuResourcesLoaded() : false;
    }

    // ── Legacy compatibility ─────────────────────────────────────────────
    // These provide backward-compatible access using the old naming convention
    // (GetEffective*) that existing code may reference.

    [[nodiscard]] float GetEffectiveRoughnessMultiplier() const noexcept { return GetRoughnessMultiplier(); }
    [[nodiscard]] float GetEffectiveMetallicMultiplier() const noexcept  { return GetMetallicMultiplier(); }
    [[nodiscard]] float GetEffectiveNormalIntensity() const noexcept     { return GetNormalIntensity(); }
    [[nodiscard]] float GetEffectiveAoIntensity() const noexcept         { return GetAoIntensity(); }
    [[nodiscard]] float GetEffectiveOpacity() const noexcept             { return GetOpacity(); }
    [[nodiscard]] float GetEffectiveEmissiveIntensity() const noexcept   { return GetEmissiveIntensity(); }
    [[nodiscard]] float GetEffectiveUvScaleX() const noexcept            { return GetUvScaleX(); }
    [[nodiscard]] float GetEffectiveUvScaleY() const noexcept            { return GetUvScaleY(); }
    [[nodiscard]] float GetEffectiveUvOffsetX() const noexcept           { return GetUvOffsetX(); }
    [[nodiscard]] float GetEffectiveUvOffsetY() const noexcept           { return GetUvOffsetY(); }

    [[nodiscard]] const std::string& GetEffectiveAlbedoPath() const noexcept    { return GetAlbedoPath(); }
    [[nodiscard]] const std::string& GetEffectiveNormalPath() const noexcept     { return GetNormalPath(); }
    [[nodiscard]] const std::string& GetEffectiveRoughnessPath() const noexcept  { return GetRoughnessPath(); }
    [[nodiscard]] const std::string& GetEffectiveMetallicPath() const noexcept   { return GetMetallicPath(); }
    [[nodiscard]] const std::string& GetEffectiveAoPath() const noexcept         { return GetAoPath(); }
    [[nodiscard]] const std::string& GetEffectiveEmissivePath() const noexcept   { return GetEmissivePath(); }

private:
    /// @source UE5 MaterialInstance.h:23 — Parent is UMaterialInterface*
    /// Allows chaining: MaterialInstance -> MaterialInstance -> Material.
    MaterialInterface* parent_ = nullptr;

    // ── Override values + flags ──────────────────────────────────────────

    float baseColorTintR_ = 1.0f;   bool hasBaseColorTintR_ = false;
    float baseColorTintG_ = 1.0f;   bool hasBaseColorTintG_ = false;
    float baseColorTintB_ = 1.0f;   bool hasBaseColorTintB_ = false;

    float roughnessMultiplier_ = 1.0f; bool hasRoughnessMultiplier_ = false;
    float metallicMultiplier_  = 1.0f; bool hasMetallicMultiplier_  = false;
    float normalIntensity_     = 1.0f; bool hasNormalIntensity_     = false;
    float aoIntensity_         = 1.0f; bool hasAoIntensity_         = false;
    float opacity_             = 1.0f; bool hasOpacity_             = false;

    float emissiveColorR_    = 0.0f; bool hasEmissiveColorR_    = false;
    float emissiveColorG_    = 0.0f; bool hasEmissiveColorG_    = false;
    float emissiveColorB_    = 0.0f; bool hasEmissiveColorB_    = false;
    float emissiveIntensity_ = 0.0f; bool hasEmissiveIntensity_ = false;

    float uvScaleX_  = 1.0f; bool hasUvScaleX_  = false;
    float uvScaleY_  = 1.0f; bool hasUvScaleY_  = false;
    float uvOffsetX_ = 0.0f; bool hasUvOffsetX_ = false;
    float uvOffsetY_ = 0.0f; bool hasUvOffsetY_ = false;

    bool twoSided_ = false;     bool hasTwoSided_ = false;
    bool alphaTest_ = false;    bool hasAlphaTest_ = false;
    float alphaClipThreshold_ = 0.3333f; bool hasAlphaClipThreshold_ = false;

    // ── Texture path overrides (empty = inherit from parent) ────────────

    std::string albedoPath_;    bool hasAlbedoPath_    = false;
    std::string normalPath_;    bool hasNormalPath_    = false;
    std::string roughnessPath_; bool hasRoughnessPath_ = false;
    std::string metallicPath_;  bool hasMetallicPath_  = false;
    std::string aoPath_;        bool hasAoPath_        = false;
    std::string emissivePath_;  bool hasEmissivePath_  = false;

    /// Returned by GetTexturePath when no override is set and no parent exists.
    static inline const std::string s_emptyString_;
};

} // namespace uz
