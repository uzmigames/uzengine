/// UzEngine Color Library
///
/// LinearColor (float4) and sRGBColor (byte4) with gamma-correct conversions.

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
/// Components are unbounded floats (can exceed 1.0 for HDR).
/// Alpha defaults to 1.0 (fully opaque).
struct LinearColor {
    float r, g, b, a;

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

    /// Converts to sRGB color space (applies gamma curve).
    sRGBColor ToSRGB() const;

    /// Named colors
    static LinearColor White() { return {1, 1, 1, 1}; }
    static LinearColor Black() { return {0, 0, 0, 1}; }
    static LinearColor Red() { return {1, 0, 0, 1}; }
    static LinearColor Green() { return {0, 1, 0, 1}; }
    static LinearColor Blue() { return {0, 0, 1, 1}; }

    /// Interpolation
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
struct sRGBColor {
    uint8_t r, g, b, a;

    /// Default: black with full alpha (0, 0, 0, 255).
    sRGBColor() : r(0), g(0), b(0), a(255) {}
    /// Construct from explicit RGBA byte components.
    sRGBColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) : r(r), g(g), b(b), a(a) {}

    /// Converts to linear RGB space (removes gamma curve).
    LinearColor ToLinear() const;
    /// Packs RGBA into a single 32-bit integer (R=MSB, A=LSB).
    uint32_t ToHex() const {
        return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)a;
    }
    /// Unpacks RGBA from a 32-bit integer.
    static sRGBColor FromHex(uint32_t hex) {
        return {(uint8_t)((hex >> 24) & 0xFF), (uint8_t)((hex >> 16) & 0xFF),
                (uint8_t)((hex >> 8) & 0xFF), (uint8_t)(hex & 0xFF)};
    }

    /// Named colors
    static sRGBColor White() { return {255, 255, 255, 255}; }
    static sRGBColor Black() { return {0, 0, 0, 255}; }
    static sRGBColor Red() { return {255, 0, 0, 255}; }
    static sRGBColor Green() { return {0, 255, 0, 255}; }
    static sRGBColor Blue() { return {0, 0, 255, 255}; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Gamma correction utilities
// ─────────────────────────────────────────────────────────────────────────────

/// Convert single float from linear to sRGB
inline float linear_to_srgb(float linear) {
    if (linear <= 0.0031308f) {
        return linear * 12.92f;
    } else {
        return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    }
}

/// Convert single float from sRGB to linear
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
