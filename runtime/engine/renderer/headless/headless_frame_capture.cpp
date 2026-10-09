#include "headless_frame_capture.h"

#include "log.h"

// stb_image_write is included as implementation only in
// editor/screenshot/screenshot_capture.cpp. Here we just need the
// declaration. The symbol `stbi_write_png_to_func` links against that
// single TU in the engine runtime.
#include "stb_image_write.h"

#include <cstring>

namespace uz {

UZ_DEFINE_LOG_CATEGORY(HeadlessCapture, Info);

namespace {

/// Matches the 256-byte constant used by ScreenshotCapture. DX12 requires
/// it; DX11 and Vulkan accept any alignment.
constexpr uint32_t kTexturePitchAlignment = 256u;

uint32_t AlignUp(uint32_t value, uint32_t alignment) noexcept {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

uint32_t BytesPerPixel(RHIFormat f) noexcept {
    switch (f) {
        case RHIFormat::RGBA8Unorm:
        case RHIFormat::RGBA8Srgb:
        case RHIFormat::BGRA8Unorm:
        case RHIFormat::BGRA8Srgb:
            return 4u;
        case RHIFormat::RGBA16Float: return 8u;
        default: return 0u;
    }
}

/// stb_image_write callback that appends into a std::vector<uint8_t>.
void StbVectorWriter(void* context, void* data, int size) {
    auto* vec = static_cast<std::vector<uint8_t>*>(context);
    const auto* bytes = static_cast<const uint8_t*>(data);
    vec->insert(vec->end(), bytes, bytes + size);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool HeadlessFrameCapture::Init(IRHIDevice* device, uint32_t width,
                                  uint32_t height, RHIFormat format) {
    if (device == nullptr || width == 0u || height == 0u) {
        UZ_LOG(HeadlessCapture, Error,
               "HeadlessFrameCapture::Init: invalid args (device={}, {}x{})",
               device != nullptr, width, height);
        return false;
    }
    const uint32_t bpp = BytesPerPixel(format);
    if (bpp == 0u) {
        UZ_LOG(HeadlessCapture, Error,
               "HeadlessFrameCapture::Init: unsupported format {}",
               static_cast<uint32_t>(format));
        return false;
    }

    // Off-screen RTV (render target + copy source).
    RHITextureDesc tdesc{};
    tdesc.width            = width;
    tdesc.height           = height;
    tdesc.depthOrArraySize = 1;
    tdesc.mipLevels        = 1;
    tdesc.format           = format;
    tdesc.usage     = RHITextureUsage::RenderTarget |
                      RHITextureUsage::CopySrc |
                      RHITextureUsage::ShaderResource;
    tdesc.debugName = "HeadlessFrameCapture_RTV";

    RHITextureHandle rtv = device->CreateTexture(tdesc);
    if (!rtv.IsValid()) {
        UZ_LOG(HeadlessCapture, Error,
               "HeadlessFrameCapture::Init: RTV allocation failed ({}x{})",
               width, height);
        return false;
    }

    // Readback buffer (CPU-readable, aligned row pitch).
    const uint32_t rowPitch = AlignUp(width * bpp, kTexturePitchAlignment);
    RHIBufferDesc bdesc{};
    bdesc.sizeBytes = static_cast<uint64_t>(rowPitch) * height;
    bdesc.memory    = RHIMemoryType::Readback;
    bdesc.usage     = RHIBufferUsage::CopyDst;
    bdesc.debugName = "HeadlessFrameCapture_Readback";

    RHIBufferHandle readback = device->CreateBuffer(bdesc);
    if (!readback.IsValid()) {
        device->DestroyTexture(rtv);
        UZ_LOG(HeadlessCapture, Error,
               "HeadlessFrameCapture::Init: readback buffer failed");
        return false;
    }

    m_device       = device;
    m_rtv          = rtv;
    m_readback     = readback;
    m_width        = width;
    m_height       = height;
    m_rowPitch     = rowPitch;
    m_format       = format;
    m_inFrame      = false;
    m_firstCapture = true;
    return true;
}

void HeadlessFrameCapture::Shutdown() {
    if (m_device != nullptr) {
        if (m_rtv.IsValid())      m_device->DestroyTexture(m_rtv);
        if (m_readback.IsValid()) m_device->DestroyBuffer(m_readback);
    }
    m_device = nullptr;
    m_rtv          = {};
    m_readback     = {};
    m_width        = 0;
    m_height       = 0;
    m_rowPitch     = 0;
    m_inFrame      = false;
    m_firstCapture = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// BeginFrame / EndFrame
// ─────────────────────────────────────────────────────────────────────────────

void HeadlessFrameCapture::BeginFrame(IRHICommandList* cmdList) {
    if (!IsInitialized() || cmdList == nullptr) return;
    if (m_inFrame) {
        UZ_LOG(HeadlessCapture, Warning,
               "HeadlessFrameCapture::BeginFrame called twice without EndFrame");
        return;
    }

    RHIBarrierDesc barrier{};
    barrier.texture     = m_rtv;
    barrier.stateBefore = m_firstCapture ? RHIResourceState::Undefined
                                          : RHIResourceState::CopySrc;
    barrier.stateAfter  = RHIResourceState::RenderTarget;
    cmdList->ResourceBarrier(&barrier, 1);

    m_inFrame = true;
}

void HeadlessFrameCapture::EndFrame(IRHICommandList* cmdList) {
    if (!IsInitialized() || cmdList == nullptr) return;
    if (!m_inFrame) {
        UZ_LOG(HeadlessCapture, Warning,
               "HeadlessFrameCapture::EndFrame called without BeginFrame");
        return;
    }

    // RenderTarget → CopySrc, copy to readback, leave in CopySrc so the
    // next BeginFrame transitions back to RenderTarget from a known state.
    RHIBarrierDesc barrier{};
    barrier.texture     = m_rtv;
    barrier.stateBefore = RHIResourceState::RenderTarget;
    barrier.stateAfter  = RHIResourceState::CopySrc;
    cmdList->ResourceBarrier(&barrier, 1);

    cmdList->CopyTextureToBuffer(m_rtv, /*mip*/0, m_readback, /*offset*/0,
                                   m_rowPitch);

    m_inFrame      = false;
    m_firstCapture = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// ReadPixels
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> HeadlessFrameCapture::ReadPixels() const {
    if (!IsInitialized()) return {};

    void* mapped = m_device->MapBuffer(m_readback);
    if (mapped == nullptr) {
        UZ_LOG(HeadlessCapture, Error, "HeadlessFrameCapture: MapBuffer failed");
        return {};
    }

    const uint32_t bpp = BytesPerPixel(m_format);
    const uint32_t tightRowBytes = m_width * bpp;
    std::vector<uint8_t> out(static_cast<size_t>(tightRowBytes) * m_height);

    const uint8_t* src = static_cast<const uint8_t*>(mapped);
    for (uint32_t y = 0; y < m_height; ++y) {
        std::memcpy(out.data() + static_cast<size_t>(y) * tightRowBytes,
                    src + static_cast<size_t>(y) * m_rowPitch,
                    tightRowBytes);
    }

    m_device->UnmapBuffer(m_readback);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// PNG encode
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> HeadlessFrameCapture::ReadPNG() const {
    if (!IsInitialized()) return {};
    const auto rgba = ReadPixels();
    if (rgba.empty()) return {};

    std::vector<uint8_t> png;
    const int ok = stbi_write_png_to_func(
        StbVectorWriter, &png,
        static_cast<int>(m_width), static_cast<int>(m_height),
        /*components=*/4, rgba.data(),
        /*strideBytes=*/static_cast<int>(m_width * 4));
    if (!ok) {
        UZ_LOG(HeadlessCapture, Error, "HeadlessFrameCapture: PNG encode failed");
        return {};
    }
    return png;
}

bool HeadlessFrameCapture::SaveToPNG(const std::string& path) const {
    const auto png = ReadPNG();
    if (png.empty()) return false;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        UZ_LOG(HeadlessCapture, Error,
               "HeadlessFrameCapture: cannot open '{}' for write", path);
        return false;
    }
    std::fwrite(png.data(), 1, png.size(), f);
    std::fclose(f);
    return true;
}

} // namespace uz
