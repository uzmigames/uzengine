#pragma once

/// VFX Color Ramp -- multi-stop RGBA gradient baked to a 256x1 GPU LUT.
///
/// Evaluates a piecewise-linear color gradient defined by up to 8 color stops.
/// Each stop has a position in [0,1] and an RGBA color. The ramp is baked to
/// 256 RGBA16F texels for efficient GPU sampling in particle pixel shaders.
///
/// The GPU LUT replaces the legacy two-point color-over-lifetime lerp with
/// complex multi-stop gradients (e.g., fire: black->orange->yellow->white).
///
/// @ref GDC 2019 "Niagara: Building the Future of FX in Unreal Engine"
///     — Color over Life module with curve asset baking to 1D texture.

#include <array>
#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// IEEE 754 Half-Float Conversion
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a 32-bit float to a 16-bit IEEE 754 half-precision float.
///
/// Handles normal numbers, denormals, infinities, and NaN. Uses bit
/// manipulation only — no external library required.
///
/// @ref IEEE 754-2008 half-precision conversion
///
/// # Arguments
/// * `v` — Input single-precision float value.
///
/// # Returns
/// 16-bit half-float representation packed in a uint16_t.
[[nodiscard]] uint16_t FloatToHalf(float v) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// VFXColorKey
// ─────────────────────────────────────────────────────────────────────────────

/// A single color stop in a VFXColorRamp gradient.
///
/// # Fields
/// * `t` — Normalized position along the gradient [0, 1].
/// * `r`, `g`, `b`, `a` — Linear RGBA color at this stop.
struct VFXColorKey {
    float t;           ///< Normalized position [0, 1] along the particle lifetime.
    float r, g, b, a;  ///< Linear RGBA color at this position.
};

// ─────────────────────────────────────────────────────────────────────────────
// VFXColorRamp
// ─────────────────────────────────────────────────────────────────────────────

/// Multi-stop RGBA color gradient for VFX color-over-lifetime.
///
/// Stores up to `kMaxStops` color keys. When baked via `BakeToRGBA16F()`,
/// evaluates the piecewise-linear ramp at 256 evenly-spaced positions and
/// returns raw RGBA16F pixel data suitable for GPU texture upload.
///
/// # Usage
/// ```cpp
/// VFXColorRamp ramp;
/// ramp.AddStop(0.0f, 0.0f, 0.0f, 0.0f, 1.0f); // black, opaque
/// ramp.AddStop(0.3f, 1.0f, 0.5f, 0.0f, 1.0f); // orange
/// ramp.AddStop(1.0f, 1.0f, 1.0f, 1.0f, 0.0f); // white, transparent
///
/// auto lut = ramp.BakeToRGBA16F();
/// // Upload lut.data() as 256x1 RGBA16Float texture
/// ```
///
/// @ref GDC 2019 "Niagara: Building the Future of FX in Unreal Engine"
///     — 256-sample LUT bake for Color over Life module
struct VFXColorRamp {
    static constexpr int kMaxStops = 8;  ///< Maximum number of color stops.
    static constexpr uint32_t kLutWidth = 256; ///< LUT texture width in texels.

    VFXColorKey stops[kMaxStops] = {};   ///< Color stops (sorted by t on bake).
    int stopCount = 0;                   ///< Number of active stops.

    /// Adds a color stop to the ramp.
    ///
    /// Stops are sorted by position during `BakeToRGBA16F()`, so insertion
    /// order does not matter. If the ramp is full (`kMaxStops` reached),
    /// the stop is silently ignored.
    ///
    /// # Arguments
    /// * `pos` — Normalized position [0, 1] along the particle lifetime.
    /// * `r`, `g`, `b`, `a` — Linear RGBA color at this position.
    void AddStop(float pos, float r, float g, float b, float a);

    /// Bakes the gradient to a 256-sample RGBA16F pixel array.
    ///
    /// Sorts stops by position, then for each of the 256 texels, finds the
    /// surrounding stops and linearly interpolates RGBA. Edge cases:
    /// - 0 stops: returns all-white (1,1,1,1) — neutral fallback.
    /// - 1 stop: fills entire LUT with that stop's color.
    /// - t before first stop: clamps to first stop color.
    /// - t after last stop: clamps to last stop color.
    ///
    /// @ref GDC 2019 "Niagara: Building the Future of FX in Unreal Engine"
    ///     — 256-sample LUT bake
    ///
    /// # Returns
    /// Array of 256 * 4 = 1024 half-float values in RGBA order.
    [[nodiscard]] std::array<uint16_t, kLutWidth * 4> BakeToRGBA16F() const;
};

} // namespace uz
