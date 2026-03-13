/// PBR Test Demo — Backend-Agnostic
///
/// Renders 5 cubes in a row using the standard mesh_vs/mesh_ps shaders with
/// Cook-Torrance PBR lighting (directional sun + ambient). Tests PerFrameCB
/// lighting fields (lightDir, lightColor, ambientColor) and multi-object
/// rendering with PerObjectCB transforms.
///
/// Supports `--screenshot <path>` and `--screenshot-frame <N>` for automated
/// visual testing. Backend is selected at runtime via `--rhi` flag.

#pragma once

#include "rhi_backend.h"

#include <cstdint>
#include <string>

namespace uz {

/// Configuration for running the PBR test demo.
struct PBRTestConfig {
    uint32_t frameCount = 0;               ///< Number of frames to render (0 = infinite)
    std::string screenshotPath;            ///< If non-empty, save screenshot to this path
    uint32_t screenshotFrame = 5;          ///< Frame number to capture (default: 5)
    RHIBackend backend = RHIBackend::DX12; ///< RHI backend to use
};

/// Runs the backend-agnostic PBR test demo.
///
/// Creates a window, initializes the selected RHI backend, loads a .uzmesh
/// cube asset from `assets/cube.uzmesh` relative to the executable, and
/// renders 5 cubes in a row using mesh_vs/mesh_ps shaders with Cook-Torrance
/// PBR lighting. Lighting data (sun direction, sun color, ambient color) is
/// uploaded via PerFrameCB. Each cube uses an independent PerObjectCB with
/// its own model and normal matrix. Includes an editor grid overlay and FPS
/// stats overlay.
///
/// # Arguments
/// * `config` — Demo configuration (frame count, screenshot settings, backend).
///
/// # Returns
/// `true` if all frames rendered without errors.
/// `false` if device, swapchain, shader compilation, or mesh load failed.
///
/// # Side Effects
/// - Opens a visible 1280x720 window titled "UzEngine — PBR Test (Cook-Torrance)"
/// - Logs RHI and asset errors on failure
/// - Logs final FPS statistics on completion
///
/// # Example
/// ```cpp
/// uz::LogInit();
/// uz::PBRTestConfig cfg;
/// cfg.backend = uz::RHIBackend::DX12;
/// bool ok = uz::RunPBRTestDemo(cfg);
/// uz::LogShutdown();
/// ```
bool RunPBRTestDemo(const PBRTestConfig& config = {});

} // namespace uz
