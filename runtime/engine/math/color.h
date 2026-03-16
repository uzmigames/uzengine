/// UzEngine Color Library — Linear RGB and sRGB color types with gamma-correct conversions.
///
/// Two color types for the rendering pipeline:
/// - `LinearColor` — `float4` in linear RGB space for shader math (HDR-capable, unbounded)
/// - `sRGBColor` — `uint8_t[4]` in sRGB space for storage, textures, and UI (0–255)
///
/// # Gamma Correction
/// sRGB uses a piecewise gamma curve (IEC 61966-2-1):
/// - Linear segment below 0.0031308 (multiplied by 12.92)
/// - Power curve above: `1.055 * x^(1/2.4) - 0.055`
///
/// Always convert to linear before blending/lighting, then back to sRGB for display.
///
/// # Thread Safety
/// All types are trivially copyable value types. Thread-safe by value.
///
/// # Examples
/// ```cpp
/// LinearColor sun{1.5f, 1.4f, 1.0f};      // HDR sunlight
/// sRGBColor ui = sun.ToSRGB();              // clamps + gamma for display
/// LinearColor back = ui.ToLinear();          // round-trip (lossy — 8-bit precision)
///
/// auto blended = LinearColor::Lerp(colorA, colorB, 0.5f); // linear-space blend
/// ```

#pragma once

#include <cmath>
#include <cstdint>

namespace uz {

// Forward declarations
struct LinearColor;
struct sRGBColor;

// ─────────────────────────────────────────────────────────────────────────────
// LinearColor — Float4 in linear RGB space (for rendering)
// ─────────────────────────────────────────────────────────────────────────────

/// HDR color in linear RGB space — used for all rendering calculations.
///
/// Components are unbounded floats (can exceed 1.0 for HDR). Negative values are valid
/// for intermediate calculations but should be clamped before display.
/// Alpha defaults to 1.0 (fully opaque).
///
/// # Memory Layout
/// `[r: f32, g: f32, b: f32, a: f32]` — 16 bytes, compatible with HLSL `float4`.
struct LinearColor {
    /// Red channel (0.0 = none, 1.0 = full, >1.0 = HDR).
    float r;
    /// Green channel.
    float g;
    /// Blue channel.
    float b;
    /// Alpha channel (0.0 = transparent, 1.0 = opaque).
    float a;

    /// Default: black with full alpha (0, 0, 0, 1).
    LinearColor() : r(0), g(0), b(0), a(1.0f) {}
    /// Construct from explicit RGBA components.
    LinearColor(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}
    /// Construct uniform gray (gray, gray, gray, 1).
    explicit LinearColor(float gray) : r(gray), g(gray), b(gray), a(1.0f) {}

    /// Component-wise addition.
    LinearColor operator+(const LinearColor& other) const {
        return {r + other.r, g + other.g, b + other.b, a + other.a};
    }
    /// Scalar multiplication (brightness adjustment).
    LinearColor operator*(float scalar) const {
        return {r * scalar, g * scalar, b * scalar, a * scalar};
    }

    /// Converts to sRGB color space by applying the gamma curve.
    ///
    /// # Returns
    /// 8-bit sRGB color. Values are clamped to [0, 255]. HDR values above 1.0 saturate to 255.
    sRGBColor ToSRGB() const;

    // ─────────────────────────────────────────────────────────────────────────
    // Predefined named colors (linear space)
    // ─────────────────────────────────────────────────────────────────────────

    /// Returns pure white (1, 1, 1, 1) in linear space.
    static LinearColor White() { return {1, 1, 1, 1}; }
    /// Returns pure black (0, 0, 0, 1) in linear space — fully opaque, zero luminance.
    static LinearColor Black() { return {0, 0, 0, 1}; }
    /// Returns pure red (1, 0, 0, 1) in linear space — full red channel only.
    static LinearColor Red() { return {1, 0, 0, 1}; }
    /// Returns pure green (0, 1, 0, 1) in linear space — full green channel only.
    static LinearColor Green() { return {0, 1, 0, 1}; }
    /// Returns pure blue (0, 0, 1, 1) in linear space — full blue channel only.
    static LinearColor Blue() { return {0, 0, 1, 1}; }

    /// Linear interpolation between two colors in linear space.
    ///
    /// # Arguments
    /// * `a` - Start color (t=0)
    /// * `b` - End color (t=1)
    /// * `t` - Interpolation factor, typically [0, 1] but unclamped
    ///
    /// # Returns
    /// Component-wise lerp: `a + (b - a) * t`.
    static LinearColor Lerp(const LinearColor& a, const LinearColor& b, float t) {
        return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
                a.a + (b.a - a.a) * t};
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// sRGBColor — Byte4 in sRGB color space (for storage/UI)
// ─────────────────────────────────────────────────────────────────────────────

/// LDR color in sRGB color space — used for storage, UI, and texture sampling.
///
/// Components are 0–255 bytes. Alpha defaults to 255 (fully opaque).
/// Alpha is stored linearly (NOT gamma-encoded) — only RGB channels have the sRGB curve.
///
/// # Memory Layout
/// `[r: u8, g: u8, b: u8, a: u8]` — 4 bytes, compatible with RGBA8 texture formats.
struct sRGBColor {
    /// Red channel (0 = none, 255 = full intensity in sRGB space).
    uint8_t r;
    /// Green channel.
    uint8_t g;
    /// Blue channel.
    uint8_t b;
    /// Alpha channel (0 = transparent, 255 = opaque). Stored linearly.
    uint8_t a;

