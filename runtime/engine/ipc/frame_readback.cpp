/// Triple-buffered GPU-to-CPU frame readback implementation.
///
/// Follows the same RHI pattern as ScreenshotCapture (screenshot_capture.cpp):
/// - Readback buffers with RHIMemoryType::Readback + RHIBufferUsage::CopyDst
/// - 256-byte row pitch alignment for DX12 compatibility
/// - Present -> CopySrc -> Present barrier transitions for post-Present capture
/// - MapBuffer/UnmapBuffer for CPU access to completed readback data
///
/// The triple-buffer ring adds per-buffer fences to avoid WaitIdle() stalls.

#include "frame_readback.h"

#include "rhi.h"
#include "log.h"
#include "profiler.h"

namespace uz {

UZ_DEFINE_LOG_CATEGORY(FrameReadback, Info);

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// All backends accept 256-byte aligned row pitch. DX12 requires it;
/// DX11 and Vulkan are fine with any alignment but 256 avoids issues.
static constexpr uint32_t kTexturePitchAlignment = 256;

/// Aligns a value up to the given power-of-two alignment.
static uint32_t AlignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Impl — hidden state for the triple-buffered ring
// ─────────────────────────────────────────────────────────────────────────────

struct FrameReadback::Impl {
    IRHIDevice* device = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t rowPitch = 0;

    /// Readback buffers — one per ring slot.
    RHIBufferHandle buffers[kBufferCount] = {};

    /// Per-buffer fences — signaled after the GPU copy for that slot completes.
    IRHIFence* fences[kBufferCount] = {};

    /// Monotonically increasing fence values, one per buffer slot.
    /// A value of 0 means the slot has never been written.
    uint64_t fenceValues[kBufferCount] = {};

    /// Global fence counter. Incremented each time a new capture is signaled.
    uint64_t nextFenceValue = 1;

    /// Write index: which buffer slot the GPU will copy into next.
    uint32_t writeIndex = 0;

    /// Read index: which buffer slot the CPU will read from next.
    uint32_t readIndex = 0;

