/// UzEngine Pass Inspector — Implementation.
///
/// Manages per-frame target registration, visualization target lookup,
/// readback-based validation, pass toggling, comparison, dump to PNG,
/// stats collection, and grid visualization for the render pass inspector.

#include "inspector_pass.h"

#include "rhi.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

// stb_image_write — implementation lives in screenshot_capture.cpp.
// We only need the function declarations here for stbi_write_png.
#include "stb_image_write.h"

UZ_DEFINE_LOG_CATEGORY(PassInspector, Trace);

namespace uz {

namespace {

/// Converts an IEEE 754 half-precision (binary16) value to single-precision float.
/// Handles normalized values, denormals, zero, infinity, and NaN.
/// Uses bit manipulation for portability (no intrinsics).
[[nodiscard]] float HalfToFloat(uint16_t h) noexcept
{
    const uint32_t sign     = (static_cast<uint32_t>(h) & 0x8000u) << 16;
    const uint32_t exponent = (static_cast<uint32_t>(h) >> 10) & 0x1Fu;
    const uint32_t mantissa =  static_cast<uint32_t>(h) & 0x03FFu;

    uint32_t result = 0;

    if (exponent == 0) {
        if (mantissa == 0) {
            // Signed zero
            result = sign;
        } else {
            // Denormalized — normalize by shifting mantissa until the implicit 1 bit appears
            uint32_t m = mantissa;
            uint32_t e = 0;
            while ((m & 0x0400u) == 0) {
                m <<= 1;
                ++e;
            }
            m &= 0x03FFu;
            result = sign | ((127u - 15u + 1u - e) << 23) | (m << 13);
        }
    } else if (exponent == 31) {
        // Infinity or NaN — preserve mantissa bits for NaN payload
        result = sign | 0x7F800000u | (mantissa << 13);
    } else {
        // Normalized — re-bias exponent from half (bias 15) to float (bias 127)
        result = sign | ((exponent + 127u - 15u) << 23) | (mantissa << 13);
    }

    float f;
    std::memcpy(&f, &result, sizeof(f));
    return f;
}

/// Returns the number of float-equivalent channels in a format for validation sampling.
/// For formats with < 4 channels, only the present channels are sampled.
[[nodiscard]] uint32_t ChannelCount(RHIFormat format) noexcept
{
    switch (format) {
        case RHIFormat::R8Unorm:        return 1;
        case RHIFormat::RG8Unorm:       return 2;
        case RHIFormat::RGBA8Unorm:     return 4;
        case RHIFormat::RGBA8Srgb:      return 4;
        case RHIFormat::BGRA8Unorm:     return 4;
        case RHIFormat::BGRA8Srgb:      return 4;
        case RHIFormat::R16Float:       return 1;
        case RHIFormat::RG16Float:      return 2;
        case RHIFormat::RGBA16Float:    return 4;
        case RHIFormat::R32Float:       return 1;
        case RHIFormat::RG32Float:      return 2;
        case RHIFormat::RGB32Float:     return 3;
        case RHIFormat::RGBA32Float:    return 4;
        case RHIFormat::R32Uint:        return 1;
        case RHIFormat::RG32Uint:       return 2;
        case RHIFormat::RGBA32Uint:     return 4;
        case RHIFormat::D16Unorm:       return 1;
        case RHIFormat::D32Float:       return 1;
        case RHIFormat::D24UnormS8Uint: return 1; // Only sample the depth channel
        case RHIFormat::D32FloatS8Uint: return 1; // Only sample the depth channel
        default:                        return 0;
    }
}

/// Reads a single channel value from raw readback data at the given byte offset,
/// converting to float according to the texture format.
[[nodiscard]] float ReadChannel(const uint8_t* texelBase, uint32_t channelIndex,
                                RHIFormat format) noexcept
{
    switch (format) {
        // ── 8-bit unsigned normalized ────────────────────────────────────
        case RHIFormat::R8Unorm:
            return static_cast<float>(texelBase[channelIndex]) / 255.0f;
        case RHIFormat::RG8Unorm:
            return static_cast<float>(texelBase[channelIndex]) / 255.0f;
        case RHIFormat::RGBA8Unorm:
        case RHIFormat::RGBA8Srgb:
            return static_cast<float>(texelBase[channelIndex]) / 255.0f;
        case RHIFormat::BGRA8Unorm:
        case RHIFormat::BGRA8Srgb:
            // BGRA layout: byte 0=B, 1=G, 2=R, 3=A. Remap to logical RGBA order.
            {
                static constexpr uint32_t bgraToRgba[4] = {2, 1, 0, 3};
                return static_cast<float>(texelBase[bgraToRgba[channelIndex]]) / 255.0f;
            }

        // ── 16-bit half-float ────────────────────────────────────────────
        case RHIFormat::R16Float:
        case RHIFormat::RG16Float:
        case RHIFormat::RGBA16Float: {
            uint16_t halfBits;
            std::memcpy(&halfBits, texelBase + channelIndex * sizeof(uint16_t), sizeof(uint16_t));
            return HalfToFloat(halfBits);
        }

        // ── 32-bit float ─────────────────────────────────────────────────
        case RHIFormat::R32Float:
        case RHIFormat::RG32Float:
        case RHIFormat::RGB32Float:
        case RHIFormat::RGBA32Float: {
            float val;
            std::memcpy(&val, texelBase + channelIndex * sizeof(float), sizeof(float));
            return val;
        }

        // ── 32-bit unsigned integer (reinterpret as float for range check) ──
        case RHIFormat::R32Uint:
        case RHIFormat::RG32Uint:
        case RHIFormat::RGBA32Uint: {
            uint32_t uval;
            std::memcpy(&uval, texelBase + channelIndex * sizeof(uint32_t), sizeof(uint32_t));
            return static_cast<float>(uval);
        }

        // ── Depth formats ────────────────────────────────────────────────
        case RHIFormat::D16Unorm: {
            uint16_t d16;
            std::memcpy(&d16, texelBase, sizeof(uint16_t));
            return static_cast<float>(d16) / 65535.0f;
        }
        case RHIFormat::D32Float:
        case RHIFormat::D32FloatS8Uint: {
            float depth;
            std::memcpy(&depth, texelBase, sizeof(float));
            return depth;
        }
        case RHIFormat::D24UnormS8Uint: {
            uint32_t packed;
            std::memcpy(&packed, texelBase, sizeof(uint32_t));
            return static_cast<float>(packed & 0x00FFFFFFu) / 16777215.0f;
        }

        default:
            return 0.0f;
    }
}

/// Returns true if the given format is a depth/stencil format.
[[nodiscard]] bool IsDepthFormat(RHIFormat format) noexcept
{
    return format == RHIFormat::D16Unorm
        || format == RHIFormat::D32Float
        || format == RHIFormat::D24UnormS8Uint
        || format == RHIFormat::D32FloatS8Uint;
}

/// Reads back a full texture to CPU memory.
/// Returns an RGBA8 pixel buffer (width * height * 4 bytes) suitable for stbi_write_png.
/// Returns empty vector on failure.
[[nodiscard]] std::vector<uint8_t> ReadbackTextureToRGBA8(
    IRHIDevice* device, IRHICommandList* cmdList,
    const RegisteredTarget& target,
    RHIBufferHandle sharedReadbackBuffer, uint64_t sharedBufferSize)
{
    std::vector<uint8_t> pixels;

    const RHITextureDesc texDesc = device->GetTextureDesc(target.texture);
    const RHIFormat format       = texDesc.format;
    const uint32_t bytesPerTexel = BytesPerTexel(format);
    const uint32_t channels      = ChannelCount(format);

    if (bytesPerTexel == 0 || channels == 0 || IsDepthFormat(format)) {
        return pixels;
    }

    const uint32_t rowPitch = ((target.width * bytesPerTexel + 255) & ~255u);
    const uint64_t totalBytes = static_cast<uint64_t>(rowPitch) * target.height;

    // Skip if target exceeds the shared readback buffer size.
    if (totalBytes > sharedBufferSize) {
        return pixels;
    }

    RHIBufferHandle readbackBuffer = sharedReadbackBuffer;

    // Record barrier + copy + restore barrier.
    cmdList->Begin();
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = target.texture;
        barrier.stateBefore = target.lastState;
        barrier.stateAfter  = RHIResourceState::CopySrc;
        cmdList->ResourceBarrier(&barrier, 1);

        cmdList->CopyTextureToBuffer(target.texture, 0,
                                      readbackBuffer, 0, rowPitch, 0);

        barrier.stateBefore = RHIResourceState::CopySrc;
        barrier.stateAfter  = target.lastState;
        cmdList->ResourceBarrier(&barrier, 1);
    }
    cmdList->End();
    cmdList->Execute();
    device->WaitIdle();

