#pragma once

/// DX12 GPU Timestamp Profiler
///
/// Records GPU timestamps via ID3D12QueryHeap for per-pass and per-frame timing.
/// Triple-buffered: frame N writes timestamps, frame N-2 reads results from readback.
/// Results are resolved in milliseconds using the GPU timestamp frequency.

#include "../rhi_gpu_profiler_types.h"

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
    /// * `name` — Human-readable scope name (copied into the slot, truncated to
    ///   `kGpuProfilerNameCapacity - 1` characters).
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

    /// Reads back the resolved timestamps of the oldest ring slot (frame N-3, whose fence the
    /// CPU already waited) and computes millisecond deltas, labelled with that slot's names.
    ///
    /// Call this after the GPU has completed the frame whose timestamps are being read
    /// (i.e., after a fence wait for frame N-3).
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
    uint32_t m_currentFrame = 0;        ///< Ring slot being recorded (0..kProfilerFramesInFlight-1)
    uint32_t m_timestampCount = 0;      ///< Number of timestamp pairs recorded this frame

    /// What one ring slot recorded: its pass names (copied) and how many pairs its
    /// ResolveQueryData wrote. ResolveFrame reads exactly `count` pairs of the slot it resolves,
    /// under that slot's own names (one shared name table labelled frame N-3's timings with
    /// frame N's pass list until 2026-10-05).
    struct FrameSlot {
        std::array<std::array<char, kGpuProfilerNameCapacity>, kMaxGpuTimestamps> names{};
        uint32_t count = 0;
    };
    std::array<FrameSlot, kProfilerFramesInFlight> m_slots{};

    std::vector<GpuTimingResult> m_results;
    float m_frameTimeMs = 0.0F;
};

} // namespace uz
