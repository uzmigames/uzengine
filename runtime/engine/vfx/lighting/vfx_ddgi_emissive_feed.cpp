#include "vfx_ddgi_emissive_feed.h"

#include "engine/rhi/rhi.h"
#include "engine/logging/log.h"

#include <algorithm>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

VfxDDGIEmissiveFeed::~VfxDDGIEmissiveFeed() {
    Shutdown();
}

bool VfxDDGIEmissiveFeed::Init(IRHIDevice* device) {
    if (device == nullptr) return false;

    RHIBufferDesc desc{};
    desc.sizeBytes = sizeof(VfxEmissiveSample) * kMaxSamples;
    desc.stride    = sizeof(VfxEmissiveSample);
    desc.usage     = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::CopyDst;
    desc.memory    = RHIMemoryType::Upload; // CPU-visible for direct memcpy
    desc.debugName = "VfxDDGIEmissiveSamples";

    buffer_ = device->CreateBuffer(desc);
    if (!buffer_.IsValid()) {
        UZ_LOG(VFX, Error, "VfxDDGIEmissiveFeed: buffer allocation failed");
        return false;
    }

    srvIndex_ = device->GetBufferSRVIndex(buffer_);
    device_   = device;

    UZ_LOG(VFX, Info,
           "VfxDDGIEmissiveFeed: initialised ({} max samples, SRV={})",
           kMaxSamples, srvIndex_);
    return true;
}

void VfxDDGIEmissiveFeed::Shutdown() {
    if (device_ != nullptr && buffer_.IsValid()) {
        device_->DestroyBuffer(buffer_);
    }
    device_      = nullptr;
    buffer_      = {};
    srvIndex_    = UINT32_MAX;
    sampleCount_ = 0;
}

void VfxDDGIEmissiveFeed::UploadFrame(const VfxEmissiveSample* rawSamples,
                                        uint32_t rawCount) {
    if (!IsInitialized() || !enabled) {
        sampleCount_ = 0;
        return;
    }

    // Select top-N by luminance.
    sampleCount_ = VfxDDGISelectTopN(rawSamples, rawCount, staging_, kMaxSamples);

    if (sampleCount_ == 0) return;

    // Upload to GPU via mapped pointer.
    void* mapped = device_->MapBuffer(buffer_);
    if (mapped != nullptr) {
        std::memcpy(mapped, staging_,
                    sizeof(VfxEmissiveSample) * sampleCount_);
        device_->UnmapBuffer(buffer_);
    } else {
        UZ_LOG(VFX, Warning,
               "VfxDDGIEmissiveFeed: MapBuffer failed, skipping upload");
        sampleCount_ = 0;
    }
}

void VfxDDGIEmissiveFeed::Reset() noexcept {
    sampleCount_ = 0;
}

} // namespace uz
