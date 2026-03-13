#pragma once

/// Metal GPU Timestamp Profiler
///
/// Records GPU timestamps via `MTLCounterSampleBuffer` for per-pass and per-frame timing.
/// Triple-buffered: frame N writes timestamps, frame N-2 reads results from the counter buffer.
/// Results are resolved in milliseconds using the GPU nanosecond-domain timestamps.
///
/// # Metal-Specific Design
/// Unlike DX12's explicit query heap, Metal attaches counter sample buffers to render pass
/// descriptors via `sampleBufferAttachments`. Timestamps are captured at encoder boundaries:
/// - Start of vertex stage for the "begin" timestamp.
/// - End of fragment stage for the "end" timestamp.
///
/// Not all Metal devices support counter sampling. The profiler gracefully degrades to a
/// no-op on unsupported hardware (e.g., older Intel Macs, iOS Simulator).
///
/// # Usage
/// ```cpp
/// profiler.BeginFrame();
/// profiler.BeginTimestamp("GBuffer");
/// cmdList->BeginRenderPass(desc);  // internally calls AttachToRenderPass
/// // ... render ...
/// cmdList->EndRenderPass();        // internally calls OnRenderPassEnd
/// profiler.EndTimestamp("GBuffer");
/// profiler.EndFrame();
/// // ... after fence wait for frame N-2 ...
/// profiler.ResolveFrame();
/// ```

#include "../rhi_gpu_profiler_types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace uz {

class MetalDevice;

// ─────────────────────────────────────────────────────────────────────────────
// MetalGpuProfiler
// ─────────────────────────────────────────────────────────────────────────────

/// Records GPU timestamps via Metal counter sample buffers and resolves them to milliseconds.
///
/// The profiler uses `sampleBufferAttachments` on `MTLRenderPassDescriptor` to capture
/// timestamps at encoder boundaries. Each timestamp pair (begin + end) measures one
/// GPU scope (e.g., "GBuffer", "Lighting", "PostProcess").
///
/// Triple-buffered to hide GPU readback latency: frame N writes, frame N-2 reads.
class MetalGpuProfiler {
public:
    /// Checks whether the device supports GPU timestamp counter sampling.
    ///
    /// Only performs lightweight capability detection — no GPU resources are allocated.
    /// Counter sample buffers are lazily created on the first `BeginTimestamp()` call.
    /// If unsupported, sets `m_supported = false` and returns `true` (graceful degradation).
    ///
    /// # Arguments
    /// * `device` — Parent MetalDevice. Must outlive this profiler.
    ///
    /// # Returns
    /// `true` on success (including graceful degradation), `false` on fatal error.
    bool Init(MetalDevice* device);

    /// Releases all counter sample buffers and resets state.
    void Shutdown();

    /// Resets the timestamp counter for a new frame.
    ///
    /// Must be called once at the start of each frame before any `BeginTimestamp()` calls.
    void BeginFrame();

    /// Records a begin timestamp for a named scope.
    ///
    /// Stores the scope name and marks a pending begin. The actual timestamp attachment
    /// happens when `AttachToRenderPass()` is called (typically from `MetalCommandList::BeginRenderPass`).
    ///
    /// Must be called BEFORE the corresponding `BeginRenderPass`.
    ///
    /// # Arguments
    /// * `name` — Human-readable scope name (string literal, not copied). Must remain valid
    ///            until `ResolveFrame()` reads it back.
    void BeginTimestamp(const char* name);

    /// Records an end timestamp for the current scope.
    ///
    /// On Metal, this is largely a bookkeeping operation since the end timestamp is
    /// automatically captured by the render pass encoder's `endOfFragmentSampleIndex`.
    ///
    /// # Arguments
    /// * `name` — Scope name (kept for API symmetry with `BeginTimestamp`; currently unused).
    void EndTimestamp(const char* name);

    /// Advances the frame index for triple-buffering rotation.
    ///
    /// Saves the timestamp count for this frame (used during readback in `ResolveFrame`).
    /// Unlike DX12, no explicit resolve command is needed — Metal resolves on demand.
    void EndFrame();

