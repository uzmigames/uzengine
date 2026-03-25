/// UzEngine Screenshot Capture Implementation — RHI-Agnostic
///
/// Two-phase GPU readback: CaptureBackBuffer() records copy commands via
/// the abstract RHI interface, SaveToPNG() reads the mapped readback buffer
/// and writes a PNG file. Works with any backend (DX12, DX11, Vulkan).

#include "screenshot_capture.h"

#include <algorithm>
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
    m_captureFormat = swapChain->GetFormat();
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

void ScreenshotCapture::CaptureFromPresent(IRHICommandList* cmdList, IRHISwapChain* swapChain) {
    m_captureFormat = swapChain->GetFormat();
    RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();

    // Transition: Present → CopySrc
    RHIBarrierDesc barrier{};
    barrier.texture = backBuffer;
    barrier.stateBefore = RHIResourceState::Present;
    barrier.stateAfter = RHIResourceState::CopySrc;
    cmdList->ResourceBarrier(&barrier, 1);

    // Copy texture → readback buffer
    cmdList->CopyTextureToBuffer(backBuffer, 0, m_readbackBuffer, 0, m_rowPitch);

    // Transition: CopySrc → Present
    barrier.stateBefore = RHIResourceState::CopySrc;
    barrier.stateAfter = RHIResourceState::Present;
    cmdList->ResourceBarrier(&barrier, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// MapReadback / UnmapReadback — direct pixel access for H.264 encoder
// ─────────────────────────────────────────────────────────────────────────────

bool ScreenshotCapture::MapReadback(const uint8_t** outData, uint32_t* outStride) const {
    if (!m_device || !m_readbackBuffer.IsValid() || !outData || !outStride) {
        return false;
    }

    void* mapped = m_device->MapBuffer(m_readbackBuffer);
    if (!mapped) {
        UZ_LOG(Screenshot, Error, "MapReadback: failed to map readback buffer");
        return false;
    }

    *outData = static_cast<const uint8_t*>(mapped);
    *outStride = m_rowPitch;
    return true;
}

void ScreenshotCapture::UnmapReadback() const {
    if (m_device && m_readbackBuffer.IsValid()) {
        m_device->UnmapBuffer(m_readbackBuffer);
    }
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

    // Always copy to tight buffer (removes row padding and handles BGRA swizzle)
    std::vector<uint8_t> tight(tightPitch * m_height);
    for (uint32_t row = 0; row < m_height; ++row) {
        std::memcpy(tight.data() + row * tightPitch,
                    srcBytes + row * m_rowPitch,
                    tightPitch);
    }

    // BGRA→RGBA swizzle: swap R and B channels so PNG is always RGBA.
    // Vulkan swapchains commonly use B8G8R8A8_UNORM when R8G8B8A8 is unavailable.
    const bool needsSwizzle = (m_captureFormat == RHIFormat::BGRA8Unorm ||
                               m_captureFormat == RHIFormat::BGRA8Srgb);
    for (uint32_t i = 0; i < tightPitch * m_height; i += 4) {
        if (needsSwizzle) {
            std::swap(tight[i + 0], tight[i + 2]); // B↔R
        }
        tight[i + 3] = 255; // Force alpha=1 — backbuffer alpha is undefined
    }

    // Save as JPG if path ends with .jpg/.jpeg, otherwise PNG
    bool ok = false;
    const auto dotPos = path.rfind('.');
    const bool isJpg = (dotPos != std::string::npos) &&
        (path.compare(dotPos, 4, ".jpg") == 0 || path.compare(dotPos, 5, ".jpeg") == 0);

    ok = stbi_write_png(path.c_str(),
        static_cast<int>(m_width), static_cast<int>(m_height),
        4, tight.data(), static_cast<int>(tightPitch)) != 0;

    if (!ok) {
        UZ_LOG(Screenshot, Error, "stbi_write_png failed: path='{}'", path);
        return false;
    }

    UZ_LOG(Screenshot, Info, "Screenshot saved: path='{}' ({}x{})", path, m_width, m_height);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Base64 encoding
// ─────────────────────────────────────────────────────────────────────────────

/// RFC 4648 base64 encoding table.
static constexpr char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// Encodes raw bytes to a base64 string.
static std::string Base64Encode(const uint8_t* data, size_t length) {
    std::string out;
    out.reserve(((length + 2) / 3) * 4);

    for (size_t i = 0; i < length; i += 3) {
        const uint32_t b0 = data[i];
        const uint32_t b1 = (i + 1 < length) ? data[i + 1] : 0;
        const uint32_t b2 = (i + 2 < length) ? data[i + 2] : 0;
        const uint32_t triplet = (b0 << 16) | (b1 << 8) | b2;

        out += kBase64Table[(triplet >> 18) & 0x3F];
        out += kBase64Table[(triplet >> 12) & 0x3F];
        out += (i + 1 < length) ? kBase64Table[(triplet >> 6) & 0x3F] : '=';
        out += (i + 2 < length) ? kBase64Table[triplet & 0x3F] : '=';
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// stb_image_write callback for in-memory PNG
// ─────────────────────────────────────────────────────────────────────────────

/// Callback for stbi_write_png_to_func — appends bytes to a std::vector.
static void StbiWriteToVector(void* context, void* data, int size) {
    auto* vec = static_cast<std::vector<uint8_t>*>(context);
    const auto* bytes = static_cast<const uint8_t*>(data);
    vec->insert(vec->end(), bytes, bytes + size);
}

// ─────────────────────────────────────────────────────────────────────────────
// ToBase64PNG
// ─────────────────────────────────────────────────────────────────────────────

std::string ScreenshotCapture::ToBase64PNG() const {
    if (!m_device || !m_readbackBuffer.IsValid()) {
        UZ_LOG(Screenshot, Error, "ToBase64PNG: capture not initialized");
        return {};
    }

    void* mapped = m_device->MapBuffer(m_readbackBuffer);
    if (!mapped) {
        UZ_LOG(Screenshot, Error, "ToBase64PNG: failed to map readback buffer");
        return {};
    }

    const uint32_t tightPitch = m_width * 4;
    const auto* srcBytes = static_cast<const uint8_t*>(mapped);

    // Copy to tight buffer (remove row padding + BGRA swizzle)
    std::vector<uint8_t> tight(tightPitch * m_height);
    for (uint32_t row = 0; row < m_height; ++row) {
        std::memcpy(tight.data() + row * tightPitch,
                    srcBytes + row * m_rowPitch,
                    tightPitch);
    }

    const bool needsSwizzle = (m_captureFormat == RHIFormat::BGRA8Unorm ||
                               m_captureFormat == RHIFormat::BGRA8Srgb);
    for (uint32_t i = 0; i < tightPitch * m_height; i += 4) {
        if (needsSwizzle) {
            std::swap(tight[i + 0], tight[i + 2]);
        }
        tight[i + 3] = 255;
    }

    // Encode to in-memory PNG
    std::vector<uint8_t> pngData;
    pngData.reserve(tightPitch * m_height / 2); // rough estimate
    int ok = stbi_write_png_to_func(StbiWriteToVector, &pngData,
        static_cast<int>(m_width), static_cast<int>(m_height),
        4, tight.data(), static_cast<int>(tightPitch));

    if (!ok || pngData.empty()) {
        UZ_LOG(Screenshot, Error, "ToBase64PNG: stbi_write_png_to_func failed");
        return {};
    }

    // Base64 encode the PNG bytes
    std::string b64 = Base64Encode(pngData.data(), pngData.size());
    UZ_LOG(Screenshot, Info, "ToBase64PNG: {}x{}, PNG={} bytes, base64={} chars",
           m_width, m_height, pngData.size(), b64.size());
    return b64;
}

} // namespace uz
