#pragma once

/// UzEngine Color Types — LinearColor (HDR rendering) and sRGBColor (storage/UI).

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace uz {

struct sRGBColor;

// ── LinearColor (float, linear space — used in rendering) ────────────────────

struct LinearColor {
    float r, g, b, a;

    LinearColor() : r(0.0f), g(0.0f), b(0.0f), a(1.0f) {}
    LinearColor(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}

    LinearColor operator+(LinearColor c) const { return {r + c.r, g + c.g, b + c.b, a + c.a}; }
    LinearColor operator-(LinearColor c) const { return {r - c.r, g - c.g, b - c.b, a - c.a}; }
    LinearColor operator*(float s) const { return {r * s, g * s, b * s, a * s}; }
    LinearColor operator*(LinearColor c) const { return {r * c.r, g * c.g, b * c.b, a * c.a}; }

    bool operator==(LinearColor c) const { return r == c.r && g == c.g && b == c.b && a == c.a; }
    bool operator!=(LinearColor c) const { return !(*this == c); }

    sRGBColor ToSRGB() const;

    static LinearColor Lerp(LinearColor a, LinearColor b, float t) {
        return {
            a.r + (b.r - a.r) * t,
            a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t
        };
    }

    // Named colors (linear space)
    static LinearColor Black() { return {0, 0, 0, 1}; }
    static LinearColor White() { return {1, 1, 1, 1}; }
    static LinearColor Red() { return {1, 0, 0, 1}; }
    static LinearColor Green() { return {0, 1, 0, 1}; }
    static LinearColor Blue() { return {0, 0, 1, 1}; }
    static LinearColor Transparent() { return {0, 0, 0, 0}; }
};

inline LinearColor operator*(float s, LinearColor c) { return c * s; }

// ── sRGBColor (uint8, sRGB space — used in storage/UI) ──────────────────────

struct sRGBColor {
    uint8_t r, g, b, a;

    sRGBColor() : r(0), g(0), b(0), a(255) {}
    sRGBColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) : r(r), g(g), b(b), a(a) {}

    bool operator==(sRGBColor c) const { return r == c.r && g == c.g && b == c.b && a == c.a; }
    bool operator!=(sRGBColor c) const { return !(*this == c); }

    LinearColor ToLinear() const;

    static sRGBColor FromHex(uint32_t hex) {
        return {
            static_cast<uint8_t>((hex >> 24) & 0xFF),
            static_cast<uint8_t>((hex >> 16) & 0xFF),
            static_cast<uint8_t>((hex >> 8) & 0xFF),
            static_cast<uint8_t>(hex & 0xFF)
        };
    }

    uint32_t ToHex() const {
        return (static_cast<uint32_t>(r) << 24) |
               (static_cast<uint32_t>(g) << 16) |
               (static_cast<uint32_t>(b) << 8)  |
               static_cast<uint32_t>(a);
    }
};

// ── sRGB ↔ Linear conversion (IEC 61966-2-1 standard) ───────────────────────

namespace detail {

inline float SRGBToLinearChannel(float s) {
    return (s <= 0.04045f) ? (s / 12.92f) : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

inline float LinearToSRGBChannel(float l) {
    l = std::max(0.0f, std::min(1.0f, l));
    return (l <= 0.0031308f) ? (l * 12.92f) : (1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f);
}

} // namespace detail

inline LinearColor sRGBColor::ToLinear() const {
    return LinearColor(
        detail::SRGBToLinearChannel(r / 255.0f),
        detail::SRGBToLinearChannel(g / 255.0f),
        detail::SRGBToLinearChannel(b / 255.0f),
        a / 255.0f
    );
}

inline sRGBColor LinearColor::ToSRGB() const {
    return sRGBColor(
        static_cast<uint8_t>(detail::LinearToSRGBChannel(r) * 255.0f + 0.5f),
        static_cast<uint8_t>(detail::LinearToSRGBChannel(g) * 255.0f + 0.5f),
        static_cast<uint8_t>(detail::LinearToSRGBChannel(b) * 255.0f + 0.5f),
        static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, a)) * 255.0f + 0.5f)
    );
}

} // namespace uz
