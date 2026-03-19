/// UzEngine Pass Inspector — Implementation.
///
/// Manages per-frame target registration, visualization target lookup,
/// and readback-based validation for the render pass inspector system.

#include "inspector_pass.h"

#include "rhi.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

UZ_DEFINE_LOG_CATEGORY(PassInspector, Trace);

namespace uz {

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
                                    VisDisplayMode mode, uint32_t width, uint32_t height) {
    if (!name || !texture.IsValid() || srvIndex == UINT32_MAX) {
        return;
    }

    RegisteredTarget target;
    target.name     = name;
    target.texture  = texture;
    target.srvIndex = srvIndex;
    target.mode     = mode;
    target.width    = width;
    target.height   = height;

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

    // For each target, copy the full mip 0 to a readback buffer, then sample
    // a 4x4 block from the center. Processing one target at a time avoids
    // allocating a huge combined buffer.
    //
    // The RHI CopyTextureToBuffer API copies an entire mip level:
    //   CopyTextureToBuffer(srcTexture, mipLevel, dstBuffer, dstOffset, dstRowPitch, arraySlice)
    //
    // Row pitch must be 256-byte aligned for D3D12.

    static constexpr uint32_t kSampleDim = 4;
    // Assume worst case 16 bytes per texel (RGBA32F) for buffer sizing.
    // The actual format varies, but the readback copies raw bytes.
    static constexpr uint32_t kMaxBytesPerTexel = 16;

    for (const auto& target : m_targets) {
        ValidationResult result;
        result.name    = target.name;
        result.isZero  = true;
        result.hasNaN  = false;
        result.hasInf  = false;
        result.minVal  = 1e30f;
        result.maxVal  = -1e30f;
        result.avgVal  = 0.0f;

        // Compute row pitch for the full texture width, 256-byte aligned.
        const uint32_t bytesPerTexel = kMaxBytesPerTexel; // Conservative estimate
        const uint32_t rowPitch = ((target.width * bytesPerTexel + 255) & ~255u);
        const uint64_t totalBytes = static_cast<uint64_t>(rowPitch) * target.height;

        // Skip very large textures to avoid huge allocations during validation.
        // Shadow atlases and large textures > 16MB are skipped.
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

        // Transition to CopySrc, copy mip 0, transition back.
        {
            RHIBarrierDesc barrier{};
            barrier.texture     = target.texture;
            barrier.stateBefore = RHIResourceState::ShaderResource;
            barrier.stateAfter  = RHIResourceState::CopySrc;
            cmdList->ResourceBarrier(&barrier, 1);

            cmdList->CopyTextureToBuffer(target.texture, 0,
                                          readbackBuffer, 0, rowPitch, 0);

            barrier.stateBefore = RHIResourceState::CopySrc;
            barrier.stateAfter  = RHIResourceState::ShaderResource;
            cmdList->ResourceBarrier(&barrier, 1);
        }

        // Execute and wait for GPU.
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
                const auto* rowBase = static_cast<const uint8_t*>(mapped) + (centerY + y) * rowPitch;
                // Read as float values starting at the center X offset.
                // For RGBA16F textures the bytes are half-floats; for RGBA8 they're bytes.
                // We read as raw floats from the start — the layout depends on format.
                // For simplicity, reinterpret as floats (correct for R32F, RGBA32F;
                // approximate for RGBA16F/RGBA8 since we're just checking for zeros/NaN).
                const auto* texelPtr = reinterpret_cast<const float*>(rowBase + centerX * bytesPerTexel);
                const uint32_t floatsPerTexel = bytesPerTexel / sizeof(float);

                for (uint32_t x = 0; x < sampleW; ++x) {
                    for (uint32_t c = 0; c < floatsPerTexel; ++c) {
                        const float val = texelPtr[x * floatsPerTexel + c];

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
