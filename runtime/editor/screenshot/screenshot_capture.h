/// UzEngine Screenshot Capture — RHI-Agnostic
///
/// Captures the current back buffer to a PNG file for automated visual testing.
/// Works with any RHI backend (DX12, DX11, Vulkan). Uses a readback buffer to
/// copy the render target to CPU memory, then writes it via stb_image_write.
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
/// Works with any RHI backend — uses only `IRHIDevice`, `IRHICommandList`,
/// and `IRHISwapChain` interfaces.
///
/// # Thread Safety
/// NOT thread-safe. Call from the render thread only.
class ScreenshotCapture {
public:
    ScreenshotCapture() = default;
    ~ScreenshotCapture() = default;

    /// Initializes the readback buffer for the given dimensions.
    ///
    /// Creates a readback buffer with 256-byte aligned row pitch. The alignment
    /// satisfies all backends (DX12 requires 256, others are fine with it).
    ///
    /// # Arguments
    /// * `device` — RHI device for buffer creation (any backend).
    /// * `width` — Back buffer width in pixels.
    /// * `height` — Back buffer height in pixels.
    ///
    /// # Returns
    /// true on success, false if buffer creation fails.
    bool Init(IRHIDevice* device, uint32_t width, uint32_t height);

    /// Records GPU commands to copy the back buffer into the readback buffer.
    ///
    /// Must be called after rendering is complete but BEFORE the Present barrier.
    /// The back buffer must be in RENDER_TARGET state when called.
    /// After this call, the back buffer is transitioned back to RENDER_TARGET.
    ///
    /// # Arguments
    /// * `cmdList` — Command list to record copy commands on (any backend).
    /// * `swapChain` — Swap chain to read the current back buffer from.
    void CaptureBackBuffer(IRHICommandList* cmdList, IRHISwapChain* swapChain);

    /// Records GPU commands to copy the back buffer from Present state.
    ///
    /// Use this when capturing AFTER the render graph has transitioned the
    /// backbuffer to Present state. Transitions: Present → CopySrc → Present.
    ///
    /// # Arguments
    /// * `cmdList` — Command list to record copy commands on.
    /// * `swapChain` — Swap chain to read the current back buffer from.
    void CaptureFromPresent(IRHICommandList* cmdList, IRHISwapChain* swapChain);

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

    /// Returns the captured pixels as a base64-encoded PNG string.
    ///
    /// Useful for LLM workflows where writing to disk and reading back is
    /// slower than piping the image directly through stdout. The returned
    /// string can be embedded in a `data:image/png;base64,...` URI or
    /// passed directly to multimodal AI models.
    ///
    /// Must be called AFTER the GPU has finished executing the copy commands
    /// (i.e., after a fence wait).
    ///
    /// # Returns
    /// Base64-encoded PNG string on success, empty string on failure.
    [[nodiscard]] std::string ToBase64PNG() const;

    /// Maps the readback buffer for CPU read access.
    ///
    /// Returns a pointer to the first row of RGBA pixel data and the row
    /// stride (pitch) in bytes. The buffer remains mapped until UnmapReadback().
    ///
    /// Must be called AFTER the GPU has finished the copy commands (fence wait).
    ///
    /// # Returns
    /// true on success, false if the buffer is not initialized or mapping fails.
    bool MapReadback(const uint8_t** outData, uint32_t* outStride) const;

    /// Unmaps the readback buffer after MapReadback().
    void UnmapReadback() const;

    /// Releases the readback buffer.
    void Shutdown();

    /// Returns true if Init() has been called successfully.
    [[nodiscard]] bool IsInitialized() const { return m_device != nullptr; }

private:
    IRHIDevice* m_device = nullptr;
    RHIBufferHandle m_readbackBuffer;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_rowPitch = 0;      ///< Aligned row pitch (256-byte alignment for DX12 compat)
    RHIFormat m_captureFormat = RHIFormat::RGBA8Unorm; ///< Actual swapchain format (BGRA needs swizzle)
};

} // namespace uz
