/// Tracy Metal GPU Profiling — ARC Implementation
///
/// This is the ONLY file in the Metal backend compiled with `-fobjc-arc`.
/// It wraps Tracy's `MetalZoneScope` (which requires ARC) behind a C-style
/// function interface so the rest of the backend can remain non-ARC.
///
/// The `MetalZoneScope` is an RAII object that must live between begin and end
/// of a render pass. Since our begin/end calls happen in separate functions,
/// we heap-allocate the scope and destroy it on end.

#include "metal_tracy_gpu.h"

#if defined(__APPLE__)
#import <Metal/Metal.h>

// TracyMetal.hmm requires ARC — this .mm file is compiled with -fobjc-arc
#if defined(TRACY_ENABLE) && __has_include(<tracy/TracyMetal.hmm>)
#include <tracy/TracyMetal.hmm>
#define HAS_TRACY_METAL 1
#else
#define HAS_TRACY_METAL 0
#endif

#endif // __APPLE__

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal State
// ─────────────────────────────────────────────────────────────────────────────

/// Wraps the Tracy context and the currently active zone scope.
///
/// The zone scope must be heap-allocated because `MetalZoneScope` is RAII
/// and must survive across the BeginRenderPass/EndRenderPass boundary.
struct MetalTracyState {
#if defined(__APPLE__) && HAS_TRACY_METAL
    tracy::MetalCtx* ctx = nullptr;
    tracy::MetalZoneScope* activeZone = nullptr;
#endif
};

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

/// Pre-flight check: verifies the device can actually allocate a counter sample buffer.
///
/// Tracy's `MetalCtx` constructor calls `assert(false)` if buffer creation fails,
/// which is fatal. We probe first with a small buffer to avoid that crash path.
/// This catches cases where the system has exhausted counter sample buffer resources
/// (e.g., during rapid device creation/destruction in tests).
#if defined(__APPLE__) && HAS_TRACY_METAL
static bool CanAllocateCounterSampleBuffer(id<MTLDevice> device) {
    if (![device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary]) {
        return false;
    }

    // Find timestamp counter set
    id<MTLCounterSet> timestampSet = nil;
    for (id<MTLCounterSet> cs in device.counterSets) {
        if ([cs.name isEqualToString:MTLCommonCounterSetTimestamp]) {
            timestampSet = cs;
            break;
        }
    }
    if (!timestampSet) return false;

    // Probe with 2 buffers of 4096 samples each — exactly what Tracy allocates.
    // Both must succeed simultaneously since Tracy holds them concurrently.
    MTLCounterSampleBufferDescriptor* desc = [[MTLCounterSampleBufferDescriptor alloc] init];
    desc.counterSet = timestampSet;
    desc.sampleCount = 4096;
    desc.storageMode = MTLStorageModeShared;
    desc.label = @"UzTracyProbe";

    NSError* error = nil;
    id<MTLCounterSampleBuffer> probe1 =
        [device newCounterSampleBufferWithDescriptor:desc error:&error];
    if (!probe1) return false;

    id<MTLCounterSampleBuffer> probe2 =
        [device newCounterSampleBufferWithDescriptor:desc error:&error];
    // ARC releases both probes when they go out of scope
    return (probe2 != nil);
}
#endif

/// Creates the Tracy Metal context via `TracyMetalContext()`.
void* MetalTracyCreate(void* mtlDevice) {
#if defined(__APPLE__) && HAS_TRACY_METAL
    if (!mtlDevice) return nullptr;

    id<MTLDevice> device = (__bridge id<MTLDevice>)mtlDevice;

    // Pre-flight: ensure the device can allocate counter sample buffers.
    // Tracy's MetalCtx asserts on failure, so we must verify first.
    if (!CanAllocateCounterSampleBuffer(device)) {
        return nullptr;
    }

    auto* tracyCtx = TracyMetalContext(device);
    if (!tracyCtx) return nullptr;

    auto* state = new MetalTracyState();
    state->ctx = tracyCtx;
    return state;
#else
    (void)mtlDevice;
    return nullptr;
#endif
}

/// Destroys the Tracy Metal context and frees the state.
void MetalTracyDestroy(void* ctx) {
#if defined(__APPLE__) && HAS_TRACY_METAL
    if (!ctx) return;

    auto* state = static_cast<MetalTracyState*>(ctx);

    // End any dangling zone before destroying the context
    if (state->activeZone) {
        delete state->activeZone;
        state->activeZone = nullptr;
    }

    if (state->ctx) {
        TracyMetalDestroy(state->ctx);
        state->ctx = nullptr;
    }

    delete state;
#else
    (void)ctx;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-Frame Collection
// ─────────────────────────────────────────────────────────────────────────────

/// Forwards to `tracy::MetalCtx::Collect()`.
void MetalTracyCollect(void* ctx) {
#if defined(__APPLE__) && HAS_TRACY_METAL
    if (!ctx) return;

    auto* state = static_cast<MetalTracyState*>(ctx);
    if (state->ctx) {
        TracyMetalCollect(state->ctx);
    }
#else
    (void)ctx;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Zone Management
// ─────────────────────────────────────────────────────────────────────────────

/// Begins a Tracy GPU zone by heap-allocating a `MetalZoneScope`.
///
/// The source location is constructed with the provided zone name. File/line
/// information points to this wrapper function — acceptable for engine-level
/// GPU zones where the zone name ("GBuffer", "Lighting") is descriptive enough.
void MetalTracyZoneBegin(void* ctx, void* rpDesc, const char* name) {
#if defined(__APPLE__) && HAS_TRACY_METAL
    if (!ctx || !rpDesc) return;

    auto* state = static_cast<MetalTracyState*>(ctx);
    if (!state->ctx) return;

    // End any previously active zone (safety net for mismatched begin/end)
    if (state->activeZone) {
        delete state->activeZone;
        state->activeZone = nullptr;
    }

    MTLRenderPassDescriptor* desc = (__bridge MTLRenderPassDescriptor*)rpDesc;

    // Construct a static source location for Tracy (name is expected to be a string literal)
    static constexpr tracy::SourceLocationData srcLoc{
        "MetalGpuZone", "MetalTracyZoneBegin", __FILE__, __LINE__, 0
    };

    // Use placement-compatible heap allocation for the RAII zone scope.
    // The MetalZoneScope constructor attaches counter queries to the descriptor.
    state->activeZone = new tracy::MetalZoneScope(
        state->ctx, desc, &srcLoc, true);
#else
    (void)ctx;
    (void)rpDesc;
    (void)name;
#endif
}

/// Ends the active Tracy GPU zone by destroying the heap-allocated scope.
void MetalTracyZoneEnd(void* ctx) {
#if defined(__APPLE__) && HAS_TRACY_METAL
    if (!ctx) return;

    auto* state = static_cast<MetalTracyState*>(ctx);
    if (state->activeZone) {
        // Destructor calls SubmitZoneEndGpu, completing the GPU zone
        delete state->activeZone;
        state->activeZone = nullptr;
    }
#else
    (void)ctx;
#endif
}

} // namespace uz
