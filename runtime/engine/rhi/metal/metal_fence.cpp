/// Metal Fence Implementation
///
/// Uses MTLSharedEvent for native timeline fence support.
/// Signal enqueues a lightweight command buffer that signals the event after
/// all previously submitted GPU work completes (matching DX12 queue semantics).
/// WaitCPU uses MTLSharedEventListener + dispatch_semaphore for blocking.
/// IsComplete/GetCompletedValue poll the event's signaledValue property.

#include "metal_fence.h"
#include "metal_device.h"
#include "metal_resources.h"

#include "log.h"
#include "profiler.h"

#if defined(__APPLE__)
#include <Metal/Metal.h>
#include <dispatch/dispatch.h>
#endif

namespace uz {

UZ_DEFINE_LOG_CATEGORY(MetalRHI, Trace);

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

MetalFence::~MetalFence() {
#if defined(__APPLE__)
    if (m_event) {
        [(__bridge id<MTLSharedEvent>)m_event release];
        m_event = nullptr;
    }
#endif
}

bool MetalFence::Init(MetalDevice* device) {
    UZ_PROFILE_FUNCTION();
    if (!device) {
        UZ_LOG(MetalRHI, Error, "MetalFence::Init -- null device");
        return false;
    }

    m_device = device;

#if defined(__APPLE__)
    id<MTLDevice> mtlDevice = (__bridge id<MTLDevice>)device->GetMTLDevice();
    id<MTLSharedEvent> event = [mtlDevice newSharedEvent];
    if (!event) {
        UZ_LOG(MetalRHI, Error,
               "MetalFence::Init -- failed to create MTLSharedEvent (device may not support shared events)");
        return false;
    }

    // __bridge_retained transfers ownership to m_event (balanced by release in destructor)
    m_event = (__bridge_retained MTL::SharedEvent*)event;

    UZ_LOG(MetalRHI, Trace, "MetalFence created (MTLSharedEvent)");
    return true;
#else
    UZ_LOG(MetalRHI, Error, "MetalFence::Init -- Metal backend not available on this platform");
    return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// IRHIFence implementation
// ─────────────────────────────────────────────────────────────────────────────

/// Signals the fence after all previously submitted GPU work completes.
///
/// Creates a lightweight command buffer on the graphics queue and encodes
/// a signal event into it. The GPU will set the event value only after all
/// earlier command buffers on the same queue have finished executing. This
/// matches DX12's `ID3D12CommandQueue::Signal()` semantics.
void MetalFence::Signal(uint64_t value) {
    UZ_PROFILE_FUNCTION();
    if (!m_device || !m_event) return;

#if defined(__APPLE__)
    id<MTLSharedEvent> event = (__bridge id<MTLSharedEvent>)m_event;
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)m_device->GetGraphicsQueue();
    id<MTLCommandBuffer> cmdBuf = [queue commandBuffer];
    if (!cmdBuf) {
        UZ_LOG(MetalRHI, Error,
               "MetalFence::Signal -- failed to create command buffer for fence signal");
        return;
    }

    // Encode the signal into the command buffer so it fires after all
    // previously enqueued GPU work on this queue completes.
    [cmdBuf encodeSignalEvent:event value:value];
    [cmdBuf commit];
#endif
}

void MetalFence::WaitCPU(uint64_t value) {
    UZ_PROFILE_FUNCTION();
    if (!m_device || !m_event) return;

#if defined(__APPLE__)
    id<MTLSharedEvent> event = (__bridge id<MTLSharedEvent>)m_event;

    // Fast path: already completed
    if (event.signaledValue >= value) return;

    // Create a dispatch semaphore for blocking wait
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

    // Create a listener on a dedicated queue to avoid deadlocking the main queue
    dispatch_queue_t listenerQueue = dispatch_queue_create(
        "uz.metal.fence.wait", DISPATCH_QUEUE_SERIAL);

    MTLSharedEventListener* listener =
        [[MTLSharedEventListener alloc] initWithDispatchQueue:listenerQueue];

    [event notifyListener:listener
                  atValue:value
                    block:^(id<MTLSharedEvent> /*sharedEvent*/, uint64_t /*val*/) {
                        dispatch_semaphore_signal(semaphore);
                    }];

    // 5 second timeout to avoid infinite hangs
    constexpr int64_t kTimeoutNs = 5'000'000'000LL;
    dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, kTimeoutNs);
    long result = dispatch_semaphore_wait(semaphore, timeout);

    if (result != 0) {
        UZ_LOG(MetalRHI, Error,
               "MetalFence::WaitCPU -- timed out waiting for value {}", value);
    }

    // Cleanup
    [listener release];
    dispatch_release(listenerQueue);
    dispatch_release(semaphore);
#endif
}

bool MetalFence::IsComplete(uint64_t value) const {
    if (!m_device || !m_event) return false;

#if defined(__APPLE__)
    id<MTLSharedEvent> event = (__bridge id<MTLSharedEvent>)m_event;
    return event.signaledValue >= value;
#else
    return false;
#endif
}

uint64_t MetalFence::GetCompletedValue() const {
    if (!m_device || !m_event) return 0;

#if defined(__APPLE__)
    id<MTLSharedEvent> event = (__bridge id<MTLSharedEvent>)m_event;
    return event.signaledValue;
#else
    return 0;
#endif
}

} // namespace uz
