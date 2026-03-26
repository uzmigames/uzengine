#pragma once

/// VFX Sprite Render Module -- billboard sprite rendering configuration.
///
/// Controls per-particle sprite dimensions and visual properties for the
/// rendering pass. Future versions will sample size-over-life and
/// color-over-life curves; currently uses flat values.
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.

#include "vfx_module.h"

#include <cstdint>

namespace uz {

// ---------------------------------------------------------------------------
// FlipbookBlendMode -- sprite sheet frame interpolation mode
// ---------------------------------------------------------------------------

/// Controls how flipbook (SubUV) sprite sheet frames are selected and blended.
///
/// @ref Nguyen 2007 "GPU Gems 3" ch. 23 -- standard SubUV animation modes
enum class FlipbookBlendMode : uint8_t {
    None,    ///< Snap to the nearest frame with no interpolation.
    Linear,  ///< Linearly interpolate between the current and next frame.
    Random,  ///< Each particle picks one deterministic random frame (no animation).
};

// ---------------------------------------------------------------------------
// SpriteRenderModule -- billboard sprite size and visual properties
// ---------------------------------------------------------------------------

/// Configures per-particle sprite rendering with size and color.
///
/// The HLSL snippet computes normalized age and writes final size/color to
/// the particle attribute buffers. Curve-based size-over-life and
/// color-over-life are planned (tracked via placeholder curve IDs).
///
/// # Parameters
/// | Name          | Type | Default | Description                          |
/// |---------------|------|---------|--------------------------------------|
/// | sprite_width  | Float| 1.0     | Billboard width in world units       |
/// | sprite_height | Float| 1.0     | Billboard height in world units      |
/// | depth_sort    | Bool | true    | Enable back-to-front depth sorting   |
///
/// # Future
/// `sizeOverLifeCurveId` and `colorOverLifeCurveId` will reference VFXCurve
/// assets once the curve system is implemented.
class SpriteRenderModule final : public IVFXModule {
public:
    SpriteRenderModule() = default;
    ~SpriteRenderModule() override = default;

    // -- IVFXModule interface ------------------------------------------------

    /// Returns "sprite_render".
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns HLSL snippet for sprite size and color evaluation.
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns parameter definitions for sprite configuration.
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// No-op. Rendering configuration is consumed by the render pass.
    void Execute(const ExecuteContext& ctx) override;

    // -- Configuration (public for editor/serialization) ---------------------

    /// Billboard width in world units.
    float spriteWidth = 1.0f;

    /// Billboard height in world units.
    float spriteHeight = 1.0f;

    /// Whether to sort particles back-to-front for correct alpha blending.
    bool depthSort = true;

    /// Placeholder curve ID for size-over-life (0 = no curve, use flat value).
    uint32_t sizeOverLifeCurveId = 0;

    /// Placeholder curve ID for color-over-life (0 = no curve, use flat value).
    uint32_t colorOverLifeCurveId = 0;

    // -- Flipbook / SubUV animation ------------------------------------------

    /// Number of rows in the sprite sheet atlas (1 = no flipbook).
    /// @ref Nguyen 2007 "GPU Gems 3" ch. 23 -- SubUV sheet dimensions
    uint32_t flipbookRows = 1;

    /// Number of columns in the sprite sheet atlas (1 = no flipbook).
    /// @ref Nguyen 2007 "GPU Gems 3" ch. 23 -- SubUV sheet dimensions
    uint32_t flipbookCols = 1;

    /// Total number of valid frames in the sprite sheet.
    /// Default: 1 (matches rows * cols for a 1x1 sheet). Set manually if the
    /// sheet has empty trailing frames that should be excluded from playback.
    /// @ref Nguyen 2007 "GPU Gems 3" ch. 23 -- partial sheet support
    uint32_t flipbookTotalFrames = 1;

    /// Flipbook playback speed in frames per second.
    /// Only used when flipbookBlendMode is None or Linear (animated modes).
    /// @ref Nguyen 2007 "GPU Gems 3" ch. 23 -- SubUV animation rate
    float flipbookFPS = 30.0f;

    /// Controls how flipbook frames are selected and blended.
    /// @ref Nguyen 2007 "GPU Gems 3" ch. 23 -- SubUV blend modes
    FlipbookBlendMode flipbookBlendMode = FlipbookBlendMode::None;

    /// Returns true if flipbook animation is active (sheet has more than one cell).
    [[nodiscard]] bool HasFlipbook() const noexcept {
        return flipbookRows > 1 || flipbookCols > 1;
    }
};

} // namespace uz
