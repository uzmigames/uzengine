#pragma once

/// Headless frame capture — off-screen RTV + CPU readback service
/// (phase 10.9.0).
///
/// Allocates an off-screen RGBA8 render target of arbitrary size, lets
/// a caller-supplied render callback draw into it, triggers a GPU→CPU
/// readback, and returns raw RGBA pixel data (or PNG-encoded bytes).
/// Completely decoupled from the swapchain so the thumbnail pipeline
/// (9.7), snapshot viewer (9.8), and CI bots can all produce PNGs
/// without a visible window.
///
/// # Design
///
/// Different from the phase 3.xx `ScreenshotCapture` which copies from
/// the swapchain backbuffer: `HeadlessFrameCapture` owns its own RTV
/// and transition dance, so callers can size it to whatever they need
/// (128×128 thumbnails, 256×256 atlas cells, full-res offscreen).
///
/// # Usage
///
/// ```cpp
/// HeadlessFrameCapture cap;
/// cap.Init(device, 128, 128);
///
/// // Each capture is a four-step dance on the command list:
/// cap.BeginFrame(cmdList);                  // transition → RenderTarget
/// renderer.RenderSceneIntoRTV(cap.GetRTV(), cmdList);
/// cap.EndFrame(cmdList);                    // copy → readback
/// device->WaitIdle();                       // caller owns fence sync
///
/// std::vector<uint8_t> rgba = cap.ReadPixels();     // or cap.ReadPNG()
/// cap.Shutdown();
/// ```
///
/// # Thread safety
/// Not thread-safe. Drive from the render thread only. Re-entrant
/// `BeginFrame` calls without a matching `EndFrame` are rejected with
/// a log warning.
///
/// @ref editor/screenshot/screenshot_capture.h (the backbuffer sibling)
/// @ref docs/engine/renderer/headless-frame-capture.md

#include "rhi.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

class HeadlessFrameCapture {
public:
    HeadlessFrameCapture() = default;
    ~HeadlessFrameCapture() = default;

    HeadlessFrameCapture(const HeadlessFrameCapture&)            = delete;
    HeadlessFrameCapture& operator=(const HeadlessFrameCapture&) = delete;

    /// Allocates the off-screen RTV + readback buffer.
    ///
    /// # Arguments
    /// - `device` — RHI device; must outlive the capturer.
    /// - `width`, `height` — output dimensions in pixels. Must be > 0.
    /// - `format` — output format (default RGBA8Unorm). Caller ensures the
    ///              format is renderable + copyable on the backend.
    ///
    /// # Returns
    /// `true` on success. Failed allocations log + leave the capturer
    /// uninitialised.
    bool Init(IRHIDevice* device, uint32_t width, uint32_t height,
              RHIFormat format = RHIFormat::RGBA8Unorm);

    /// Releases the off-screen RTV and readback buffer.
    void Shutdown();

    /// Transitions the RTV to `RenderTarget` state so the caller's
    /// render callback can draw into it. Must be followed by an
    /// `EndFrame(cmdList)` on the same command list.
    ///
    /// Inserts a single barrier `CopySrc → RenderTarget` (or
    /// `Undefined → RenderTarget` on the first capture). No-op when the
    /// capturer is not initialised.
    void BeginFrame(IRHICommandList* cmdList);

    /// Transitions the RTV to `CopySrc`, copies into the readback
    /// buffer, and leaves the RTV in `CopySrc` state ready for the next
    /// `BeginFrame`. Call after the render callback has finished.
    void EndFrame(IRHICommandList* cmdList);

    /// Reads the GPU-populated readback into a flat RGBA8 vector.
    /// Size = `width * height * 4` bytes. The caller MUST have waited
    /// for the GPU to finish the previous `EndFrame` before calling
    /// this (typically `device->WaitIdle()`).
    ///
    /// Returns an empty vector when uninitialised or when the map fails.
    [[nodiscard]] std::vector<uint8_t> ReadPixels() const;

    /// Reads the captured frame and PNG-encodes it in memory.
    /// Returns an empty vector on failure.
    [[nodiscard]] std::vector<uint8_t> ReadPNG() const;

    /// Writes the captured frame to a PNG file on disk.
    /// Returns true on success.
    bool SaveToPNG(const std::string& path) const;

    // ── Accessors ────────────────────────────────────────────────────────

    [[nodiscard]] bool IsInitialized() const noexcept {
        return m_device != nullptr;
    }

    /// RTV handle for the caller's render callback to bind.
    [[nodiscard]] RHITextureHandle GetRTV() const noexcept { return m_rtv; }

    [[nodiscard]] uint32_t Width()  const noexcept { return m_width; }
    [[nodiscard]] uint32_t Height() const noexcept { return m_height; }
    [[nodiscard]] RHIFormat Format() const noexcept { return m_format; }

    /// Returns the aligned row pitch used by the readback buffer. Useful
    /// for callers that want to map the readback directly instead of
    /// going through `ReadPixels`.
    [[nodiscard]] uint32_t RowPitch() const noexcept { return m_rowPitch; }

private:
    IRHIDevice*        m_device       = nullptr;
    RHITextureHandle   m_rtv;
    RHIBufferHandle    m_readback;
    uint32_t           m_width        = 0;
    uint32_t           m_height       = 0;
    uint32_t           m_rowPitch     = 0;    ///< 256-byte aligned for DX12.
    RHIFormat          m_format       = RHIFormat::RGBA8Unorm;
    bool               m_inFrame      = false; ///< True between BeginFrame/EndFrame.
    bool               m_firstCapture = true;  ///< Barrier source is Undefined first pass.
};

} // namespace uz