    /// Default: black with full alpha (0, 0, 0, 255).
    sRGBColor() : r(0), g(0), b(0), a(255) {}
    /// Construct from explicit RGBA byte components.
    sRGBColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) : r(r), g(g), b(b), a(a) {}

    /// Converts to linear RGB space by removing the sRGB gamma curve.
    ///
    /// # Returns
    /// Float color in [0, 1] range. Use for shader math, blending, and lighting.
    LinearColor ToLinear() const;

    /// Packs RGBA into a single 32-bit integer (R=MSB, A=LSB): `0xRRGGBBAA`.
    ///
    /// # Returns
    /// 32-bit packed color suitable for hex literals (e.g., `0xFF0000FF` = red).
    uint32_t ToHex() const {
        return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)a;
    }
    /// Unpacks RGBA from a 32-bit integer in `0xRRGGBBAA` layout.
    ///
    /// # Arguments
    /// * `hex` — Packed color (e.g., `0xFF0000FF` = red, `0xFFFFFFFF` = white).
    ///
    /// # Returns
    /// sRGBColor with each channel extracted from the corresponding byte.
    static sRGBColor FromHex(uint32_t hex) {
        return {(uint8_t)((hex >> 24) & 0xFF), (uint8_t)((hex >> 16) & 0xFF),
                (uint8_t)((hex >> 8) & 0xFF), (uint8_t)(hex & 0xFF)};
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Predefined named colors (sRGB space)
    // ─────────────────────────────────────────────────────────────────────────

    /// Returns pure white (255, 255, 255, 255) in sRGB space.
    static sRGBColor White() { return {255, 255, 255, 255}; }
    /// Returns pure black (0, 0, 0, 255) in sRGB space — fully opaque, zero luminance.
    static sRGBColor Black() { return {0, 0, 0, 255}; }
    /// Returns pure red (255, 0, 0, 255) in sRGB space — full red channel only.
    static sRGBColor Red() { return {255, 0, 0, 255}; }
    /// Returns pure green (0, 255, 0, 255) in sRGB space — full green channel only.
    static sRGBColor Green() { return {0, 255, 0, 255}; }
    /// Returns pure blue (0, 0, 255, 255) in sRGB space — full blue channel only.
    static sRGBColor Blue() { return {0, 0, 255, 255}; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Gamma correction utilities
// ─────────────────────────────────────────────────────────────────────────────

/// Convert a single float from linear to sRGB space (IEC 61966-2-1 transfer function).
///
/// # Arguments
/// * `linear` - Value in linear space, typically [0, 1]
///
/// # Returns
/// sRGB-encoded value. Input is NOT clamped — values outside [0, 1] produce extrapolated results.
inline float linear_to_srgb(float linear) {
    if (linear <= 0.0031308f) {
        return linear * 12.92f;
    } else {
        return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    }
}

/// Convert a single float from sRGB to linear space (inverse IEC 61966-2-1).
///
/// # Arguments
/// * `srgb` - Value in sRGB space, typically [0, 1]
///
/// # Returns
/// Linear-space value for shader math.
inline float srgb_to_linear(float srgb) {
    if (srgb <= 0.04045f) {
        return srgb / 12.92f;
    } else {
        return std::pow((srgb + 0.055f) / 1.055f, 2.4f);
    }
}

/// Convert LinearColor to sRGBColor
inline sRGBColor linear_to_srgb_color(const LinearColor& linear) {
    return {(uint8_t)(linear_to_srgb(linear.r) * 255.0f + 0.5f),
            (uint8_t)(linear_to_srgb(linear.g) * 255.0f + 0.5f),
            (uint8_t)(linear_to_srgb(linear.b) * 255.0f + 0.5f),
            (uint8_t)(linear.a * 255.0f + 0.5f)};
}

/// Convert sRGBColor to LinearColor
inline LinearColor srgb_to_linear_color(const sRGBColor& srgb) {
    return {srgb_to_linear(srgb.r / 255.0f), srgb_to_linear(srgb.g / 255.0f),
            srgb_to_linear(srgb.b / 255.0f), srgb.a / 255.0f};
}

// ─────────────────────────────────────────────────────────────────────────────
// Inline implementations (declared forward, defined after all types)
// ─────────────────────────────────────────────────────────────────────────────

inline sRGBColor LinearColor::ToSRGB() const {
    return linear_to_srgb_color(*this);
}

inline LinearColor sRGBColor::ToLinear() const {
    return srgb_to_linear_color(*this);
}

} // namespace uz
