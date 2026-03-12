#pragma once

/// DX12 GPU Timestamp Profiler
///
/// Records GPU timestamps via ID3D12QueryHeap for per-pass and per-frame timing.
/// Triple-buffered: frame N writes timestamps, frame N-2 reads results from readback.
/// Results are resolved in milliseconds using the GPU timestamp frequency.

#include "../gpu_profiler_types.h"

#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace uz {

using Microsoft::WRL::ComPtr;

class DX12Device;

// ─────────────────────────────────────────────────────────────────────────────
// DX12GpuProfiler
// ─────────────────────────────────────────────────────────────────────────────

/// Records GPU timestamps and resolves them to milliseconds.
///
/// Usage per frame:
///   profiler.BeginFrame(cmdList);
///   profiler.BeginTimestamp(cmdList, "GBuffer");
///   // ... render GBuffer ...
///   profiler.EndTimestamp(cmdList, "GBuffer");
///   profiler.EndFrame(cmdList);
///   // ... after fence wait for this frame ...
///   profiler.ResolveFrame();
class DX12GpuProfiler {
public:
    /// Creates the timestamp query heap and readback buffer.
    ///
    /// Queries the GPU timestamp frequency from the direct queue. Returns `false`
    /// if the queue does not support timestamps.
    ///
    /// # Arguments
    /// * `device` — Parent DX12 device.
    bool Init(DX12Device* device);

    /// Resets the timestamp counter for a new frame.
    ///
    /// # Arguments
    /// * `cmdList` — Active command list (currently unused, reserved for future use).
    void BeginFrame(ID3D12GraphicsCommandList* cmdList);

    /// Records a begin timestamp for a named scope.
    ///
    /// Must be paired with a subsequent `EndTimestamp()` call with the same name.
    ///
    /// # Arguments
    /// * `cmdList` — Active command list to record the timestamp query on.
    /// * `name` — Human-readable scope name (string literal, not copied).
    void BeginTimestamp(ID3D12GraphicsCommandList* cmdList, const char* name);

    /// Records an end timestamp for the current scope.
    ///
    /// # Arguments
    /// * `cmdList` — Active command list to record the timestamp query on.
    /// * `name` — Scope name (currently unused, kept for symmetry with `BeginTimestamp`).
    void EndTimestamp(ID3D12GraphicsCommandList* cmdList, const char* name);

    /// Resolves all timestamp queries for this frame into the readback buffer.
    ///
    /// Must be called at the end of the frame, before the command list is submitted.
    /// Advances the internal frame index.
    ///
    /// # Arguments
    /// * `cmdList` — Active command list to record the resolve command on.
    void EndFrame(ID3D12GraphicsCommandList* cmdList);

    /// Reads back resolved timestamps from 2 frames ago and computes millisecond deltas.
    ///
    /// Call this after the GPU has completed the frame whose timestamps are being read
    /// (i.e., after a fence wait for frame N-2).
    void ResolveFrame();

    /// Returns the most recent resolved timing results.
    ///
    /// # Returns
    /// A vector of `GpuTimingResult` entries (one per `BeginTimestamp`/`EndTimestamp` pair).
    /// Empty if no timestamps were recorded or `ResolveFrame()` has not been called.
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

    DX12Device* m_device = nullptr;

    ComPtr<ID3D12QueryHeap> m_queryHeap;
    ComPtr<ID3D12Resource>  m_readbackBuffer;

    uint64_t m_gpuFrequency = 0;        ///< GPU timestamp ticks per second
    uint32_t m_currentFrame = 0;        ///< Current frame index (0..2)
    uint32_t m_timestampCount = 0;      ///< Number of timestamp pairs this frame

    /// Names for each timestamp pair, indexed by pair index.
    std::array<const char*, kMaxGpuTimestamps> m_names = {};

    std::vector<GpuTimingResult> m_results;
    float m_frameTimeMs = 0.0F;
};

} // namespace uz
