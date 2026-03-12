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

namespace uz {

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
/// * `backend`    - Which RHI backend to use (DX12, DX11, etc.).
/// * `frameCount` - Number of frames to render. Pass `0` to run until
///   the user closes the window.
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
/// bool ok = uz::RunTriangleDemo(uz::RHIBackend::DX12, 0);
/// uz::LogShutdown();
/// ```
bool RunTriangleDemo(RHIBackend backend = RHIBackend::DX12, uint32_t frameCount = 0);

} // namespace uz
