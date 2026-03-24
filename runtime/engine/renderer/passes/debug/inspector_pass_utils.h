/// UzEngine Pass Inspector — Internal utilities shared across translation units.
///
/// Contains format introspection helpers (HalfToFloat, ChannelCount, ReadChannel,
/// IsDepthFormat) used by both validation and dump code paths. This header is an
/// implementation detail — not part of the public API.

#pragma once

#include "rhi_types.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace uz::detail {

/// Converts an IEEE 754 half-precision (binary16) value to single-precision float.
/// Handles normalized values, denormals, zero, infinity, and NaN.
/// Uses bit manipulation for portability (no intrinsics).
[[nodiscard]] inline float HalfToFloat(uint16_t h) noexcept
{
    const uint32_t sign     = (static_cast<uint32_t>(h) & 0x8000u) << 16;
    const uint32_t exponent = (static_cast<uint32_t>(h) >> 10) & 0x1Fu;
    const uint32_t mantissa =  static_cast<uint32_t>(h) & 0x03FFu;

    uint32_t result = 0;

    if (exponent == 0) {
        if (mantissa == 0) {
            // Signed zero
            result = sign;
        } else {
            // Denormalized — normalize by shifting mantissa until the implicit 1 bit appears
            uint32_t m = mantissa;
            uint32_t e = 0;
            while ((m & 0x0400u) == 0) {
                m <<= 1;
                ++e;
            }
            m &= 0x03FFu;
            result = sign | ((127u - 15u + 1u - e) << 23) | (m << 13);
        }
    } else if (exponent == 31) {
        // Infinity or NaN — preserve mantissa bits for NaN payload
        result = sign | 0x7F800000u | (mantissa << 13);
    } else {
        // Normalized — re-bias exponent from half (bias 15) to float (bias 127)
        result = sign | ((exponent + 127u - 15u) << 23) | (mantissa << 13);
    }

    float f;
    std::memcpy(&f, &result, sizeof(f));
    return f;
}

/// Returns the number of float-equivalent channels in a format for validation sampling.
/// For formats with < 4 channels, only the present channels are sampled.
[[nodiscard]] inline uint32_t ChannelCount(RHIFormat format) noexcept
{
    switch (format) {
        case RHIFormat::R8Unorm:        return 1;
        case RHIFormat::RG8Unorm:       return 2;
        case RHIFormat::RGBA8Unorm:     return 4;
        case RHIFormat::RGBA8Srgb:      return 4;
        case RHIFormat::BGRA8Unorm:     return 4;
        case RHIFormat::BGRA8Srgb:      return 4;
        case RHIFormat::R16Float:       return 1;
        case RHIFormat::RG16Float:      return 2;
        case RHIFormat::RGBA16Float:    return 4;
        case RHIFormat::R32Float:       return 1;
        case RHIFormat::RG32Float:      return 2;
        case RHIFormat::RGB32Float:     return 3;
        case RHIFormat::RGBA32Float:    return 4;
        case RHIFormat::R32Uint:        return 1;
        case RHIFormat::RG32Uint:       return 2;
        case RHIFormat::RGBA32Uint:     return 4;
        case RHIFormat::D16Unorm:       return 1;
        case RHIFormat::D32Float:       return 1;
        case RHIFormat::D24UnormS8Uint: return 1; // Only sample the depth channel
        case RHIFormat::D32FloatS8Uint: return 1; // Only sample the depth channel
        case RHIFormat::BC1Unorm:       return 4; // RGBA (1-bit alpha)
        case RHIFormat::BC1Srgb:        return 4;
        case RHIFormat::BC3Unorm:       return 4; // RGBA (8-bit alpha)
        case RHIFormat::BC3Srgb:        return 4;
        case RHIFormat::BC4Unorm:       return 1; // R only
        case RHIFormat::BC5Unorm:       return 2; // RG only
        case RHIFormat::BC7Unorm:       return 4; // RGBA
        case RHIFormat::BC7Srgb:        return 4;
        default:                        return 0;
    }
}

/// Reads a single channel value from raw readback data at the given byte offset,
/// converting to float according to the texture format.
[[nodiscard]] inline float ReadChannel(const uint8_t* texelBase, uint32_t channelIndex,
                                       RHIFormat format) noexcept
{
    switch (format) {
        // ── 8-bit unsigned normalized ────────────────────────────────────
        case RHIFormat::R8Unorm:
            return static_cast<float>(texelBase[channelIndex]) / 255.0f;
        case RHIFormat::RG8Unorm:
            return static_cast<float>(texelBase[channelIndex]) / 255.0f;
        case RHIFormat::RGBA8Unorm:
        case RHIFormat::RGBA8Srgb:
            return static_cast<float>(texelBase[channelIndex]) / 255.0f;
        case RHIFormat::BGRA8Unorm:
        case RHIFormat::BGRA8Srgb:
            // BGRA layout: byte 0=B, 1=G, 2=R, 3=A. Remap to logical RGBA order.
            {
                static constexpr uint32_t bgraToRgba[4] = {2, 1, 0, 3};
                return static_cast<float>(texelBase[bgraToRgba[channelIndex]]) / 255.0f;
            }

        // ── 16-bit half-float ────────────────────────────────────────────
        case RHIFormat::R16Float:
        case RHIFormat::RG16Float:
        case RHIFormat::RGBA16Float: {
            uint16_t halfBits;
            std::memcpy(&halfBits, texelBase + channelIndex * sizeof(uint16_t), sizeof(uint16_t));
            return HalfToFloat(halfBits);
        }

        // ── 32-bit float ─────────────────────────────────────────────────
        case RHIFormat::R32Float:
        case RHIFormat::RG32Float:
        case RHIFormat::RGB32Float:
        case RHIFormat::RGBA32Float: {
            float val;
            std::memcpy(&val, texelBase + channelIndex * sizeof(float), sizeof(float));
            return val;
        }

        // ── 32-bit unsigned integer (reinterpret as float for range check) ──
        case RHIFormat::R32Uint:
        case RHIFormat::RG32Uint:
        case RHIFormat::RGBA32Uint: {
            uint32_t uval;
            std::memcpy(&uval, texelBase + channelIndex * sizeof(uint32_t), sizeof(uint32_t));
            return static_cast<float>(uval);
        }

        // ── Depth formats ────────────────────────────────────────────────
        case RHIFormat::D16Unorm: {
            uint16_t d16;
            std::memcpy(&d16, texelBase, sizeof(uint16_t));
            return static_cast<float>(d16) / 65535.0f;
        }
        case RHIFormat::D32Float:
        case RHIFormat::D32FloatS8Uint: {
            float depth;
            std::memcpy(&depth, texelBase, sizeof(float));
            return depth;
        }
        case RHIFormat::D24UnormS8Uint: {
            uint32_t packed;
            std::memcpy(&packed, texelBase, sizeof(uint32_t));
            return static_cast<float>(packed & 0x00FFFFFFu) / 16777215.0f;
        }

        default:
            return 0.0f;
    }
}

/// Returns true if the given format is a depth/stencil format.
[[nodiscard]] inline bool IsDepthFormat(RHIFormat format) noexcept
{
    return format == RHIFormat::D16Unorm
        || format == RHIFormat::D32Float
        || format == RHIFormat::D24UnormS8Uint
        || format == RHIFormat::D32FloatS8Uint;
}

} // namespace uz::detail
