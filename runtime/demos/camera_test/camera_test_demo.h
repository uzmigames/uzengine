/// Camera Test Demo — Backend-Agnostic
///
/// Renders a colored cube in 3D with FPS camera controls (WASD + mouse look).
/// Tests Camera + FPSCameraController + PerFrameCB integration against any
/// RHI backend selected at runtime.
///
/// Supports `--screenshot <path>` and `--screenshot-frame <N>` for automated
/// visual testing. When a screenshot is requested, the demo captures the back
/// buffer on frame N, saves it as PNG, and exits.

#pragma once

#include "rhi_backend.h"

#include <cstdint>
#include <string>

namespace uz {

/// Configuration for running the camera test demo.
struct CameraTestConfig {
    RHIBackend backend = RHIBackend::DX12;  ///< RHI backend to initialize
    uint32_t frameCount = 0;                ///< Number of frames to render (0 = infinite)
    std::string screenshotPath;             ///< If non-empty, save screenshot to this path
    uint32_t screenshotFrame = 5;           ///< Frame number to capture (default: 5, allows GPU warmup)
};

/// Runs the camera test demo using the backend specified in config.
///
/// Creates a 1280x720 window, initializes the selected RHI backend with
/// validation enabled, uploads a unit cube vertex buffer, and renders a
/// 3D cube with per-frame FPS camera controls via WASD + right-mouse-button look.
///
/// # Arguments
/// * `config` — Demo configuration (backend, frame count, screenshot settings).
///
/// # Returns
/// `true` if all frames rendered without error.
/// `false` if device/swapchain/pipeline/shader creation failed.
///
/// # Side Effects
/// - Opens a visible window titled "UzEngine — Camera Test (WASD + RMB Look)"
/// - Logs RHI errors on failure
/// - Logs final FPS statistics on completion
///
/// # Example
/// ```cpp
/// uz::LogInit();
/// uz::CameraTestConfig cfg{};
/// cfg.backend = uz::RHIBackend::DX12;
/// bool ok = uz::RunCameraTestDemo(cfg);
/// uz::LogShutdown();
/// ```
bool RunCameraTestDemo(const CameraTestConfig& config = {});

} // namespace uz
