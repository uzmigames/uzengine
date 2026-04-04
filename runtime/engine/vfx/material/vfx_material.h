/// VFX Material — particle material integrated with the engine material system.
///
/// Inherits from MaterialInterface so VFX particles can participate in the
/// same material pipeline as meshes: shared textures, material instances,
/// hot-reload, and editor parameter inspection.
///
/// VFX-specific extensions beyond MaterialInterface:
/// - Flipbook/SubUV animation (rows, cols, totalFrames, fps, blend mode)
/// - Soft particle fade distance
/// - Alpha erosion
/// - Color ramp LUT (GPU-baked curve for color-over-life)
///
/// # Flipbook Auto-Detection
/// When a sprite texture is loaded, `LoadTexture()` scans the filename for
/// a grid pattern (e.g., `_6x6_` or `_4x4.tga`) and automatically sets
/// `flipbookRows`, `flipbookCols`, and `flipbookTotalFrames`.
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the thread that owns the command list.

#pragma once

#include "engine/renderer/material/material_interface.h"

#include <cstdint>
#include <string>

namespace uz {

class IRHIDevice;
class IRHICommandList;

// ─────────────────────────────────────────────────────────────────────────────
// VFXMaterial — particle material with MaterialInterface integration
// ─────────────────────────────────────────────────────────────────────────────

/// Particle material that inherits from MaterialInterface.
///
/// Exposes all standard material parameters (blend mode, textures, shading model)
/// plus VFX-specific extensions (flipbook, soft fade, color ramp). Can be used
/// as a MaterialInstance parent for per-emitter overrides.
class VFXMaterial : public MaterialInterface {
public:
    // ── MaterialInterface overrides ─────────────────────────────────────

    [[nodiscard]] float GetScalarParam(const std::string& name, float defaultVal) const override;
    void GetVectorParam(const std::string& name, float& outR, float& outG, float& outB,
                        float defaultR, float defaultG, float defaultB) const override;
    [[nodiscard]] const std::string& GetTexturePath(const std::string& name) const override;
    [[nodiscard]] uint32_t GetTextureSrvIndex(const std::string& name) const override;

    [[nodiscard]] EBlendMode GetBlendMode() const override;
    [[nodiscard]] EMaterialDomain GetMaterialDomain() const override { return EMaterialDomain::Particle; }
    [[nodiscard]] EShadingModel GetShadingModel() const override;
    [[nodiscard]] bool IsTwoSided() const override { return isTwoSided; }
    [[nodiscard]] float GetEmissiveIntensity() const override { return emissiveIntensity; }
    [[nodiscard]] float GetOpacity() const override { return 1.0f - alphaErosion; }
    [[nodiscard]] bool IsGpuResourcesLoaded() const override { return HasSprite(); }
    void SetGpuResourcesLoaded(bool loaded) override { gpuLoaded = loaded; }

    // ── Sprite texture ──────────────────────────────────────────────────

    std::string spriteTexturePath;                    ///< File path to sprite sheet (TGA/PNG/JPEG).
    uint32_t    spriteTextureSrvIndex = UINT32_MAX;   ///< Bindless SRV index. UINT32_MAX = not loaded.

    // ── Blend mode ──────────────────────────────────────────────────────

    /// 0 = AlphaBlend (SrcAlpha/InvSrcAlpha)
    /// 1 = Additive   (One/One)
    /// 2 = PremulAlpha (One/InvSrcAlpha)
    uint32_t blendMode = 1;

    // ── Rendering parameters ────────────────────────────────────────────

    float emissiveIntensity = 1.0f;   ///< Additive emissive multiplier (0 = no emission).
    float softFadeDistance  = 100.0f; ///< Soft particle fade distance (cm).
    float alphaErosion     = 0.0f;    ///< Alpha erosion threshold [0, 1].

    // ── Flipbook / SubUV ────────────────────────────────────────────────

    uint32_t flipbookRows        = 1;    ///< Rows in sprite sheet. 1 = no flipbook.
    uint32_t flipbookCols        = 1;    ///< Columns in sprite sheet. 1 = no flipbook.
    uint32_t flipbookTotalFrames = 1;    ///< Total valid frames in the sheet.
    float    flipbookFPS         = 0.0f; ///< Playback speed (frames/sec). 0 = static.
    uint32_t flipbookBlendMode   = 0;    ///< 0 = None, 1 = Linear, 2 = Random.

    // ── Color tint ──────────────────────────────────────────────────────

    float baseColorTintR = 1.0f;
    float baseColorTintG = 1.0f;
    float baseColorTintB = 1.0f;

    // ── Color ramp ──────────────────────────────────────────────────────

    uint32_t colorRampLutSrvIndex = UINT32_MAX; ///< Bindless SRV for 256x1 LUT.

    // ── Flags ───────────────────────────────────────────────────────────

    bool isUnlit    = true;   ///< VFX particles default to unlit.
    bool isTwoSided = true;   ///< Billboards are always double-sided.

    // ── Methods ─────────────────────────────────────────────────────────

    /// Loads the sprite texture from `spriteTexturePath` via TextureLoader.
    /// Auto-detects flipbook grid dimensions from filename (e.g., `_6x6_`).
    bool LoadTexture(IRHIDevice* device, IRHICommandList* cmdList, bool isSRGB = true);

    /// Returns true if a sprite texture has been successfully loaded.
    [[nodiscard]] bool HasSprite() const noexcept {
        return spriteTextureSrvIndex != UINT32_MAX;
    }

private:
    bool gpuLoaded = false;
    static const std::string kEmptyString; ///< Returned by GetTexturePath for unknown slots.
};

} // namespace uz
