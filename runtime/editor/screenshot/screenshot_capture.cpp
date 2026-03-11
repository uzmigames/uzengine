/// UzEngine Screenshot Capture Implementation
///
/// Two-phase GPU readback: CaptureBackBuffer() records copy commands,
/// SaveToPNG() reads the mapped readback buffer and writes a PNG file.

#include "screenshot_capture.h"

#include <cstring>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace uz {

UZ_DEFINE_LOG_CATEGORY(Screenshot, Info);

/// D3D12 requires texture row pitch aligned to 256 bytes.
static constexpr uint32_t kTexturePitchAlignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

/// Aligns a value up to the given power-of-two alignment.
static uint32_t AlignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool ScreenshotCapture::Init(DX12Device* device, uint32_t width, uint32_t height) {
    m_device = device;
    m_width = width;
    m_height = height;

    // RGBA8 = 4 bytes per pixel, aligned to D3D12_TEXTURE_DATA_PITCH_ALIGNMENT
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

void ScreenshotCapture::CaptureBackBuffer(DX12CommandList* cmdList, DX12SwapChain* swapChain) {
    ID3D12Resource* backBuffer = swapChain->GetCurrentBackBufferResource();

    auto* buf = m_device->GetBufferPool().Get(m_readbackBuffer.index, m_readbackBuffer.generation);
    if (!buf) {
        UZ_LOG(Screenshot, Error, "Readback buffer handle invalid");
        return;
    }

    // Transition back buffer: RenderTarget → CopySrc
    cmdList->TransitionBarrier(backBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    // Set up texture → buffer copy via CopyTextureRegion
    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = backBuffer;
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = buf->resource;
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.PlacedFootprint.Offset = 0;
    dstLoc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dstLoc.PlacedFootprint.Footprint.Width = m_width;
    dstLoc.PlacedFootprint.Footprint.Height = m_height;
    dstLoc.PlacedFootprint.Footprint.Depth = 1;
    dstLoc.PlacedFootprint.Footprint.RowPitch = m_rowPitch;

    cmdList->GetNative()->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    // Transition back buffer: CopySrc → RenderTarget (caller will transition to Present)
    cmdList->TransitionBarrier(backBuffer,
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
}

// ─────────────────────────────────────────────────────────────────────────────
// Save
// ─────────────────────────────────────────────────────────────────────────────

bool ScreenshotCapture::SaveToPNG(const std::string& path) const {
    if (!m_device || !m_readbackBuffer.IsValid()) {
        UZ_LOG(Screenshot, Error, "Cannot save — capture not initialized");
        return false;
    }

    auto* buf = m_device->GetBufferPool().Get(m_readbackBuffer.index, m_readbackBuffer.generation);
    if (!buf || !buf->mappedPtr) {
        UZ_LOG(Screenshot, Error, "Cannot save — readback buffer not mapped");
        return false;
    }

    // If row pitch matches tight packing, write directly.
    // Otherwise, copy row-by-row to remove padding.
    const uint32_t tightPitch = m_width * 4;
    const auto* srcBytes = static_cast<const uint8_t*>(buf->mappedPtr);

    if (m_rowPitch == tightPitch) {
        int result = stbi_write_png(path.c_str(),
            static_cast<int>(m_width), static_cast<int>(m_height),
            4, srcBytes, static_cast<int>(tightPitch));
        if (!result) {
            UZ_LOG(Screenshot, Error, "stbi_write_png failed: path='{}'", path);
            return false;
        }
    } else {
        // Remove row padding — stb needs tight rows
        std::vector<uint8_t> tight(tightPitch * m_height);
        for (uint32_t row = 0; row < m_height; ++row) {
            std::memcpy(tight.data() + row * tightPitch,
                        srcBytes + row * m_rowPitch,
                        tightPitch);
        }
        int result = stbi_write_png(path.c_str(),
            static_cast<int>(m_width), static_cast<int>(m_height),
            4, tight.data(), static_cast<int>(tightPitch));
        if (!result) {
            UZ_LOG(Screenshot, Error, "stbi_write_png failed: path='{}'", path);
            return false;
        }
    }

    UZ_LOG(Screenshot, Info, "Screenshot saved: path='{}' ({}x{})", path, m_width, m_height);
    return true;
}

} // namespace uz
