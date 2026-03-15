/// UzEngine Post-Processing Math — CPU-side equivalents of shader functions.
///
/// Provides inline implementations of the tone mapping, luminance, and blur
/// kernel math used by the post-processing shaders. These functions are
/// bit-identical to their HLSL counterparts and exist so that unit tests
/// can verify correctness without a GPU.
///
/// # Usage
/// Include this header in test files only. Production code uses the HLSL
/// shaders directly on the GPU.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace uz::postprocess {

// ─────────────────────────────────────────────────────────────────────────────
// Luminance
// ─────────────────────────────────────────────────────────────────────────────

/// Compute relative luminance from linear RGB (ITU-R BT.709).
///
/// L = 0.2126 * R + 0.7152 * G + 0.0722 * B
///
/// # Returns
/// Scalar luminance in the same range as the input components.
[[nodiscard]] inline float Luminance(float r, float g, float b) noexcept {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// ─────────────────────────────────────────────────────────────────────────────
// ACES Tone Mapping
// ─────────────────────────────────────────────────────────────────────────────

/// ACES filmic tone mapping for a single channel.
///
/// Maps HDR value x ∈ [0, ∞) to LDR ∈ [0, 1) using the rational approximation:
/// ```
///   numerator   = x * (2.51 * x + 0.03)
///   denominator = x * (2.43 * x + 0.59) + 0.14
///   output      = clamp(numerator / denominator, 0, 1)
/// ```
///
/// # Returns
/// Tone-mapped value clamped to [0, 1].
[[nodiscard]] inline float ACESTonemapChannel(float x) noexcept {
    if (x <= 0.0f) return 0.0f;  // Negative HDR values are physically meaningless
    const float num   = x * (2.51f * x + 0.03f);
    const float denom = x * (2.43f * x + 0.59f) + 0.14f;
    float result = num / (denom + 1e-6f);
    return std::clamp(result, 0.0f, 1.0f);
}

/// ACES filmic tone mapping for RGB.
///
/// Applies `ACESTonemapChannel` independently to R, G, B.
/// Pre-multiplies by `exposure` (linear multiplier) first.
///
/// # Parameters
/// * `r, g, b` — Linear HDR color components.
/// * `exposure` — Linear exposure multiplier (use `exp2(exposureAdjust)`).
/// * `intensity` — Post-tonemap intensity multiplier.
///
/// # Returns
/// Tone-mapped RGB clamped to [0, 1] per channel via output parameters.
inline void ACESTonemapRGB(float r, float g, float b,
                            float exposure, float intensity,
                            float& outR, float& outG, float& outB) noexcept {
    r *= exposure;
    g *= exposure;
    b *= exposure;
    outR = std::clamp(ACESTonemapChannel(r) * intensity, 0.0f, 1.0f);
    outG = std::clamp(ACESTonemapChannel(g) * intensity, 0.0f, 1.0f);
    outB = std::clamp(ACESTonemapChannel(b) * intensity, 0.0f, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Bloom Threshold
// ─────────────────────────────────────────────────────────────────────────────

/// Applies bloom luminance threshold to a pixel.
///
/// If the pixel's luminance is below `threshold`, returns 0 for all channels.
/// Otherwise returns the original color unchanged.
///
/// # Returns
/// true if the pixel passes the threshold (is bright enough for bloom).
[[nodiscard]] inline bool BloomThresholdTest(float r, float g, float b,
                                              float threshold) noexcept {
    return Luminance(r, g, b) >= threshold;
}

// ─────────────────────────────────────────────────────────────────────────────
// Gaussian Kernel
// ─────────────────────────────────────────────────────────────────────────────

/// 5-tap Gaussian kernel weights (sigma ≈ 1.0, normalized to sum to 1.0).
///
/// Offsets: [-2, -1, 0, +1, +2]
constexpr float kGaussianWeights[5] = {
    0.0545f,   // offset -2
    0.2442f,   // offset -1
    0.4027f,   // offset  0 (center)
    0.2442f,   // offset +1
    0.0545f    // offset +2
};

/// Verifies that the Gaussian kernel weights sum to approximately 1.0.
///
/// # Returns
/// Sum of all 5 weights. Should be within 1e-4 of 1.0.
[[nodiscard]] inline float GaussianKernelSum() noexcept {
    float sum = 0.0f;
    for (int i = 0; i < 5; ++i) {
        sum += kGaussianWeights[i];
    }
    return sum;
}

// ─────────────────────────────────────────────────────────────────────────────
// FXAA Edge Detection
// ─────────────────────────────────────────────────────────────────────────────

/// Determines if a pixel is on an edge by comparing luminance contrast
/// of its 4-neighborhood against a threshold.
///
/// # Parameters
/// * `centerLum` — Luminance of the center pixel.
/// * `upLum, downLum, leftLum, rightLum` — Luminance of 4 neighbors.
/// * `threshold` — Minimum contrast to be classified as an edge.
///
/// # Returns
/// true if contrast >= threshold (pixel is on an edge).
[[nodiscard]] inline bool FXAAEdgeDetect(float centerLum,
                                          float upLum, float downLum,
                                          float leftLum, float rightLum,
                                          float threshold) noexcept {
    const float maxLum = std::max({centerLum, upLum, downLum, leftLum, rightLum});
    const float minLum = std::min({centerLum, upLum, downLum, leftLum, rightLum});
    return (maxLum - minLum) >= threshold;
}

/// Determines if an edge is more horizontal or vertical.
///
/// Compares vertical contrast (up+down vs center) against horizontal
/// contrast (left+right vs center).
///
/// # Returns
/// true if the edge is vertical (should blur horizontally).
[[nodiscard]] inline bool FXAAIsVerticalEdge(float centerLum,
                                              float upLum, float downLum,
                                              float leftLum, float rightLum) noexcept {
    const float vertContrast = std::abs(upLum - centerLum) + std::abs(downLum - centerLum);
    const float horzContrast = std::abs(leftLum - centerLum) + std::abs(rightLum - centerLum);
    return vertContrast > horzContrast;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bloom Mip Chain Dimensions
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the dimensions of a bloom mip level given the source dimensions.
///
/// Each mip is half the resolution of the previous, rounded down, with a
/// minimum of 1 pixel per dimension.
///
/// # Parameters
/// * `sourceWidth, sourceHeight` — Dimensions of mip level 0 (source).
/// * `mipLevel` — Target mip level (0 = source, 1 = half, 2 = quarter, ...).
///
/// # Returns
/// Dimensions of the requested mip level via output parameters.
inline void BloomMipDimensions(uint32_t sourceWidth, uint32_t sourceHeight,
                                uint32_t mipLevel,
                                uint32_t& outWidth, uint32_t& outHeight) noexcept {
    outWidth  = std::max(1u, sourceWidth  >> mipLevel);
    outHeight = std::max(1u, sourceHeight >> mipLevel);
}

} // namespace uz::postprocess
