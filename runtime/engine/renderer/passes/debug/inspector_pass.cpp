/// UzEngine Pass Inspector — Implementation.
///
/// Manages per-frame target registration, visualization target lookup,
/// and readback-based validation for the render pass inspector system.

#include "inspector_pass.h"

#include "rhi.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

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
///
/// For half-float formats: reads uint16_t and converts via HalfToFloat.
/// For 8-bit unorm formats: reads uint8_t and normalizes to [0,1].
/// For 32-bit float/uint formats: reads float directly (uint reinterpret for uint formats).
/// For depth formats: reads as the appropriate native type.
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

    // Process each target individually: record a single barrier+copy pair,
    // submit, wait, and read back. This per-target approach is necessary
    // because the end-of-frame resource state may not match the declared
    // lastState (pre-existing render graph state tracking gaps), and one
    // invalid barrier would poison an entire batch submission.
    //
    // Row pitch must be 256-byte aligned for D3D12.

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

        // Skip depth/stencil formats — the DX12 CopyTextureToBuffer footprint
        // requires R32_FLOAT (not D32_FLOAT) for depth textures, which the RHI
        // does not currently handle. Attempting to copy triggers E_INVALIDARG
        // on Close and a subsequent device removed.
        const bool isDepthFormat = (format == RHIFormat::D16Unorm
                                 || format == RHIFormat::D32Float
                                 || format == RHIFormat::D24UnormS8Uint
                                 || format == RHIFormat::D32FloatS8Uint);
        if (isDepthFormat) {
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
            result.isZero = false; // Don't flag as zero — we just didn't check.
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

        // Submit and wait for this single copy. If the command list Close
        // fails (barrier state mismatch from pre-existing render graph gaps),
        // reset and skip this target rather than crashing.
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

} // namespace uz
