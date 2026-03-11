/// Camera Test Demo
///
/// Renders a colored cube with FPS camera controls (WASD + mouse look).
/// Tests Camera + FPSCameraController + PerFrameCB integration.
///
/// Supports `--screenshot <path>` and `--screenshot-frame <N>` for automated
/// visual testing. When a screenshot is requested, the demo captures the back
/// buffer on frame N, saves it as PNG, and exits.

#pragma once

#include <cstdint>
#include <string>

namespace uz {

/// Configuration for running the camera test demo.
struct CameraTestConfig {
    uint32_t frameCount = 0;            ///< Number of frames (0 = infinite)
    std::string screenshotPath;         ///< If non-empty, save screenshot to this path
    uint32_t screenshotFrame = 5;       ///< Frame number to capture (default: 5, allows GPU warmup)
};

/// Runs the camera test demo.
///
/// # Arguments
/// * `config` — Demo configuration (frame count, screenshot settings).
///
/// # Returns
/// `true` if the demo ran successfully.
bool RunCameraTestDemo(const CameraTestConfig& config = {});

} // namespace uz
