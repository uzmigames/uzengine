/// UzEngine Screenshot Capture Implementation — RHI-Agnostic
///
/// Two-phase GPU readback: CaptureBackBuffer() records copy commands via
/// the abstract RHI interface, SaveToPNG() reads the mapped readback buffer
/// and writes a PNG file. Works with any backend (DX12, DX11, Vulkan).

#include "screenshot_capture.h"

#include <cstring>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace uz {

UZ_DEFINE_LOG_CATEGORY(Screenshot, Info);

/// All backends accept 256-byte aligned row pitch. DX12 requires it;
/// DX11 and Vulkan are fine with any alignment but 256 avoids issues.
static constexpr uint32_t kTexturePitchAlignment = 256;

/// Aligns a value up to the given power-of-two alignment.
static uint32_t AlignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool ScreenshotCapture::Init(IRHIDevice* device, uint32_t width, uint32_t height) {
    m_device = device;
    m_width = width;
    m_height = height;

    // RGBA8 = 4 bytes per pixel, aligned to kTexturePitchAlignment
    m_rowPitch = AlignUp(width * 4, kTexturePitchAlignment);

    RHIBufferDesc desc{};
    desc.sizeBytes = m_rowPitch * height;
    desc.memory = RHIMemoryType::Readback;
    desc.usage = RHIBufferUsage::CopyDst;
    desc.debugName = "ScreenshotReadback";

    m_readbackBuffer = device->CreateBuffer(desc);
    if (!m_readbackBuffer.IsValid()) {
        UZ_LOG(Screenshot, Error, "Failed to create readback buffer ({}x{}, {} bytes)",
               width, height, desc.sizeBytes);
        m_device = nullptr;
        return false;
    }

    UZ_LOG(Screenshot, Info, "Screenshot capture initialized ({}x{}, rowPitch={})",
           width, height, m_rowPitch);
    return true;
}

void ScreenshotCapture::Shutdown() {
    if (m_device && m_readbackBuffer.IsValid()) {
        m_device->DestroyBuffer(m_readbackBuffer);
    }
    m_device = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Capture
// ─────────────────────────────────────────────────────────────────────────────

void ScreenshotCapture::CaptureBackBuffer(IRHICommandList* cmdList, IRHISwapChain* swapChain) {
    RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();

    // Transition: RenderTarget → CopySrc
    RHIBarrierDesc barrier{};
    barrier.texture = backBuffer;
    barrier.stateBefore = RHIResourceState::RenderTarget;
    barrier.stateAfter = RHIResourceState::CopySrc;
    cmdList->ResourceBarrier(&barrier, 1);

    // Copy texture → readback buffer
    cmdList->CopyTextureToBuffer(backBuffer, 0, m_readbackBuffer, 0, m_rowPitch);

    // Transition: CopySrc → RenderTarget
    barrier.stateBefore = RHIResourceState::CopySrc;
    barrier.stateAfter = RHIResourceState::RenderTarget;
    cmdList->ResourceBarrier(&barrier, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Save
// ─────────────────────────────────────────────────────────────────────────────

bool ScreenshotCapture::SaveToPNG(const std::string& path) const {
    if (!m_device || !m_readbackBuffer.IsValid()) {
        UZ_LOG(Screenshot, Error, "Cannot save — capture not initialized");
        return false;
    }

    void* mapped = m_device->MapBuffer(m_readbackBuffer);
    if (!mapped) {
        UZ_LOG(Screenshot, Error, "Cannot save — failed to map readback buffer");
        return false;
    }

    const uint32_t tightPitch = m_width * 4;
    const auto* srcBytes = static_cast<const uint8_t*>(mapped);

    bool ok = false;
    if (m_rowPitch == tightPitch) {
        ok = stbi_write_png(path.c_str(),
            static_cast<int>(m_width), static_cast<int>(m_height),
            4, srcBytes, static_cast<int>(tightPitch)) != 0;
    } else {
        // Remove row padding — stb needs tight rows
        std::vector<uint8_t> tight(tightPitch * m_height);
        for (uint32_t row = 0; row < m_height; ++row) {
            std::memcpy(tight.data() + row * tightPitch,
                        srcBytes + row * m_rowPitch,
                        tightPitch);
        }
        ok = stbi_write_png(path.c_str(),
            static_cast<int>(m_width), static_cast<int>(m_height),
            4, tight.data(), static_cast<int>(tightPitch)) != 0;
    }

    m_device->UnmapBuffer(m_readbackBuffer);

    if (!ok) {
        UZ_LOG(Screenshot, Error, "stbi_write_png failed: path='{}'", path);
        return false;
    }

    UZ_LOG(Screenshot, Info, "Screenshot saved: path='{}' ({}x{})", path, m_width, m_height);
    return true;
}

} // namespace uz
