/// UzEngine Triangle Demo — Backend-Agnostic
///
/// Renders a colored RGB triangle using abstract RHI interfaces only.
/// Supports both SM6.6 bindless (vertex fetch from StructuredBuffer via SRV index)
/// and SM5 Input Assembler fallback (vertex buffer bound via input layout).
/// The path is selected automatically based on `device->IsBindless()`.
///
/// # What This Validates
/// - Backend-agnostic device/swapchain/command list lifecycle
/// - Shader pipeline integration (VFS mount -> cache -> DXC/FXC compilation)
/// - Graphics PSO creation with optional input layout
/// - Vertex buffer creation as StructuredBuffer (SM6.6) or VertexBuffer (SM5)
/// - Complete draw call pipeline: Begin -> barriers -> clear -> render pass
///   -> set pipeline -> bind vertex data -> draw -> end render pass -> barriers
///   -> End -> Execute -> Present -> fence signal

#pragma once

#include "rhi_backend.h"

#include <cstdint>
#include <string>

namespace uz {

/// Configuration for running the triangle demo.
struct TriangleDemoConfig {
    uint32_t frameCount = 0;            ///< Number of frames to render (0 = infinite)
    std::string screenshotPath;         ///< If non-empty, save screenshot to this path
    uint32_t screenshotFrame = 5;       ///< Frame number to capture (default: 5)
    RHIBackend backend = RHIBackend::DX12; ///< RHI backend to use
};

/// Runs the backend-agnostic triangle render loop.
///
/// Creates a 1280x720 window, initializes the selected RHI backend,
/// loads shaders via ShaderPipeline (VFS -> cache -> DXC or FXC),
/// creates a graphics pipeline and vertex buffer, then renders a colored
/// RGB triangle with triple-buffered frame pacing.
///
/// Automatically selects between SM6.6 bindless and SM5 Input Assembler paths
/// based on `device->IsBindless()`. The `UZ_FORCE_SM5=1` environment variable
/// forces the SM5 path for testing FXC fallback even on capable hardware.
///
/// # Arguments
/// * `config` — Demo configuration (frame count, screenshot settings, backend).
///
/// # Returns
/// `true` if all frames rendered without errors.
/// `false` if any initialization step failed (device, shaders, pipeline, buffers).
///
/// # Side Effects
/// - Opens a visible window titled "UzEngine — Triangle Demo"
/// - Mounts `{exe_dir}/../../shaders/` into VFS at `/demo/triangle`
/// - Caches compiled shaders in `{exe_dir}/cache/shaders/`
/// - Logs pipeline results, SRV index (SM6.6 path), and final FPS statistics
///
/// # Example
/// ```cpp
/// uz::LogInit();
/// uz::TriangleDemoConfig cfg;
/// cfg.backend = uz::RHIBackend::DX12;
/// bool ok = uz::RunTriangleDemo(cfg);
/// uz::LogShutdown();
/// ```
bool RunTriangleDemo(const TriangleDemoConfig& config = {});

} // namespace uz
