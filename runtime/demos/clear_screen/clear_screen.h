/// UzEngine Clear Screen Demo — Backend-Agnostic
///
/// Creates a window, initializes an RHI device (DX12 or DX11), and renders
/// an animated sine-wave RGB clear screen using triple-buffered frame pacing.
/// Used to validate the full RHI initialization pipeline:
/// device creation -> swapchain -> command list recording -> fence synchronization -> present.
///
/// # Performance
/// Achieves 6000+ FPS on modern GPUs (RTX 4090) — validates that the
/// RHI pipeline has near-zero overhead for trivial workloads.

#pragma once

#include "rhi_backend.h"

#include <cstdint>
#include <string>

namespace uz {

/// Configuration for running the clear screen demo.
struct ClearScreenConfig {
    uint32_t frameCount = 0;            ///< Number of frames to render (0 = infinite)
    std::string screenshotPath;         ///< If non-empty, save screenshot to this path
    uint32_t screenshotFrame = 5;       ///< Frame number to capture (default: 5)
    RHIBackend backend = RHIBackend::DX12; ///< RHI backend to use
};

/// Runs the backend-agnostic clear screen render loop.
///
/// Creates a 1280x720 window, initializes the selected RHI backend with
/// validation enabled, and renders an animated color-cycling clear screen
/// using triple-buffered frame pacing with per-frame fences.
///
/// # Arguments
/// * `config` — Demo configuration (frame count, screenshot settings, backend).
///
/// # Returns
/// `true` if all frames rendered without validation errors.
/// `false` if device/swapchain/command list creation failed.
///
/// # Side Effects
/// - Opens a visible window titled "UzEngine -- Clear Screen"
/// - Logs RHI errors on failure
/// - Logs final FPS statistics on completion
///
/// # Example
/// ```cpp
/// uz::LogInit();
/// uz::ClearScreenConfig cfg;
/// cfg.backend = uz::RHIBackend::DX12;
/// bool ok = uz::RunClearScreen(cfg);
/// uz::LogShutdown();
/// ```
bool RunClearScreen(const ClearScreenConfig& config = {});

} // namespace uz
