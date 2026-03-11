#pragma once

#include <cstdint>

/// DX12 clear screen render loop for validation.
///
/// Creates a window, initializes the DX12 device, and renders an animated
/// color-cycling clear screen. Used to validate the full RHI initialization
/// pipeline: device → swapchain → command list → fence → present.

namespace uz {

/// Runs the clear screen render loop for the specified number of frames.
///
/// Returns true if all frames completed without DX12 validation errors.
/// Pass 0 for frameCount to run until window close.
bool RunClearScreen(uint32_t frameCount = 0);

} // namespace uz
