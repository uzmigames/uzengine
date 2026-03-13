/// Deferred Rendering Test Demo — Backend-Agnostic
///
/// Renders 5 cubes in a row through a multi-pass deferred shading pipeline:
/// G-Buffer pass writes albedo, world-space normals, and metallic/roughness
/// to three render targets plus a depth buffer. A fullscreen lighting pass
/// then reconstructs world positions from depth and accumulates 4 point lights
/// plus a directional sun via Cook-Torrance BRDF.
///
/// Debug visualization: press keys 1-7 to inspect individual G-Buffer channels
/// in-place. Press 0 to return to the composed deferred result. The current
/// mode is shown in the stats overlay.
///
/// Supports `--screenshot <path>` and `--screenshot-frame <N>` for automated
/// visual testing. Backend is selected at runtime via `--rhi` flag.

#pragma once

#include "rhi_backend.h"

#include <cstdint>
#include <string>

namespace uz {

/// Configuration for running the deferred rendering test demo.
struct DeferredTestConfig {
    uint32_t    frameCount      = 0;               ///< Number of frames to render (0 = run until window close).
    std::string screenshotPath;                    ///< If non-empty, save screenshot to this path at `screenshotFrame`.
    uint32_t    screenshotFrame = 60;              ///< Frame number at which to capture the screenshot (default: 60).
    RHIBackend  backend         = RHIBackend::DX12;///< RHI backend to use (default: DX12).
};

/// Runs the deferred rendering test demo.
///
/// Creates a 1280x720 window, initialises the selected RHI backend, uploads a
/// cube mesh and five PBR texture sets, and renders a multi-pass deferred frame:
///
/// 1. **G-Buffer pass** — draws 5 cubes and fills albedo (RGBA8), normal
///    (RGBA16F), metallic/roughness (RGBA8), and depth (D32F) render targets.
/// 2. **Lighting pass** — fullscreen triangle that reads the G-Buffer, reconstructs
///    world positions via `invViewProj`, and accumulates 4 point lights + 1
///    directional light using Cook-Torrance BRDF.
/// 3. **(Optional) Debug pass** — if the user pressed 1-7, a GBuffer debug pass
///    replaces the lighting pass and visualises a single G-Buffer channel.
///
/// The editor grid and FPS stats overlay are composited into the final backbuffer
/// after lighting.
///
/// # Arguments
/// * `config` — Demo configuration (frame count, screenshot settings, RHI backend).
///
/// # Returns
/// `true` if all frames rendered without errors.
/// `false` if device creation, swapchain, shader compilation, or mesh load failed.
///
/// # Side Effects
/// - Opens a visible 1280x720 window titled "UzEngine — Deferred Rendering Test".
/// - Logs errors on failure via `UZ_LOG(Renderer, Error, ...)`.
/// - Logs final FPS statistics on completion.
///
/// # Example
/// ```cpp
/// uz::LogInit();
/// uz::DeferredTestConfig cfg;
/// cfg.backend = uz::RHIBackend::DX12;
/// bool ok = uz::RunDeferredTestDemo(cfg);
/// uz::LogShutdown();
/// ```
bool RunDeferredTestDemo(const DeferredTestConfig& config = {});

} // namespace uz
