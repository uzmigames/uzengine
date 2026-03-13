/// Metal GPU Timestamp Profiler Implementation
///
/// Uses `MTLCounterSampleBuffer` with `sampleBufferAttachments` to measure GPU timing.
/// Triple-buffered: frame N writes timestamps, frame N-2 reads resolved results.
///
/// Compiled as Objective-C++ WITHOUT ARC (`-fno-objc-arc`). All Metal ObjC objects are
/// managed via manual retain/release through `__bridge_retained` (store) and explicit
/// `CFRelease` (teardown). Temporary casts use `__bridge` (no ownership transfer).

#include "metal_gpu_profiler.h"
#include "metal_device.h"

#include "log.h"
#include "profiler.h"

#if defined(__APPLE__)
#include <Metal/Metal.h>
#endif

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Initialization
// ─────────────────────────────────────────────────────────────────────────────

/// Lightweight capability check — no GPU resources allocated.
///
/// Counter sample buffers are lazily created on first `BeginTimestamp()` call
/// via `EnsureBuffersCreated()`. This avoids reserving GPU counter hardware
/// when profiling is not actively used.
bool MetalGpuProfiler::Init(MetalDevice* device) {
    UZ_PROFILE_FUNCTION();

    if (!device) {
        UZ_LOG(MetalRHI, Error, "MetalGpuProfiler::Init -- null device");
        return false;
    }

    m_device = device;

#if defined(__APPLE__)
    id<MTLDevice> mtlDevice = (__bridge id<MTLDevice>)device->GetMTLDevice();
    if (!mtlDevice) {
        UZ_LOG(MetalRHI, Error, "MetalGpuProfiler::Init -- MTLDevice is nil");
        return false;
    }

    // Check if the device supports timestamp sampling at stage boundaries
    if (![mtlDevice supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary]) {
        UZ_LOG(MetalRHI, Warning, "GPU timestamp sampling at stage boundary not supported -- "
               "profiler disabled (graceful degradation)");
        m_supported = false;
        return true;
    }

    // Find the timestamp counter set
    id<MTLCounterSet> timestampCounterSet = nil;
    for (id<MTLCounterSet> counterSet in mtlDevice.counterSets) {
        if ([counterSet.name isEqualToString:MTLCommonCounterSetTimestamp]) {
            timestampCounterSet = counterSet;
            break;
        }
    }

    if (!timestampCounterSet) {
        UZ_LOG(MetalRHI, Warning, "MTLCommonCounterSetTimestamp not found -- profiler disabled");
        m_supported = false;
        return true;
    }

    // Device supports counter sampling — buffers will be created lazily
    m_supported = true;
    m_buffersCreated = false;
    m_currentFrame = 0;
    m_timestampCount = 0;
    m_frameTsCounts.fill(0);

    UZ_LOG(MetalRHI, Info, "GPU profiler: counter sampling supported (buffers deferred)");
    return true;
#else
    m_supported = false;
    return true;
#endif
}

/// Creates triple-buffered counter sample buffers on first use.
///
/// Called lazily from `BeginTimestamp()` to avoid allocating GPU counter
/// hardware when profiling is not actively requested.
bool MetalGpuProfiler::EnsureBuffersCreated() {
    if (m_buffersCreated) return true;
    if (!m_supported || !m_device) return false;

#if defined(__APPLE__)
    id<MTLDevice> mtlDevice = (__bridge id<MTLDevice>)m_device->GetMTLDevice();
    if (!mtlDevice) return false;

    // Re-find the timestamp counter set (Init confirmed it exists)
    id<MTLCounterSet> timestampCounterSet = nil;
    for (id<MTLCounterSet> counterSet in mtlDevice.counterSets) {
        if ([counterSet.name isEqualToString:MTLCommonCounterSetTimestamp]) {
            timestampCounterSet = counterSet;
            break;
        }
    }
    if (!timestampCounterSet) {
        m_supported = false;
        return false;
    }

    for (uint32_t i = 0; i < kProfilerFramesInFlight; ++i) {
        MTLCounterSampleBufferDescriptor* desc =
            [[MTLCounterSampleBufferDescriptor alloc] init];
        desc.counterSet = timestampCounterSet;
        desc.sampleCount = kTimestampsPerFrame;
        desc.storageMode = MTLStorageModeShared;
        desc.label = [NSString stringWithFormat:@"UzGpuProfiler_Frame%u", i];

        NSError* error = nil;
        id<MTLCounterSampleBuffer> sampleBuffer =
            [mtlDevice newCounterSampleBufferWithDescriptor:desc error:&error];
        [desc release];

        if (error || !sampleBuffer) {
            UZ_LOG(MetalRHI, Error, "Failed to create counter sample buffer {}: {}",
                   i, error ? [[error localizedDescription] UTF8String] : "unknown");
            // Clean up any buffers already created
            for (uint32_t j = 0; j < i; ++j) {
                if (m_counterSampleBuffers[j]) {
                    CFRelease(m_counterSampleBuffers[j]);
                    m_counterSampleBuffers[j] = nullptr;
                }
            }
            m_supported = false;
            return false;
        }

        m_counterSampleBuffers[i] = (__bridge_retained void*)sampleBuffer;
    }

    m_buffersCreated = true;
    UZ_LOG(MetalRHI, Info, "GPU profiler buffers created: maxScopes={}/frame, framesInFlight={}",
           kMaxGpuTimestamps, kProfilerFramesInFlight);
    return true;
#else
    return false;
#endif
}