    void* mapped = device->MapBuffer(readbackBuffer);
    if (!mapped) {
        return pixels;
    }

    // Convert to RGBA8 for PNG output.
    pixels.resize(static_cast<size_t>(target.width) * target.height * 4u);

    for (uint32_t y = 0; y < target.height; ++y) {
        const auto* rowBase = static_cast<const uint8_t*>(mapped)
                            + static_cast<uint64_t>(y) * rowPitch;

        for (uint32_t x = 0; x < target.width; ++x) {
            const auto* texelBase = rowBase + x * bytesPerTexel;
            const size_t pixelOffset = (static_cast<size_t>(y) * target.width + x) * 4u;

            float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

            if (channels >= 1) r = ReadChannel(texelBase, 0, format);
            if (channels >= 2) g = ReadChannel(texelBase, 1, format);
            if (channels >= 3) b = ReadChannel(texelBase, 2, format);
            if (channels >= 4) a = ReadChannel(texelBase, 3, format);

            // Handle NaN/Inf by replacing with magenta (diagnostic color).
            if (std::isnan(r) || std::isinf(r)) r = 1.0f;
            if (std::isnan(g) || std::isinf(g)) g = 0.0f;
            if (std::isnan(b) || std::isinf(b)) b = 1.0f;
            if (std::isnan(a) || std::isinf(a)) a = 1.0f;

            // For grayscale formats, replicate R across RGB.
            if (channels == 1) { g = r; b = r; }
            if (channels == 2) { b = 0.0f; }

            // HDR tonemap for display: simple Reinhard per channel.
            r = r / (1.0f + r);
            g = g / (1.0f + g);
            b = b / (1.0f + b);

            // Clamp and convert to 8-bit.
            auto clamp8 = [](float v) -> uint8_t {
                return static_cast<uint8_t>(std::min(std::max(v, 0.0f), 1.0f) * 255.0f + 0.5f);
            };

            pixels[pixelOffset + 0] = clamp8(r);
            pixels[pixelOffset + 1] = clamp8(g);
            pixels[pixelOffset + 2] = clamp8(b);
            pixels[pixelOffset + 3] = clamp8(a);
        }
    }

