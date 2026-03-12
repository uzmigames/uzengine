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

namespace uz {

/// Runs the backend-agnostic clear screen render loop.
///
/// Creates a 1280x720 window, initializes the selected RHI backend with
/// validation enabled, and renders an animated color-cycling clear screen
/// using triple-buffered frame pacing with per-frame fences.
///
/// # Arguments
/// * `backend` - Which RHI backend to use (DX12, DX11, etc.).
/// * `frameCount` - Number of frames to render. Pass `0` to run until
///   the user closes the window.
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
/// bool ok = uz::RunClearScreen(uz::RHIBackend::DX12, 300);
/// uz::LogShutdown();
/// ```
bool RunClearScreen(RHIBackend backend = RHIBackend::DX12, uint32_t frameCount = 0);

} // namespace uz
