/// UzEngine Screenshot Capture
///
/// Captures the current back buffer to a PNG file for automated visual testing.
/// Uses a DX12 readback buffer to copy the render target to CPU memory,
/// then writes it via stb_image_write.
///
/// # Usage
/// ```cpp
/// ScreenshotCapture capture;
/// capture.Init(device, width, height);
///
/// // After rendering, before Present:
/// capture.CaptureBackBuffer(cmdList, swapChain);
///
/// // After GPU finishes (fence wait):
/// capture.SaveToPNG("screenshot.png");
///
/// capture.Shutdown();
/// ```
///
/// # Integration with Demos
/// Demos accept `--screenshot <path>` and `--screenshot-frame <N>` flags.
/// On frame N, the back buffer is captured and saved, then the demo exits.

#pragma once

#include "rhi.h"
#include "dx12_device.h"
#include "dx12_command_list.h"
#include "dx12_swapchain.h"
#include "log.h"

#include <string>

namespace uz {

UZ_DECLARE_LOG_CATEGORY(Screenshot);

/// Captures the GPU back buffer to a CPU-readable PNG file.
///
/// Allocates a persistent readback buffer sized to the back buffer dimensions.
/// The capture is a two-phase operation:
/// 1. `CaptureBackBuffer()` — records GPU copy commands (call before Present)
/// 2. `SaveToPNG()` — reads mapped CPU memory and writes PNG (call after fence wait)
///
/// # Thread Safety
/// NOT thread-safe. Call from the render thread only.
class ScreenshotCapture {
public:
    ScreenshotCapture() = default;
    ~ScreenshotCapture() = default;

    /// Initializes the readback buffer for the given dimensions.
    ///
    /// # Arguments
    /// * `device` — DX12 device for buffer creation.
    /// * `width` — Back buffer width in pixels.
    /// * `height` — Back buffer height in pixels.
    ///
    /// # Returns
    /// true on success, false if buffer creation fails.
    bool Init(DX12Device* device, uint32_t width, uint32_t height);

    /// Records GPU commands to copy the back buffer into the readback buffer.
    ///
    /// Must be called after rendering is complete but BEFORE the Present barrier.
    /// The back buffer must be in RENDER_TARGET state when called.
    /// After this call, the back buffer is transitioned back to RENDER_TARGET.
    ///
    /// # Arguments
    /// * `cmdList` — Command list to record copy commands on.
    /// * `swapChain` — Swap chain to read the current back buffer from.
    void CaptureBackBuffer(DX12CommandList* cmdList, DX12SwapChain* swapChain);

    /// Saves the captured pixels to a PNG file.
    ///
    /// Must be called AFTER the GPU has finished executing the copy commands
    /// (i.e., after a fence wait). The readback buffer must have been populated
    /// by a prior `CaptureBackBuffer()` call.
    ///
    /// # Arguments
    /// * `path` — Output file path for the PNG.
    ///
    /// # Returns
    /// true if the PNG was written successfully, false on I/O error.
    bool SaveToPNG(const std::string& path) const;

    /// Releases the readback buffer.
    void Shutdown();

    /// Returns true if Init() has been called successfully.
    [[nodiscard]] bool IsInitialized() const { return m_device != nullptr; }

private:
    DX12Device* m_device = nullptr;
    RHIBufferHandle m_readbackBuffer;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_rowPitch = 0;  ///< Aligned row pitch (D3D12 requires 256-byte alignment)
};

} // namespace uz
