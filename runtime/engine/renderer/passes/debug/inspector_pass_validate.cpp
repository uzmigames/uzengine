/// UzEngine Pass Inspector — Validation implementation.
///
/// GPU readback-based anomaly detection: allocates per-target readback buffers,
/// copies a 4x4 texel sample from each target's center, and checks for
/// NaN, Inf, all-zero, min/max/avg statistics.

#include "inspector_pass.h"
#include "inspector_pass_utils.h"

#include "rhi.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

UZ_DECLARE_LOG_CATEGORY(PassInspector);

namespace uz {

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
        const uint32_t channels      = detail::ChannelCount(format);

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

        if (detail::IsDepthFormat(format)) {
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
                        const float val = detail::ReadChannel(texelBase, c, format);

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

} // namespace uz