    /// Reads back resolved timestamps from frame N-2 and computes millisecond deltas.
    ///
    /// Metal timestamps from `MTLCounterResultTimestamp` are in the GPU's nanosecond domain.
    /// On Apple Silicon, the period is 1.0 (timestamps are directly in nanoseconds).
    ///
    /// Call this after the GPU has completed the frame whose timestamps are being read
    /// (i.e., after a fence wait for frame N-2).
    void ResolveFrame();

    /// Returns the most recent resolved timing results.
    ///
    /// # Returns
    /// A vector of `GpuTimingResult` entries (one per `BeginTimestamp`/`EndTimestamp` pair).
    /// Empty if no timestamps were recorded or `ResolveFrame()` has not been called.
    [[nodiscard]] const std::vector<GpuTimingResult>& GetResults() const { return m_results; }

    /// Returns the total GPU frame time spanning all profiled scopes.
    ///
    /// # Returns
    /// Time in milliseconds from the first `BeginTimestamp` to the last `EndTimestamp`.
    /// Returns `0.0f` if no timestamps were resolved.
    [[nodiscard]] float GetFrameTimeMs() const { return m_frameTimeMs; }

    /// Returns true if the device supports GPU timestamp counters.
    ///
    /// When false, all profiling methods are safe to call but produce no results.
    [[nodiscard]] bool IsSupported() const { return m_supported; }

    /// Attaches the counter sample buffer to a render pass descriptor.
    ///
    /// Called by `MetalCommandList::BeginRenderPass()` to set up `sampleBufferAttachments[0]`
    /// on the descriptor before creating the render encoder. Configures:
    /// - `startOfVertexSampleIndex` = begin timestamp index
    /// - `endOfFragmentSampleIndex` = end timestamp index
    /// - `startOfFragmentSampleIndex` = `MTLCounterDontSample`
    /// - `endOfVertexSampleIndex` = `MTLCounterDontSample`
    ///
    /// # Arguments
    /// * `rpDesc` — `MTLRenderPassDescriptor*` cast to `void*` (bridged). Must not be null.
    ///
    /// # Returns
    /// `true` if a pending timestamp was attached, `false` otherwise (no pending begin,
    /// profiling unsupported, or capacity exhausted).
    bool AttachToRenderPass(void* rpDesc);

    /// Notifies the profiler that a render pass has ended.
    ///
    /// Clears internal state related to the pending timestamp pair. Called by
    /// `MetalCommandList::EndRenderPass()`.
    void OnRenderPassEnd();

private:
    /// Total timestamps per frame (2 per scope: begin + end).
    static constexpr uint32_t kTimestampsPerFrame = kMaxGpuTimestamps * 2;

    /// Lazily creates counter sample buffers on first use.
    ///
    /// Called internally from `BeginTimestamp()` on the first invocation.
    /// Separated from `Init()` to avoid allocating heavy GPU resources
    /// when profiling is not actively used.
    ///
    /// # Returns
    /// `true` if buffers were created (or already exist), `false` on allocation failure.
    bool EnsureBuffersCreated();

    MetalDevice* m_device = nullptr;
    bool m_supported = false;        ///< Device supports counter sampling.
    bool m_buffersCreated = false;   ///< Counter sample buffers have been allocated.

    /// Counter sample buffers — triple-buffered, one per frame in flight.
    /// Stored as `void*` to avoid ObjC types in the header. Each is an
    /// `id<MTLCounterSampleBuffer>` retained via `__bridge_retained`.
    /// Lazily allocated by `EnsureBuffersCreated()`.
    void* m_counterSampleBuffers[kProfilerFramesInFlight] = {};

    uint32_t m_currentFrame = 0;     ///< Current frame index (0..kProfilerFramesInFlight-1).
    uint32_t m_timestampCount = 0;   ///< Number of timestamp pairs recorded this frame.
    bool m_hasPendingBegin = false;  ///< True between BeginTimestamp and AttachToRenderPass.

    /// Names for each timestamp pair, indexed by pair index.
    std::array<const char*, kMaxGpuTimestamps> m_names = {};

    /// Per-frame timestamp pair counts for readback (indexed by frame index).
    std::array<uint32_t, kProfilerFramesInFlight> m_frameTsCounts = {};

    std::vector<GpuTimingResult> m_results;
    float m_frameTimeMs = 0.0F;
};

} // namespace uz
