#pragma once

/// DX11 GPU Timestamp Profiler
///
/// Records GPU timestamps via `ID3D11Query` (TIMESTAMP / TIMESTAMP_DISJOINT) for
/// per-pass and per-frame timing. Triple-buffered: frame N writes timestamps,
/// frame N-2 reads results via `GetData()`.
///
/// DX11 uses individual query objects (not heaps) and requires one disjoint query
/// per frame to validate timestamp coherence and obtain the GPU frequency.

#include "../rhi_gpu_profiler_types.h"

#include <d3d11_1.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace uz {

using Microsoft::WRL::ComPtr;

class DX11Device;

// ─────────────────────────────────────────────────────────────────────────────
// DX11GpuProfiler
// ─────────────────────────────────────────────────────────────────────────────

/// Records GPU timestamps and resolves them to milliseconds.
///
/// Usage per frame:
///   profiler.BeginFrame(ctx);
///   profiler.BeginTimestamp(ctx, "GBuffer");
///   // ... render GBuffer ...
///   profiler.EndTimestamp(ctx, "GBuffer");
///   profiler.EndFrame(ctx);
///   // ... next frame or later ...
///   profiler.ResolveFrame();
///
/// DX11 differences from DX12:
/// - Uses individual `ID3D11Query` COM objects instead of a query heap.
/// - One `TIMESTAMP_DISJOINT` query per frame brackets all timestamp queries.
/// - Results are read via `ID3D11DeviceContext::GetData()`, not a readback buffer.
/// - GPU frequency comes from the disjoint query result, not from the command queue.
class DX11GpuProfiler {
public:
    /// Creates all timestamp and disjoint query objects.
    ///
    /// Pre-creates `kMaxGpuTimestamps * 2 * kProfilerFramesInFlight` timestamp queries
    /// and `kProfilerFramesInFlight` disjoint queries via `ID3D11Device::CreateQuery()`.
    ///
    /// # Arguments
    /// * `device` — Parent DX11 device (used to obtain the D3D11 device and context).
    ///
    /// # Returns
    /// `true` on success, `false` if query creation fails.
    bool Init(DX11Device* device);

    /// Begins a new frame: resets the timestamp counter and starts the disjoint query.
    ///
    /// Must be called before any `BeginTimestamp()` calls for this frame.
    ///
    /// # Arguments
    /// * `ctx` — Immediate device context.
    void BeginFrame(ID3D11DeviceContext1* ctx);

    /// Records a begin timestamp for a named scope.
    ///
    /// Must be paired with a subsequent `EndTimestamp()` call with the same name.
    ///
    /// # Arguments
    /// * `ctx` — Immediate device context to record the timestamp query on.
    /// * `name` — Human-readable scope name (string literal, not copied).
    void BeginTimestamp(ID3D11DeviceContext1* ctx, const char* name);

    /// Records an end timestamp for the current scope.
    ///
    /// # Arguments
    /// * `ctx` — Immediate device context to record the timestamp query on.
    /// * `name` — Scope name (currently unused, kept for symmetry with `BeginTimestamp`).
    void EndTimestamp(ID3D11DeviceContext1* ctx, const char* name);

    /// Ends the disjoint query for this frame and advances the frame index.
    ///
    /// Must be called after all timestamp pairs are recorded.
    ///
    /// # Arguments
    /// * `ctx` — Immediate device context.
    void EndFrame(ID3D11DeviceContext1* ctx);

    /// Reads back timestamps from 2 frames ago and computes millisecond deltas.
    ///
    /// Queries the disjoint result first — if `Disjoint` is true (GPU frequency changed
    /// mid-frame, e.g., power state transition), all results for that frame are discarded.
    ///
    /// Call this after the GPU has completed the frame whose timestamps are being read
    /// (i.e., after a fence wait or implicit sync for frame N-2).
    void ResolveFrame();

    /// Returns the most recent resolved timing results.
    ///
    /// # Returns
    /// A vector of `GpuTimingResult` entries (one per `BeginTimestamp`/`EndTimestamp` pair).
    /// Empty if no timestamps were recorded, `ResolveFrame()` has not been called,
    /// or the disjoint query indicated invalid data.
    const std::vector<GpuTimingResult>& GetResults() const { return m_results; }

    /// Returns the total GPU frame time spanning all profiled scopes.
    ///
    /// # Returns
    /// Time in milliseconds from the first `BeginTimestamp` to the last `EndTimestamp`.
    /// Returns `0.0f` if no timestamps were resolved.
    float GetFrameTimeMs() const { return m_frameTimeMs; }

private:
    /// Total timestamps per frame (2 per scope: begin + end).
    static constexpr uint32_t kTimestampsPerFrame = kMaxGpuTimestamps * 2;

    DX11Device* m_device = nullptr;
    ID3D11DeviceContext1* m_context = nullptr;

    /// Disjoint queries, one per in-flight frame.
    std::array<ComPtr<ID3D11Query>, kProfilerFramesInFlight> m_disjointQueries;

    /// Timestamp queries: [frame][queryIndex]. Pre-created in Init().
    std::array<std::array<ComPtr<ID3D11Query>, kTimestampsPerFrame>, kProfilerFramesInFlight> m_timestampQueries;

    uint32_t m_currentFrame = 0;        ///< Current frame index (0..2).
    uint32_t m_timestampCount = 0;      ///< Number of timestamp pairs this frame.

    /// Timestamp pair count recorded for each frame (needed at resolve time).
    std::array<uint32_t, kProfilerFramesInFlight> m_framePairCounts = {};

    /// Names for each timestamp pair, indexed by pair index.
    std::array<const char*, kMaxGpuTimestamps> m_names = {};

    std::vector<GpuTimingResult> m_results;
    float m_frameTimeMs = 0.0F;
};

} // namespace uz
