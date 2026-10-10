#pragma once

/// VFX UI Icon Atlas — phase 13.1.0.
///
/// Stable enum of every editor UI icon + compile-time UV table into a
/// procedural 256x256 RGBA8 atlas (8x8 grid of 32x32 cells, 64 slots).
/// The atlas pixels are generated procedurally in `vfx_icon_atlas.cpp`
/// at runtime so no binary PNG asset lives in the repo. Each icon
/// renders as a monochrome white-on-transparent glyph; the
/// `AddSprite(icon, x, y, w, h, tint)` call multiplies the sampled
/// RGB by the caller-supplied tint so a single atlas serves every
/// accent color.
///
/// @ref runtime/editor/font/text_renderer.h (AddSprite API)

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Icon enum — numeric identity stable across builds
// ─────────────────────────────────────────────────────────────────────────────

enum class VfxUiIcon : uint16_t {
    None = 0,

    // Transport (row 0)
    Play       = 1,
    Pause      = 2,
    Stop       = 3,
    StepBack   = 4,
    StepFwd    = 5,
    JumpStart  = 6,
    JumpEnd    = 7,
    Restart    = 8,

    // Toggle / state (row 1)
    Loop       = 9,
    Check      = 10,
    Cross      = 11,
    Plus       = 12,
    Minus      = 13,
    Dot        = 14,
    Star       = 15,
    Warning    = 16,

    // Navigation (row 2)
    ChevronUp    = 17,
    ChevronDown  = 18,
    ChevronLeft  = 19,
    ChevronRight = 20,
    ArrowUp      = 21,
    ArrowDown    = 22,
    ArrowLeft    = 23,
    ArrowRight   = 24,

    // Drag / menu (row 3)
    GripDots     = 25,
    Overflow     = 26,
    Search       = 27,
    Gear         = 28,
    Reset        = 29,
    BindLink     = 30,
    Lock         = 31,
    Unlock       = 32,

    // Curves / geometry (row 4) — phase 13.5 additions
    Diamond      = 33,
    Circle       = 34,

    Count        = 35,
};

// ─────────────────────────────────────────────────────────────────────────────
// UV rect table
// ─────────────────────────────────────────────────────────────────────────────

struct VfxIconUv {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

/// Returns the UV rect for `icon` into the 256x256 atlas. For
/// `VfxUiIcon::None` returns a zero-area rect. All UVs map into the
/// 32x32 cell centered on a 24x24 drawn glyph (4-pixel padding).
[[nodiscard]] VfxIconUv VfxGetIconUv(VfxUiIcon icon) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Atlas generation
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr uint32_t kVfxIconAtlasWidth  = 256u;
inline constexpr uint32_t kVfxIconAtlasHeight = 256u;
inline constexpr uint32_t kVfxIconAtlasCellPx = 32u;
inline constexpr uint32_t kVfxIconAtlasCols   = 8u;
inline constexpr uint32_t kVfxIconAtlasRows   = 8u;

/// Generates the procedural RGBA8 icon atlas pixels into `outPixels`.
///
/// `outPixels` MUST point to a buffer of at least
/// `kVfxIconAtlasWidth * kVfxIconAtlasHeight * 4` bytes. The buffer
/// is overwritten with opaque-white glyphs on fully-transparent
/// background so sampling with alpha blending composites cleanly over
/// any panel color.
void VfxGenerateIconAtlas(uint8_t* outPixels) noexcept;

} // namespace uz
