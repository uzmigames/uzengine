/// VFX GPU Readback Framework implementation -- ring-buffered GPU→CPU staging.

#include "vfx_readback.h"

#include "engine/logging/log.h"
#include "engine/rhi/rhi.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Create / Destroy
// ─────────────────────────────────────────────────────────────────────────────

bool VFXReadbackManager::Create(IRHIDevice* device,
                                 uint32_t maxRequestsPerFrame,
                                 uint32_t ringSize,
                                 uint32_t stagingCapacityBytes) {
    if (!device || ringSize == 0 || stagingCapacityBytes == 0) {
        UZ_LOG(VFX, Error,
               "VFXReadbackManager::Create: invalid parameters "
               "(device={}, ringSize={}, capacity={})",
               static_cast<const void*>(device), ringSize, stagingCapacityBytes);
        return false;
    }

    ringSize_ = ringSize;
    maxRequestsPerFrame_ = maxRequestsPerFrame;
    currentSlot_ = 0;
    frameIndex_ = 0;

    ring_.resize(ringSize);

    for (uint32_t i = 0; i < ringSize; ++i) {
        ring_[i].capacityBytes = stagingCapacityBytes;
        ring_[i].usedBytes = 0;
        ring_[i].fenceValue = 0;
        ring_[i].requests.reserve(maxRequestsPerFrame);

        // Allocate READBACK-heap staging buffer.
        RHIBufferDesc stagingDesc;
        stagingDesc.sizeBytes = stagingCapacityBytes;
        stagingDesc.stride    = 1;
        stagingDesc.usage     = RHIBufferUsage::CopyDst;
        stagingDesc.memory    = RHIMemoryType::Readback;
        stagingDesc.debugName = "VFXReadbackStaging";
        ring_[i].buffer = device->CreateBuffer(stagingDesc);

        if (!ring_[i].buffer.IsValid()) {
            UZ_LOG(VFX, Error,
                   "VFXReadbackManager::Create: failed to allocate staging buffer "
                   "slot {} ({} bytes)",
                   i, stagingCapacityBytes);
            // Clean up partially allocated slots.
            for (uint32_t j = 0; j < i; ++j) {
                device->DestroyBuffer(ring_[j].buffer);
                ring_[j].buffer = RHIBufferHandle::Invalid();
            }
            ring_.clear();
            return false;
        }
    }

    UZ_LOG(VFX, Info,
           "VFXReadbackManager created: ringSize={}, staging={}KB/slot, "
           "maxRequests={}/frame",
           ringSize, stagingCapacityBytes / 1024, maxRequestsPerFrame);
    return true;
}

void VFXReadbackManager::Destroy(IRHIDevice* device) {
    if (!device) { return; }

    for (auto& frame : ring_) {
        if (frame.buffer.IsValid()) {
            device->DestroyBuffer(frame.buffer);
            frame.buffer = RHIBufferHandle::Invalid();
        }
        frame.requests.clear();
        frame.usedBytes = 0;
        frame.fenceValue = 0;
    }
    ring_.clear();
    ringSize_ = 0;
    currentSlot_ = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// BeginFrame
// ─────────────────────────────────────────────────────────────────────────────

void VFXReadbackManager::BeginFrame(uint64_t frameIndex) {
    frameIndex_ = frameIndex;

    if (ring_.empty()) { return; }

    // Advance to the current ring slot (wrapping).
    currentSlot_ = static_cast<uint32_t>(frameIndex % ringSize_);

    // Reset the current slot for new requests.
    auto& slot = ring_[currentSlot_];
    slot.usedBytes = 0;
    slot.requests.clear();
    slot.fenceValue = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// RequestReadback
// ─────────────────────────────────────────────────────────────────────────────

bool VFXReadbackManager::RequestReadback(
    RHIBufferHandle src,
    uint32_t byteOffset,
    uint32_t byteSize,
    std::function<void(const void* data, uint32_t size)> callback) {

    if (ring_.empty()) {
        UZ_LOG(VFX, Warning, "VFXReadbackManager::RequestReadback: not created");
        return false;
    }

    auto& slot = ring_[currentSlot_];

    // Check per-frame request limit.
    if (slot.requests.size() >= maxRequestsPerFrame_) {
        UZ_LOG(VFX, Warning,
               "VFXReadbackManager: per-frame request limit ({}) exceeded, "
               "dropping readback",
               maxRequestsPerFrame_);
        return false;
    }

    // Check staging capacity.
    if (slot.usedBytes + byteSize > slot.capacityBytes) {
        UZ_LOG(VFX, Warning,
               "VFXReadbackManager: staging buffer full ({}/{} bytes), "
               "dropping readback of {} bytes",
               slot.usedBytes, slot.capacityBytes, byteSize);
        return false;
    }

    ReadbackRequest req;
    req.src = src;
    req.offset = byteOffset;
    req.size = byteSize;
    req.stagingOffset = slot.usedBytes;
    req.callback = std::move(callback);

    slot.usedBytes += byteSize;
    slot.requests.push_back(std::move(req));

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// EndFrame
// ─────────────────────────────────────────────────────────────────────────────

void VFXReadbackManager::EndFrame(IRHICommandList* cmd, uint64_t fenceValue) {
    if (ring_.empty() || !cmd) { return; }

    auto& slot = ring_[currentSlot_];
    slot.fenceValue = fenceValue;

    // Issue CopyBufferRegion for each request in this frame's batch.
    for (const auto& req : slot.requests) {
        cmd->CopyBuffer(
            req.src,              // src (GPU buffer)
            req.offset,           // src offset
            slot.buffer,          // dst (readback staging)
            req.stagingOffset,    // dst offset
            req.size
        );
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ProcessCallbacks
// ─────────────────────────────────────────────────────────────────────────────

void VFXReadbackManager::ProcessCallbacks(IRHIDevice* device,
                                           uint64_t completedFenceValue) {
    if (ring_.empty() || !device) { return; }

    // Only process if we have accumulated enough frames for the ring to wrap.
    if (frameIndex_ < ringSize_) { return; }

    // The oldest completed slot is `ringSize_` frames behind the current one.
    const uint32_t oldestSlot =
        static_cast<uint32_t>((frameIndex_ - ringSize_) % ringSize_);

    auto& slot = ring_[oldestSlot];

    // Non-blocking: skip if GPU hasn't finished this slot's copies yet.
    if (slot.fenceValue == 0 || completedFenceValue < slot.fenceValue) {
        return;
    }

    if (slot.requests.empty()) { return; }

    // Map the staging buffer and invoke all callbacks.
    void* mapped = device->MapBuffer(slot.buffer);
    if (!mapped) {
        UZ_LOG(VFX, Warning,
               "VFXReadbackManager::ProcessCallbacks: failed to map staging "
               "buffer slot {}",
               oldestSlot);
        slot.requests.clear();
        return;
    }

    const auto* base = static_cast<const uint8_t*>(mapped);
    for (const auto& req : slot.requests) {
        if (req.callback) {
            req.callback(base + req.stagingOffset, req.size);
        }
    }

    device->UnmapBuffer(slot.buffer);
    slot.requests.clear();
}

} // namespace uz