    device->UnmapBuffer(readbackBuffer);
    // Buffer is shared — caller owns lifetime.

    return pixels;
}

/// Generates an INDEX.html file for browsing dump PNGs.
void GenerateIndexHtml(const std::string& dir, const std::vector<std::string>& filenames,
                       uint32_t frameNumber)
{
    const std::string htmlPath = dir + "/INDEX.html";
    std::ofstream ofs(htmlPath);
    if (!ofs.is_open()) {
        UZ_LOG(PassInspector, Warning, "PassInspector: failed to create {}", htmlPath);
        return;
    }

    ofs << "<!DOCTYPE html>\n<html>\n<head>\n<title>Pass Dump — Frame " << frameNumber << "</title>\n";
    ofs << "<style>\n";
    ofs << "body { background: #1a1a1a; color: #e0e0e0; font-family: monospace; margin: 20px; }\n";
    ofs << "h1 { color: #80ff80; }\n";
    ofs << ".grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(300px, 1fr)); gap: 10px; }\n";
    ofs << ".cell { background: #2a2a2a; padding: 10px; border-radius: 6px; }\n";
    ofs << ".cell img { width: 100%; cursor: pointer; }\n";
    ofs << ".cell .label { font-size: 13px; margin-bottom: 5px; color: #80ff80; }\n";
    ofs << ".fullscreen { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; ";
    ofs << "background: rgba(0,0,0,0.9); z-index: 100; cursor: pointer; }\n";
    ofs << ".fullscreen img { max-width: 95%; max-height: 95%; margin: auto; display: block; ";
    ofs << "position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); }\n";
    ofs << "</style>\n</head>\n<body>\n";
    ofs << "<h1>Pass Dump — Frame " << frameNumber << " (" << filenames.size() << " targets)</h1>\n";
    ofs << "<div class=\"grid\">\n";

    for (const auto& fn : filenames) {
        ofs << "  <div class=\"cell\">\n";
        ofs << "    <div class=\"label\">" << fn << "</div>\n";
        ofs << "    <img src=\"" << fn << "\" onclick=\"showFull(this.src)\">\n";
        ofs << "  </div>\n";
    }

    ofs << "</div>\n";
    ofs << "<div class=\"fullscreen\" id=\"fullview\" onclick=\"this.style.display='none'\">\n";
    ofs << "  <img id=\"fullimg\">\n</div>\n";
    ofs << "<script>\nfunction showFull(src) {\n";
    ofs << "  document.getElementById('fullimg').src = src;\n";
    ofs << "  document.getElementById('fullview').style.display = 'block';\n";
    ofs << "}\n</script>\n</body>\n</html>\n";
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Per-frame lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void PassInspector::BeginFrame() {
    m_targets.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Target registration
// ─────────────────────────────────────────────────────────────────────────────

void PassInspector::RegisterTarget(const char* name, RHITextureHandle texture, uint32_t srvIndex,
                                    VisDisplayMode mode, uint32_t width, uint32_t height,
                                    RHIResourceState state) {
    if (!name || !texture.IsValid() || srvIndex == UINT32_MAX) {
        return;
    }

    RegisteredTarget target;
    target.name      = name;
    target.texture   = texture;
    target.srvIndex  = srvIndex;
    target.mode      = mode;
    target.width     = width;
    target.height    = height;
    target.lastState = state;

    m_targets.push_back(std::move(target));
}

// ─────────────────────────────────────────────────────────────────────────────
// Visualization control
// ─────────────────────────────────────────────────────────────────────────────

void PassInspector::SetVisTarget(const char* target) {
    if (!target || target[0] == '\0') {
        ClearVis();
        return;
    }

    // "off" and "final" are special — they disable visualization.
    if (std::strcmp(target, "off") == 0 || std::strcmp(target, "final") == 0) {
        ClearVis();
        return;
    }

    m_visTargetName = target;
    UZ_LOG(PassInspector, Info, "PassInspector: vis target set to '{}'", m_visTargetName);
}

void PassInspector::ClearVis() {
    if (!m_visTargetName.empty()) {
        UZ_LOG(PassInspector, Info, "PassInspector: vis disabled");
    }
    m_visTargetName.clear();
}

const RegisteredTarget* PassInspector::GetVisTarget() const noexcept {
    if (m_visTargetName.empty()) {
        return nullptr;
    }

    for (const auto& target : m_targets) {
        if (target.name == m_visTargetName) {
            return &target;
        }
    }

    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Validation
// ─────────────────────────────────────────────────────────────────────────────

std::vector<ValidationResult> PassInspector::Validate(IRHIDevice* device, IRHICommandList* cmdList) {
    std::vector<ValidationResult> results;

    if (!device || !cmdList || m_targets.empty()) {
        return results;
    }

    results.reserve(m_targets.size());

    static constexpr uint32_t kSampleDim = 4;

    // The command list may not be in recording state. Begin a fresh recording.
    cmdList->Begin();

    for (const auto& target : m_targets) {
        ValidationResult result;
        result.name    = target.name;
        result.isZero  = true;
        result.hasNaN  = false;
        result.hasInf  = false;
        result.minVal  = 1e30f;
        result.maxVal  = -1e30f;
        result.avgVal  = 0.0f;

        // Query the actual texture format from the RHI.
        const RHITextureDesc texDesc = device->GetTextureDesc(target.texture);
        const RHIFormat format       = texDesc.format;
        const uint32_t bytesPerTexel = BytesPerTexel(format);
        const uint32_t channels      = ChannelCount(format);

        if (bytesPerTexel == 0 || channels == 0) {
            UZ_LOG(PassInspector, Warning,
                   "[PASS VALIDATE] {}: SKIPPED (unsupported format {})",
                   target.name, static_cast<int>(format));
            result.minVal = 0.0f;
            result.maxVal = 0.0f;
            result.avgVal = 0.0f;
            results.push_back(std::move(result));
            continue;
        }

        if (IsDepthFormat(format)) {
            result.minVal = 0.0f;
            result.maxVal = 0.0f;
            result.avgVal = 0.0f;
            result.isZero = false; // Don't flag as zero — depth not validated.
            UZ_LOG(PassInspector, Info,
                   "[PASS VALIDATE] {}: SKIPPED (depth format — readback not supported)",
                   target.name);
            results.push_back(std::move(result));
            continue;
        }

        const uint32_t rowPitch = ((target.width * bytesPerTexel + 255) & ~255u);
        const uint64_t totalBytes = static_cast<uint64_t>(rowPitch) * target.height;

        // Skip very large textures to avoid huge allocations during validation.
        if (totalBytes > 16u * 1024u * 1024u) {
            result.minVal = 0.0f;
            result.maxVal = 0.0f;
            result.avgVal = 0.0f;
            result.isZero = false;
            UZ_LOG(PassInspector, Info,
                   "[PASS VALIDATE] {}: SKIPPED (texture too large: {}x{}, {} bytes)",
                   target.name, target.width, target.height, totalBytes);
            results.push_back(std::move(result));
            continue;
        }

        RHIBufferDesc readbackDesc{};
        readbackDesc.sizeBytes = totalBytes;
        readbackDesc.stride    = bytesPerTexel;
        readbackDesc.usage     = RHIBufferUsage::CopyDst;
        readbackDesc.memory    = RHIMemoryType::Readback;
        readbackDesc.debugName = "PassInspector_Validate";

        RHIBufferHandle readbackBuffer = device->CreateBuffer(readbackDesc);
        if (!readbackBuffer.IsValid()) {
            UZ_LOG(PassInspector, Warning,
                   "[PASS VALIDATE] {}: SKIPPED (failed to create readback buffer)",
                   target.name);
            result.minVal = 0.0f;
            result.maxVal = 0.0f;
            result.avgVal = 0.0f;
            results.push_back(std::move(result));
            continue;
        }

        // Record barrier + copy + restore barrier for this single target.
        {
            RHIBarrierDesc barrier{};
            barrier.texture     = target.texture;
            barrier.stateBefore = target.lastState;
            barrier.stateAfter  = RHIResourceState::CopySrc;
            cmdList->ResourceBarrier(&barrier, 1);

            cmdList->CopyTextureToBuffer(target.texture, 0,
                                          readbackBuffer, 0, rowPitch, 0);

            barrier.stateBefore = RHIResourceState::CopySrc;
            barrier.stateAfter  = target.lastState;
            cmdList->ResourceBarrier(&barrier, 1);
        }

        cmdList->End();
        cmdList->Execute();
        device->WaitIdle();
        cmdList->Begin();

        // Map and sample a 4x4 block from the center.
        void* mapped = device->MapBuffer(readbackBuffer);
        if (mapped) {
            const uint32_t centerX = (target.width  > kSampleDim) ? (target.width  / 2 - kSampleDim / 2) : 0;
            const uint32_t centerY = (target.height > kSampleDim) ? (target.height / 2 - kSampleDim / 2) : 0;
            const uint32_t sampleW = std::min(kSampleDim, target.width);
            const uint32_t sampleH = std::min(kSampleDim, target.height);

            uint32_t sampleCount = 0;
            double sum = 0.0;

            for (uint32_t y = 0; y < sampleH; ++y) {
                const auto* rowBase = static_cast<const uint8_t*>(mapped)
                                    + static_cast<uint64_t>(centerY + y) * rowPitch;

                for (uint32_t x = 0; x < sampleW; ++x) {
                    const auto* texelBase = rowBase + (centerX + x) * bytesPerTexel;

                    for (uint32_t c = 0; c < channels; ++c) {
                        const float val = ReadChannel(texelBase, c, format);

                        if (std::isnan(val)) {
                            result.hasNaN = true;
                        } else if (std::isinf(val)) {
                            result.hasInf = true;
                        } else {
                            if (val != 0.0f) {
                                result.isZero = false;
                            }
                            result.minVal = std::min(result.minVal, val);
                            result.maxVal = std::max(result.maxVal, val);
                            sum += static_cast<double>(val);
                            ++sampleCount;
                        }
                    }
                }
            }

            if (sampleCount > 0) {
                result.avgVal = static_cast<float>(sum / static_cast<double>(sampleCount));
            } else {
                result.minVal = 0.0f;
                result.maxVal = 0.0f;
                result.avgVal = 0.0f;
            }

            device->UnmapBuffer(readbackBuffer);
        } else {
            UZ_LOG(PassInspector, Warning,
                   "[PASS VALIDATE] {}: SKIPPED (failed to map readback buffer)",
                   target.name);
            result.minVal = 0.0f;
            result.maxVal = 0.0f;
            result.avgVal = 0.0f;
        }

        device->DestroyBuffer(readbackBuffer);
        results.push_back(std::move(result));
    }

    // Log results.
    for (const auto& r : results) {
        const char* status = "OK";
        if (r.hasNaN)       status = "FAIL — has NaN";
        else if (r.hasInf)  status = "FAIL — has Inf";
        else if (r.isZero)  status = "WARNING — all zeros";

        UZ_LOG(PassInspector, Info,
               "[PASS VALIDATE] {}: {} (min={:.4f}, max={:.4f}, avg={:.4f})",
               r.name, status, r.minVal, r.maxVal, r.avgVal);
    }

    return results;
}

std::vector<ValidationResult> PassInspector::ValidateByName(
    IRHIDevice* device, IRHICommandList* cmdList, const std::string& passName)
{
    if (!device || !cmdList || passName.empty()) {
        return {};
    }

    // Filter targets that match the pass name prefix (e.g., "ssgi" matches
    // "ssgi.trace", "ssgi.denoised", etc.) or exact name.
    std::vector<RegisteredTarget> filteredTargets;
    for (const auto& target : m_targets) {
        if (target.name == passName ||
            (target.name.size() > passName.size() &&
             target.name.compare(0, passName.size(), passName) == 0 &&
             target.name[passName.size()] == '.')) {
            filteredTargets.push_back(target);
        }
    }

    if (filteredTargets.empty()) {
        UZ_LOG(PassInspector, Warning,
               "[PASS VALIDATE] No targets found matching '{}'", passName);
        return {};
    }

    // Temporarily replace m_targets with the filtered set, validate, then restore.
    auto savedTargets = std::move(m_targets);
    m_targets = std::move(filteredTargets);
    auto results = Validate(device, cmdList);
    m_targets = std::move(savedTargets);

    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass Toggle (Phase 4)
// ─────────────────────────────────────────────────────────────────────────────

void PassInspector::TogglePass(const std::string& passName) {
    if (passName.empty()) return;

    auto it = m_disabledPasses.find(passName);
    if (it != m_disabledPasses.end()) {
        m_disabledPasses.erase(it);
        UZ_LOG(PassInspector, Info, "PassInspector: pass '{}' enabled", passName);
    } else {
        m_disabledPasses.insert(passName);
        UZ_LOG(PassInspector, Info, "PassInspector: pass '{}' disabled", passName);
    }
}

bool PassInspector::IsPassEnabled(const std::string& passName) const {
    return m_disabledPasses.find(passName) == m_disabledPasses.end();
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass Compare (Phase 4)
// ─────────────────────────────────────────────────────────────────────────────

void PassInspector::SetComparePass(const std::string& passName) {
    if (passName.empty()) {
        ClearCompare();
        return;
    }

    m_comparePassName = passName;
    m_comparePhase = 0;
    UZ_LOG(PassInspector, Info, "PassInspector: comparison mode for '{}' — "
           "next 2 frames will capture with/without", passName);
}

void PassInspector::ClearCompare() {
    if (!m_comparePassName.empty()) {
        UZ_LOG(PassInspector, Info, "PassInspector: comparison mode ended");
    }
    m_comparePassName.clear();
    m_comparePhase = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass Stats (Phase 2)
// ─────────────────────────────────────────────────────────────────────────────

void PassInspector::SetTimingData(const std::vector<PassTimingEntry>& entries) {
    m_timingData = entries;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass Dump (Phase 3)
// ─────────────────────────────────────────────────────────────────────────────

void PassInspector::RequestDump(uint32_t frame) {
    if (frame == UINT32_MAX) {
        m_dumpNextFrame = true;
        UZ_LOG(PassInspector, Info, "PassInspector: dump requested for next frame");
    } else {
        m_dumpFrame = frame;
        m_dumpNextFrame = false;
        UZ_LOG(PassInspector, Info, "PassInspector: dump scheduled for frame {}", frame);
    }
}

bool PassInspector::IsDumpPending(uint32_t currentFrame) const noexcept {
    if (m_dumpNextFrame) return true;
    return m_dumpFrame != UINT32_MAX && m_dumpFrame == currentFrame;
}

void PassInspector::ClearDumpRequest() {
    m_dumpFrame = UINT32_MAX;
    m_dumpNextFrame = false;
}

void PassInspector::ExecuteDump(IRHIDevice* device, IRHICommandList* cmdList,
                                 uint32_t frameNumber, const std::string& outputDir) {
    if (!device || !cmdList || m_targets.empty()) {
        UZ_LOG(PassInspector, Warning, "PassInspector: dump failed — no targets registered");
        return;
    }

    // Create the output directory: outputDir/pass-dump-f{N}/
    std::ostringstream dirStream;
    dirStream << outputDir << "/pass-dump-f" << frameNumber;
    const std::string dumpDir = dirStream.str();

    std::error_code ec;
    std::filesystem::create_directories(dumpDir, ec);
    if (ec) {
        UZ_LOG(PassInspector, Error, "PassInspector: failed to create directory '{}': {}",
               dumpDir, ec.message());
        return;
    }

    UZ_LOG(PassInspector, Info, "PassInspector: dumping {} targets to '{}'",
           m_targets.size(), dumpDir);

    // Create a single shared readback buffer large enough for the biggest target.
    // Reusing one buffer avoids VRAM exhaustion from rapid sequential allocations.
    constexpr uint64_t kMaxReadbackSize = 1920u * 1080u * 8u; // RGBA16F @ 1080p = ~16MB
    RHIBufferDesc rbDesc{};
    rbDesc.sizeBytes = kMaxReadbackSize;
    rbDesc.stride    = 1;
    rbDesc.usage     = RHIBufferUsage::CopyDst;
    rbDesc.memory    = RHIMemoryType::Readback;
    rbDesc.debugName = "PassInspector_SharedReadback";
    RHIBufferHandle sharedReadback = device->CreateBuffer(rbDesc);
    if (!sharedReadback.IsValid()) {
        UZ_LOG(PassInspector, Error, "PassInspector: failed to create shared readback buffer");
        return;
    }

    std::vector<std::string> filenames;
    filenames.reserve(m_targets.size());

    for (uint32_t i = 0; i < static_cast<uint32_t>(m_targets.size()); ++i) {
        const auto& target = m_targets[i];

        // Build filename: XX-name.png (dots replaced with dashes)
        std::ostringstream fnStream;
        fnStream << std::setfill('0') << std::setw(2) << i << "-";
        std::string safeName = target.name;
        std::replace(safeName.begin(), safeName.end(), '.', '-');
        fnStream << safeName << ".png";
        const std::string filename = fnStream.str();

        // Readback the texture to RGBA8 using the shared buffer.
        auto pixels = ReadbackTextureToRGBA8(device, cmdList, target, sharedReadback, kMaxReadbackSize);
        if (pixels.empty()) {
            const RHITextureDesc td = device->GetTextureDesc(target.texture);
            UZ_LOG(PassInspector, Warning,
                   "PassInspector: dump skipped '{}' (readback failed — fmt={}, {}x{}, depth={}, bpp={}, ch={})",
                   target.name, static_cast<int>(td.format), target.width, target.height,
                   IsDepthFormat(td.format), BytesPerTexel(td.format), ChannelCount(td.format));
            continue;
        }

        // Write PNG.
        const std::string fullPath = dumpDir + "/" + filename;
        const int writeResult = stbi_write_png(
            fullPath.c_str(),
            static_cast<int>(target.width),
            static_cast<int>(target.height),
            4,
            pixels.data(),
            static_cast<int>(target.width * 4));

        if (writeResult) {
            filenames.push_back(filename);
            UZ_LOG(PassInspector, Info, "PassInspector: dumped {} ({}x{})",
                   filename, target.width, target.height);
        } else {
            UZ_LOG(PassInspector, Warning, "PassInspector: failed to write '{}'", fullPath);
        }
    }

    // Release the shared readback buffer.
    device->DestroyBuffer(sharedReadback);

    // Generate INDEX.html for browser-based viewing.
    GenerateIndexHtml(dumpDir, filenames, frameNumber);

    UZ_LOG(PassInspector, Info, "PassInspector: dump complete — {} PNGs + INDEX.html in '{}'",
           filenames.size(), dumpDir);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass Grid (Phase 5)
// ─────────────────────────────────────────────────────────────────────────────

std::vector<const RegisteredTarget*> PassInspector::GetGridTargets() const {
    std::vector<const RegisteredTarget*> gridTargets;
    const uint32_t maxCells = GetGridCellCount();
    gridTargets.reserve(std::min(maxCells, static_cast<uint32_t>(m_targets.size())));

    for (uint32_t i = 0; i < static_cast<uint32_t>(m_targets.size()) && i < maxCells; ++i) {
        gridTargets.push_back(&m_targets[i]);
    }

    return gridTargets;
}

} // namespace uz
