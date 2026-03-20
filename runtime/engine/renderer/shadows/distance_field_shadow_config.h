/// UzEngine Distance Field Shadow Configuration
///
/// Quality presets and runtime parameters for the DFS system.
/// These control ray march step count, max shadow distance, tile culling size,
/// and self-shadow prevention bias.
///
/// # Reference
/// - DistanceFieldShadowing.cpp — ray march loop, tile culling
/// - DirectionalLightComponent.cpp:1018 — max DFS distance (51200 cm = 512m)
/// - DistanceFieldLightingShared.h — object culling tile size

#pragma once

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// DfsQuality — ray march quality presets
// ─────────────────────────────────────────────────────────────────────────────

/// Quality levels for distance field shadow ray marching.
///
/// Higher quality increases the number of ray march steps in the cone tracing
/// compute shader, improving shadow accuracy at the cost of GPU time.
/// Approximate timings are for a 1080p half-res trace on an RTX 3070.
///
/// # Reference
/// DistanceFieldShadowing.cpp — step count per quality level.
enum class DfsQuality : uint8_t {
    Off    = 0,   ///< Disabled (0 steps, 0 cost).
    Low    = 1,   ///< 20 steps (~0.7ms at 1080p half-res).
    Medium = 2,   ///< 32 steps (~1.2ms).
    High   = 3    ///< 64 steps (~2.5ms). Default for high-end GPUs.
};

/// Returns the number of ray march steps for a given quality level.
///
/// # Parameters
/// - q: DFS quality preset.
///
/// # Returns
/// Step count: 0 (Off), 20 (Low), 32 (Medium), or 64 (High).
[[nodiscard]] constexpr uint32_t DfsStepCount(DfsQuality q) noexcept {
    switch (q) {
        case DfsQuality::Off:    return 0;
        case DfsQuality::Low:    return 20;
        case DfsQuality::Medium: return 32;
        case DfsQuality::High:   return 64;
    }
    return 0; // Unreachable — silences compiler warning for non-enum values.
}

// ─────────────────────────────────────────────────────────────────────────────
// DistanceFieldShadowConfig — runtime DFS parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the Distance Field Shadow system.
///
/// Controls whether DFS is active, the maximum shadow distance, ray march quality,
/// resolution mode, object culling tile size, and self-shadow bias. All distance
/// values are in centimeters (standard: 1 unit = 1 cm).
///
/// Default values match reference 5.6 defaults:
/// - distance: 51200 cm (512m) — DirectionalLightComponent.cpp:1018
/// - tileWorldSize: 200 cm — DistanceFieldShadowing.cpp tile culling grid
/// - selfShadowBias: 2 cm — prevents surface acne from SDF quantization noise
///
/// # Usage
/// ```cpp
/// DistanceFieldShadowConfig cfg;
/// cfg.enabled = true;
/// cfg.quality = DfsQuality::High;
/// shadowRenderer.SetDfsConfig(cfg);
/// ```
struct DistanceFieldShadowConfig {
    bool       enabled        = false;             ///< Enable DFS globally. Requires mesh SDFs to be baked.
    float      distance       = 51200.0f;          ///< Max DFS distance in cm (512m). Source: DLC.cpp:1018.
    DfsQuality quality        = DfsQuality::High;  ///< Ray march quality level (step count).
    bool       fullResolution = false;             ///< True = full-res trace. False = half-res + bilateral upsample.
    float      tileWorldSize  = 200.0f;            ///< World-space tile size for object culling in cm.
    float      selfShadowBias = 2.0f;             ///< Self-shadow prevention bias in cm.
};

} // namespace uz
