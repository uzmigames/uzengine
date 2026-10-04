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

        // Query the actual texture descriptor from the RHI. We need the full
        // descriptor (not just format) so we can correctly sample every face
        // of a cubemap / every slice of a texture array. A previous version
        // of this validator copied only subresource 0 for every target, which
        // produced false-positive WARN_ZERO reports for cubemaps whose face 0
        // (+X) happened to be dim relative to the other faces (e.g., a sky
        // cubemap where +X looks toward the horizon while +Z looks at the
        // sun). The fix is to iterate all array slices and union the stats.
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
            result.isZero = false;
            result.skipped = true;
            result.skipReason = "unsupported format";
            results.push_back(std::move(result));
            continue;
        }

        if (detail::IsDepthFormat(format)) {
            result.minVal = 0.0f;
            result.maxVal = 0.0f;
            result.avgVal = 0.0f;
            result.isZero = false; // Don't flag as zero — depth not validated.
            result.skipped = true;
            result.skipReason = "depth format — readback not supported";
            UZ_LOG(PassInspector, Info,
                   "[PASS VALIDATE] {}: SKIPPED (depth format — readback not supported)",
                   target.name);
            results.push_back(std::move(result));
            continue;
        }

        // 3D volume textures are read back one Z slice at a time — see the
        // slice selection below. They used to be skipped outright (reporting
        // 0/0/0, which read as "the volume is empty"), because
        // CopyTextureToBuffer could only reach Z-slice 0. It now takes the
        // slice through its `arraySlice` parameter.

        // A target registered as ONE channel of a packed target must be measured on that channel.
        // Without this, "gbuffer.ao" (the albedo target's alpha) reported the albedo target's RGBA
        // statistics, and "gbuffer.metallic"/"gbuffer.roughness" reported the normal target's.
        uint32_t channelFirst = 0;
        uint32_t channelCount = channels;
        switch (target.dumpChannel) {
            case DumpChannel::R: channelFirst = 0; channelCount = 1; result.channel = "R"; break;
            case DumpChannel::G: channelFirst = 1; channelCount = 1; result.channel = "G"; break;
            case DumpChannel::B: channelFirst = 2; channelCount = 1; result.channel = "B"; break;
            case DumpChannel::A: channelFirst = 3; channelCount = 1; result.channel = "A"; break;
            case DumpChannel::All: break;
        }
        if (channelFirst + channelCount > channels) {
            UZ_LOG(PassInspector, Warning,
                   "[PASS VALIDATE] {}: SKIPPED (channel {} but the format has {} channels)",
                   target.name, result.channel, channels);
            result.minVal = 0.0f;
            result.maxVal = 0.0f;
            result.avgVal = 0.0f;
            result.isZero = false;
            result.skipped = true;
            result.skipReason = "registered channel is outside the format";
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
            result.skipped = true;
            result.skipReason = "texture over the readback size cap";
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
            result.isZero = false;
            result.skipped = true;
            result.skipReason = "readback buffer could not be created";
            results.push_back(std::move(result));
            continue;
        }

        // Determine how many slices to sample. For a cubemap this is 6 (one
        // readback per face). For a texture array it is the array size. For a
        // plain 2D texture it is 1.
        //
        // A volume is sampled at ONE slice — its centre. Slice 0 of a global
        // distance field clipmap is its boundary plane, which is uniformly far
        // from geometry and reads as a constant; the centre slice cuts through
        // the scene and carries the signed distances the pass actually writes.
        const uint32_t arraySlices = (!texDesc.is3D)
            ? std::max<uint32_t>(1u, static_cast<uint32_t>(texDesc.depthOrArraySize))
            : 1u;
        const uint32_t sliceBase = texDesc.is3D
            ? std::max<uint32_t>(1u, static_cast<uint32_t>(texDesc.depthOrArraySize)) / 2u
            : 0u;

        uint32_t    sampleCountTotal = 0;
        double      sumTotal         = 0.0;
        bool        anyReadbackOk    = false;

        for (uint32_t slice = 0; slice < arraySlices; ++slice) {
            // Record barrier + copy + restore barrier for this slice.
            {
                RHIBarrierDesc barrier{};
                barrier.texture     = target.texture;
                barrier.stateBefore = target.lastState;
                barrier.stateAfter  = RHIResourceState::CopySrc;
                cmdList->ResourceBarrier(&barrier, 1);

                cmdList->CopyTextureToBuffer(target.texture, /*mip*/0,
                                              readbackBuffer, /*dstOffset*/0,
                                              rowPitch,
                                              /*arraySlice (Z slice for a volume)*/
                                              sliceBase + slice);

                barrier.stateBefore = RHIResourceState::CopySrc;
                barrier.stateAfter  = target.lastState;
                cmdList->ResourceBarrier(&barrier, 1);
            }

            cmdList->End();
            cmdList->Execute();
            device->WaitIdle();
            cmdList->Begin();

            // Map and sample a 4x4 block from the center of this slice.
            void* mapped = device->MapBuffer(readbackBuffer);
            if (!mapped) {
                UZ_LOG(PassInspector, Warning,
                       "[PASS VALIDATE] {}: slice {} — failed to map readback buffer",
                       target.name, slice);
                continue;
            }

            anyReadbackOk = true;

            const uint32_t centerX = (target.width  > kSampleDim) ? (target.width  / 2 - kSampleDim / 2) : 0;
            const uint32_t centerY = (target.height > kSampleDim) ? (target.height / 2 - kSampleDim / 2) : 0;
            const uint32_t sampleW = std::min(kSampleDim, target.width);
            const uint32_t sampleH = std::min(kSampleDim, target.height);

            for (uint32_t y = 0; y < sampleH; ++y) {
                const auto* rowBase = static_cast<const uint8_t*>(mapped)
                                    + static_cast<uint64_t>(centerY + y) * rowPitch;

                for (uint32_t x = 0; x < sampleW; ++x) {
                    const auto* texelBase = rowBase + (centerX + x) * bytesPerTexel;

                    for (uint32_t ci = 0; ci < channelCount; ++ci) {
                        const uint32_t c = channelFirst + ci;
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
                            sumTotal += static_cast<double>(val);
                            ++sampleCountTotal;
                        }
                    }
                }
            }

            device->UnmapBuffer(readbackBuffer);
        }

        if (!anyReadbackOk) {
            result.minVal = 0.0f;
            result.maxVal = 0.0f;
            result.avgVal = 0.0f;
            result.isZero = false; // Cannot confirm — don't warn.
            result.skipped = true;
            result.skipReason = "readback buffer could not be mapped";
        } else if (sampleCountTotal > 0) {
            result.avgVal = static_cast<float>(sumTotal / static_cast<double>(sampleCountTotal));
        } else {
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
        if (r.skipped)      status = "SKIPPED — not measured";
        else if (r.hasNaN)  status = "FAIL — has NaN";
        else if (r.hasInf)  status = "FAIL — has Inf";
        else if (r.isZero)  status = "WARNING — all zeros";

        if (r.skipped) {
            UZ_LOG(PassInspector, Info, "[PASS VALIDATE] {}: {} ({})",
                   r.name, status, r.skipReason);
        } else {
            UZ_LOG(PassInspector, Info,
                   "[PASS VALIDATE] {}: {} (channel={}, min={:.4f}, max={:.4f}, avg={:.4f})",
                   r.name, status, r.channel, r.minVal, r.maxVal, r.avgVal);
        }
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