/// Releases all counter sample buffers.
void MetalGpuProfiler::Shutdown() {
    UZ_PROFILE_FUNCTION();

#if defined(__APPLE__)
    for (uint32_t i = 0; i < kProfilerFramesInFlight; ++i) {
        if (m_counterSampleBuffers[i]) {
            CFRelease(m_counterSampleBuffers[i]);
            m_counterSampleBuffers[i] = nullptr;
        }
    }
#endif

    m_supported = false;
    m_buffersCreated = false;
    m_device = nullptr;
    m_results.clear();
    m_frameTimeMs = 0.0F;

    UZ_LOG(MetalRHI, Info, "GPU profiler shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-Frame Recording
// ─────────────────────────────────────────────────────────────────────────────

/// Resets timestamp counter for a new frame.
void MetalGpuProfiler::BeginFrame() {
    m_timestampCount = 0;
    m_hasPendingBegin = false;
}

/// Stores the scope name and marks a pending begin for the next render pass.
///
/// On first call, lazily allocates counter sample buffers via `EnsureBuffersCreated()`.
void MetalGpuProfiler::BeginTimestamp(const char* name) {
    if (!m_supported) return;

    // Lazy allocation — only create GPU counter buffers when profiling is actually used
    if (!m_buffersCreated && !EnsureBuffersCreated()) return;

    if (m_timestampCount >= kMaxGpuTimestamps) {
        UZ_LOG(MetalRHI, Warning, "GPU profiler: max timestamps ({}) reached, skipping '{}'",
               kMaxGpuTimestamps, name ? name : "(null)");
        return;
    }

    m_names[m_timestampCount] = name;
    m_hasPendingBegin = true;
}

/// Attaches counter sample buffer to the render pass descriptor.
bool MetalGpuProfiler::AttachToRenderPass(void* rpDesc) {
    if (!m_supported || !m_hasPendingBegin || !rpDesc) return false;
    if (m_timestampCount >= kMaxGpuTimestamps) return false;

#if defined(__APPLE__)
    MTLRenderPassDescriptor* desc = (__bridge MTLRenderPassDescriptor*)rpDesc;
    id<MTLCounterSampleBuffer> sampleBuffer =
        (__bridge id<MTLCounterSampleBuffer>)m_counterSampleBuffers[m_currentFrame];

    if (!sampleBuffer) {
        UZ_LOG(MetalRHI, Error, "AttachToRenderPass -- counter sample buffer is nil for frame {}",
               m_currentFrame);
        return false;
    }

    uint32_t tsIdx = m_timestampCount;

    // Attach sample buffer: start-of-vertex = begin timestamp, end-of-fragment = end timestamp
    desc.sampleBufferAttachments[0].sampleBuffer = sampleBuffer;
    desc.sampleBufferAttachments[0].startOfVertexSampleIndex = tsIdx * 2;
    desc.sampleBufferAttachments[0].endOfVertexSampleIndex = MTLCounterDontSample;
    desc.sampleBufferAttachments[0].startOfFragmentSampleIndex = MTLCounterDontSample;
    desc.sampleBufferAttachments[0].endOfFragmentSampleIndex = tsIdx * 2 + 1;

    m_hasPendingBegin = false;
    return true;
#else
    (void)rpDesc;
    return false;
#endif
}

/// Marks the end of a timestamp scope. On Metal, the actual end timestamp is captured
/// by the render pass encoder automatically via `endOfFragmentSampleIndex`.
void MetalGpuProfiler::EndTimestamp(const char* /*name*/) {
    if (!m_supported) return;
    if (m_timestampCount >= kMaxGpuTimestamps) return;

    m_timestampCount++;
}

/// Notifies that a render pass has completed — clears any pending state.
void MetalGpuProfiler::OnRenderPassEnd() {
    // Nothing to do on Metal — timestamps are captured automatically by the encoder.
    // This hook exists for future per-pass bookkeeping or diagnostics.
}

/// Saves this frame's timestamp count and advances the triple-buffer index.
void MetalGpuProfiler::EndFrame() {
    UZ_PROFILE_FUNCTION();

    m_frameTsCounts[m_currentFrame] = m_timestampCount;
    m_currentFrame = (m_currentFrame + 1) % kProfilerFramesInFlight;
}

// ─────────────────────────────────────────────────────────────────────────────
// Readback & Resolution
// ─────────────────────────────────────────────────────────────────────────────

/// Reads timestamps from frame N-2 and computes millisecond deltas.
void MetalGpuProfiler::ResolveFrame() {
    UZ_PROFILE_FUNCTION();

    m_results.clear();
    m_frameTimeMs = 0.0F;

    if (!m_supported) return;

#if defined(__APPLE__)
    // Read from the oldest frame — currentFrame was already advanced in EndFrame,
    // so it now points to the frame that is kProfilerFramesInFlight-old
    uint32_t readFrame = m_currentFrame;
    uint32_t count = m_frameTsCounts[readFrame];
    if (count == 0) return;

    id<MTLCounterSampleBuffer> sampleBuffer =
        (__bridge id<MTLCounterSampleBuffer>)m_counterSampleBuffers[readFrame];
    if (!sampleBuffer) return;

    // Resolve the range of timestamps we recorded (2 per scope: begin + end)
    NSRange range = NSMakeRange(0, count * 2);
    NSData* data = [sampleBuffer resolveCounterRange:range];
    if (!data || data.length < count * 2 * sizeof(MTLCounterResultTimestamp)) {
        UZ_LOG(MetalRHI, Warning, "ResolveFrame -- insufficient resolved data: "
               "expected={} bytes, got={} bytes",
               count * 2 * sizeof(MTLCounterResultTimestamp),
               data ? static_cast<uint64_t>(data.length) : 0ULL);
        return;
    }

    const auto* timestamps =
        static_cast<const MTLCounterResultTimestamp*>(data.bytes);

    uint64_t frameBegin = UINT64_MAX;
    uint64_t frameEnd = 0;

    for (uint32_t i = 0; i < count; ++i) {
        uint64_t tBegin = timestamps[i * 2].timestamp;
        uint64_t tEnd   = timestamps[i * 2 + 1].timestamp;

        // Skip invalid or unresolved timestamps
        if (tBegin == 0 && tEnd == 0) break;
        if (tBegin == MTLCounterErrorValue || tEnd == MTLCounterErrorValue) {
            UZ_LOG(MetalRHI, Warning, "ResolveFrame -- MTLCounterErrorValue at scope '{}' (index={})",
                   m_names[i] ? m_names[i] : "(null)", i);
            continue;
        }

        // Metal timestamps on Apple Silicon are in nanoseconds — convert to ms
        float ms = static_cast<float>(static_cast<double>(tEnd - tBegin) / 1'000'000.0);
        m_results.push_back({m_names[i], ms});

        if (tBegin < frameBegin) frameBegin = tBegin;
        if (tEnd > frameEnd) frameEnd = tEnd;
    }

    if (frameBegin < frameEnd) {
        m_frameTimeMs = static_cast<float>(
            static_cast<double>(frameEnd - frameBegin) / 1'000'000.0);
    }
#endif
}

} // namespace uz
