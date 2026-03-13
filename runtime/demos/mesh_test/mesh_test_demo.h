/// Mesh Test Demo — Backend-Agnostic
///
/// Renders a .uzmesh cube (Vertex format with normals/tangents) using the
/// standard mesh_vs/mesh_ps shaders with per-object transforms and directional
/// lighting. Tests MeshBuilder + Vertex format + PerObjectCB integration.
///
/// Supports `--screenshot <path>` and `--screenshot-frame <N>` for automated
/// visual testing. Backend is selected at runtime via `--rhi` flag.

#pragma once

#include "rhi_backend.h"

#include <cstdint>
#include <string>

namespace uz {

/// Configuration for running the mesh test demo.
struct MeshTestConfig {
    uint32_t frameCount = 0;            ///< Number of frames to render (0 = infinite)
    std::string screenshotPath;         ///< If non-empty, save screenshot to this path
    uint32_t screenshotFrame = 5;       ///< Frame number to capture (default: 5)
    RHIBackend backend = RHIBackend::DX12; ///< RHI backend to use
};

/// Runs the backend-agnostic mesh test demo.
///
/// Creates a window, initializes the selected RHI backend, loads a .uzmesh
/// cube asset from `assets/cube.uzmesh` relative to the executable, and
/// renders it using mesh_vs/mesh_ps shaders with per-frame and per-object
/// constant buffers. Includes an editor grid overlay and FPS stats overlay.
///
/// # Arguments
/// * `config` — Demo configuration (frame count, screenshot settings, backend).
///
/// # Returns
/// `true` if all frames rendered without errors.
/// `false` if device, swapchain, shader compilation, or mesh load failed.
///
/// # Side Effects
/// - Opens a visible 1280x720 window titled "UzEngine — Mesh Test (Lit Cube)"
/// - Logs RHI and asset errors on failure
/// - Logs final FPS statistics on completion
///
/// # Example
/// ```cpp
/// uz::LogInit();
/// uz::MeshTestConfig cfg;
/// cfg.backend = uz::RHIBackend::DX12;
/// bool ok = uz::RunMeshTestDemo(cfg);
/// uz::LogShutdown();
/// ```
bool RunMeshTestDemo(const MeshTestConfig& config = {});

} // namespace uz
