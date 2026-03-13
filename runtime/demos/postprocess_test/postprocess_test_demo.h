/// Post-Process Test Demo — HDR scene with bloom, tone mapping, and FXAA.
///
/// Renders 3 bright emissive cubes using a simple forward pass into an HDR
/// render target (RGBA16F), then applies the post-processing chain:
///   1. Tone mapping (ACES filmic, HDR → LDR)
///   2. Bloom (downsample → blur H → blur V → composite)
///   3. FXAA (edge-detect + directional blur)
///
/// Each effect can be toggled at runtime:
///   T — toggle tone mapping
///   B — toggle bloom
///   A — toggle FXAA
///
/// Uses raw RHI render graph passes (same pattern as clear_screen demo).

#pragma once

#include "rhi_backend.h"

#include <cstdint>
#include <string>

namespace uz {

/// Configuration for the post-process test demo.
struct PostProcessTestConfig {
    uint32_t frameCount = 0;               ///< Number of frames to render (0 = infinite).
    std::string screenshotPath;            ///< If non-empty, save screenshot to this path.
    uint32_t screenshotFrame = 5;          ///< Frame number to capture (default: 5).
    RHIBackend backend = RHIBackend::DX12; ///< RHI backend to use.
    bool enableTonemap = true;             ///< Initial state of tone mapping (toggle with T).
    bool enableFXAA    = true;             ///< Initial state of FXAA (toggle with A).
};

/// Runs the post-process test demo.
///
/// Creates a window, initializes RHI, renders bright HDR cubes, and applies
/// the full post-processing chain (tonemap + bloom + FXAA). Returns true on
/// success, false on RHI failure.
bool RunPostProcessTest(const PostProcessTestConfig& config = {});

} // namespace uz
