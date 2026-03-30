/// VFX Material -- CPU-side material properties for particle emitters.
///
/// Encapsulates sprite texture, blend mode, emissive intensity, soft fade,
/// flipbook animation, and color tint. Mirrors Niagara's emitter material
/// slot: each emitter can optionally reference a VFXMaterial to override
/// inline rendering parameters.
///
/// # Flipbook Auto-Detection
/// When a sprite texture is loaded, `LoadTexture()` scans the filename for
/// a grid pattern (e.g., `_6x6_` or `_4x4.tga`) and automatically sets
/// `flipbookRows`, `flipbookCols`, and `flipbookTotalFrames`.
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the thread that owns the command list.

#pragma once

#include <cstdint>
#include <string>

namespace uz {

// Forward declarations -- avoids pulling in RHI headers.
class IRHIDevice;
class IRHICommandList;

// ─────────────────────────────────────────────────────────────────────────────
// VFXMaterial
// ─────────────────────────────────────────────────────────────────────────────

/// CPU-side material descriptor for VFX particle emitters.
///
/// When assigned to an emitter, the renderer reads sprite SRV, flipbook
/// config, blend mode, emissive, and soft fade from this struct instead
/// of the emitter config's inline fields.
struct VFXMaterial {
    // ── Sprite texture ──────────────────────────────────────────────────
    std::string spriteTexturePath;                    ///< File path to sprite sheet (TGA/PNG/JPEG).
    uint32_t    spriteTextureSrvIndex = UINT32_MAX;   ///< Bindless SRV index of loaded sprite. UINT32_MAX = not loaded.

    // ── Blend mode ──────────────────────────────────────────────────────
    /// 0 = AlphaBlend (SrcAlpha/InvSrcAlpha)
    /// 1 = Additive   (One/One)
    /// 2 = PremulAlpha (One/InvSrcAlpha)
    uint32_t blendMode = 1;

    // ── Rendering parameters ────────────────────────────────────────────
    float emissiveIntensity = 1.0f;   ///< Additive emissive multiplier (0 = no emission).
    float softFadeDistance  = 0.5f;   ///< World-space soft particle fade distance.
    float alphaErosion     = 0.0f;    ///< Alpha erosion threshold [0, 1].

    // ── Flipbook / SubUV ────────────────────────────────────────────────
    uint32_t flipbookRows        = 1;    ///< Rows in sprite sheet. 1 = no flipbook.
    uint32_t flipbookCols        = 1;    ///< Columns in sprite sheet. 1 = no flipbook.
    uint32_t flipbookTotalFrames = 1;    ///< Total valid frames in the sheet.
    float    flipbookFPS         = 0.0f; ///< Playback speed (frames/sec). 0 = static.
    uint32_t flipbookBlendMode   = 0;    ///< 0 = None, 1 = Linear, 2 = Random.

    // ── Color tint ──────────────────────────────────────────────────────
    float baseColorTintR = 1.0f;   ///< Material-level red tint [0, 1].
    float baseColorTintG = 1.0f;   ///< Material-level green tint [0, 1].
    float baseColorTintB = 1.0f;   ///< Material-level blue tint [0, 1].

    // ── Flags ───────────────────────────────────────────────────────────
    bool isUnlit   = true;   ///< VFX particles default to unlit (no scene lighting).
    bool isTwoSided = true;  ///< Billboards are always double-sided.

    // ── Methods ─────────────────────────────────────────────────────────

    /// Loads the sprite texture from `spriteTexturePath` via TextureLoader.
    ///
    /// On success, sets `spriteTextureSrvIndex` and auto-detects flipbook
    /// grid dimensions from the filename pattern (e.g., `_6x6_` sets
    /// rows=6, cols=6, totalFrames=36, default FPS=24).
    ///
    /// # Arguments
    /// * `device`  -- RHI device for texture creation. Must not be null.
    /// * `cmdList` -- Command list in recording state. Must not be null.
    ///
    /// # Returns
    /// `true` on success (texture loaded and SRV bound).
    /// `false` on failure (empty path, file not found, decode error).
    ///
    /// # Side Effects
    /// - Creates one GPU texture resource (caller owns lifetime).
    /// - Flushes the command list internally (TextureLoader::Load is synchronous).
    /// - Logs success/failure at VFX::Info/Error level.
    bool LoadTexture(IRHIDevice* device, IRHICommandList* cmdList);

    /// Returns true if a sprite texture has been successfully loaded.
    [[nodiscard]] bool HasSprite() const noexcept {
        return spriteTextureSrvIndex != UINT32_MAX;
    }
};

} // namespace uz