    /// Total number of captures submitted. Used to detect the pipeline
    /// fill period (first kBufferCount-1 frames have no readable data).
    uint64_t captureCount = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

FrameReadback::FrameReadback() = default;

FrameReadback::~FrameReadback() {
    Shutdown();
}

bool FrameReadback::Init(IRHIDevice* device, uint32_t width, uint32_t height) {
    UZ_PROFILE_SCOPE(FrameReadback_Init);

    if (!device || width == 0 || height == 0) {
        UZ_LOG(FrameReadback, Error, "Init: invalid arguments (device={}, {}x{})",
               static_cast<const void*>(device), width, height);
        return false;
    }

    // Clean up any prior state.
    Shutdown();

    auto* impl = new Impl();
    impl->device = device;
    impl->width = width;
    impl->height = height;
    impl->rowPitch = AlignUp(width * 4, kTexturePitchAlignment);

    const uint64_t bufferSize = static_cast<uint64_t>(impl->rowPitch) * height;

    // Create kBufferCount readback buffers.
    for (uint32_t i = 0; i < kBufferCount; ++i) {
        RHIBufferDesc desc{};
        desc.sizeBytes = bufferSize;
        desc.memory = RHIMemoryType::Readback;
        desc.usage = RHIBufferUsage::CopyDst;

        // Debug names help identify buffers in PIX/RenderDoc captures.
        // Using a static array avoids dynamic allocation for debug strings.
        static const char* kDebugNames[kBufferCount] = {
            "FrameReadback_0",
            "FrameReadback_1",
            "FrameReadback_2",
        };
        desc.debugName = kDebugNames[i];

        impl->buffers[i] = device->CreateBuffer(desc);
        if (!impl->buffers[i].IsValid()) {
            UZ_LOG(FrameReadback, Error, "Init: failed to create readback buffer {} ({}x{}, {} bytes)",
                   i, width, height, bufferSize);

            // Clean up any buffers created so far.
            for (uint32_t j = 0; j < i; ++j) {
                device->DestroyBuffer(impl->buffers[j]);
            }
            delete impl;
            return false;
        }
    }

    // Create kBufferCount fences, one per buffer slot.
    for (uint32_t i = 0; i < kBufferCount; ++i) {
        impl->fences[i] = device->CreateFence();
        if (!impl->fences[i]) {
            UZ_LOG(FrameReadback, Error, "Init: failed to create fence {}", i);

            // Clean up fences created so far.
            for (uint32_t j = 0; j < i; ++j) {
                device->DestroyFence(impl->fences[j]);
            }
            // Clean up all buffers.
            for (uint32_t j = 0; j < kBufferCount; ++j) {
                device->DestroyBuffer(impl->buffers[j]);
            }
            delete impl;
            return false;
        }
    }

    impl_ = impl;

    UZ_LOG(FrameReadback, Info, "Initialized: {}x{}, rowPitch={}, bufferSize={}, bufferCount={}",
           width, height, impl->rowPitch, bufferSize, kBufferCount);
    return true;
}

void FrameReadback::Shutdown() {
    if (!impl_) return;

    auto* device = impl_->device;

    // Destroy fences first (they reference the GPU timeline).
    for (uint32_t i = 0; i < kBufferCount; ++i) {
        if (impl_->fences[i]) {
            device->DestroyFence(impl_->fences[i]);
            impl_->fences[i] = nullptr;
        }
    }

    // Destroy readback buffers.
    for (uint32_t i = 0; i < kBufferCount; ++i) {
        if (impl_->buffers[i].IsValid()) {
            device->DestroyBuffer(impl_->buffers[i]);
        }
    }

    UZ_LOG(FrameReadback, Info, "Shutdown: released {} buffers + {} fences",
           kBufferCount, kBufferCount);

    delete impl_;
    impl_ = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Capture — record GPU copy commands for the current write buffer
// ─────────────────────────────────────────────────────────────────────────────

void FrameReadback::CaptureFrame(IRHICommandList* cmdList, IRHISwapChain* swapChain) {
    UZ_PROFILE_SCOPE(FrameReadback_Capture);

    if (!impl_ || !cmdList || !swapChain) return;

    const uint32_t slot = impl_->writeIndex;
    RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();

    // Transition: Present -> CopySrc
    RHIBarrierDesc barrier{};
    barrier.texture = backBuffer;
    barrier.stateBefore = RHIResourceState::Present;
    barrier.stateAfter = RHIResourceState::CopySrc;
    cmdList->ResourceBarrier(&barrier, 1);

    // Copy the backbuffer into the current write slot's readback buffer.
    cmdList->CopyTextureToBuffer(backBuffer, 0, impl_->buffers[slot], 0, impl_->rowPitch);

    // Transition: CopySrc -> Present
    barrier.stateBefore = RHIResourceState::CopySrc;
    barrier.stateAfter = RHIResourceState::Present;
    cmdList->ResourceBarrier(&barrier, 1);

    // Advance write index for the next frame.
    impl_->writeIndex = (slot + 1) % kBufferCount;
    impl_->captureCount++;
}

// ─────────────────────────────────────────────────────────────────────────────
// Fence signaling — call AFTER cmdList->Execute()
// ─────────────────────────────────────────────────────────────────────────────

void FrameReadback::SignalFence() {
    if (!impl_) return;

    // The write index was already advanced in CaptureFrame(), so the buffer
    // we just wrote is at (writeIndex - 1 + kBufferCount) % kBufferCount.
    const uint32_t slot = (impl_->writeIndex + kBufferCount - 1) % kBufferCount;
    const uint64_t value = impl_->nextFenceValue++;

    impl_->fenceValues[slot] = value;
    impl_->fences[slot]->Signal(value);
}

// ─────────────────────────────────────────────────────────────────────────────
// Read — check readiness and map the oldest completed buffer
// ─────────────────────────────────────────────────────────────────────────────

bool FrameReadback::IsReadReady() const {
    if (!impl_) return false;

    // During the pipeline fill period, there is no buffer old enough to read.
    // We need at least kBufferCount captures before the read slot has valid data.
    // However, since write and read start at 0 and write advances first, we need
    // the read slot to have been written AND its fence to be signaled.
    // A simpler check: we need at least kBufferCount captures submitted.
    if (impl_->captureCount < kBufferCount) {
        return false;
    }

    const uint32_t slot = impl_->readIndex;
    const uint64_t expected = impl_->fenceValues[slot];

    // A fence value of 0 means this slot was never written to.
    if (expected == 0) return false;

    return impl_->fences[slot]->IsComplete(expected);
}

bool FrameReadback::MapRead(const uint8_t** outData, uint32_t* outStride) const {
    if (!impl_ || !outData || !outStride) return false;

    const uint32_t slot = impl_->readIndex;
    void* mapped = impl_->device->MapBuffer(impl_->buffers[slot]);
    if (!mapped) {
        UZ_LOG(FrameReadback, Error, "MapRead: failed to map buffer slot {}", slot);
        return false;
    }

    *outData = static_cast<const uint8_t*>(mapped);
    *outStride = impl_->rowPitch;
    return true;
}

void FrameReadback::UnmapRead() const {
    if (!impl_) return;

    const uint32_t slot = impl_->readIndex;
    impl_->device->UnmapBuffer(impl_->buffers[slot]);
}

void FrameReadback::AdvanceRead() {
    if (!impl_) return;
    impl_->readIndex = (impl_->readIndex + 1) % kBufferCount;
}

// ─────────────────────────────────────────────────────────────────────────────
// Queries
// ─────────────────────────────────────────────────────────────────────────────

bool FrameReadback::IsInitialized() const {
    return impl_ != nullptr;
}

uint32_t FrameReadback::GetWidth() const {
    return impl_ ? impl_->width : 0;
}

uint32_t FrameReadback::GetHeight() const {
    return impl_ ? impl_->height : 0;
}

} // namespace uz
