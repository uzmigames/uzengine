#pragma once

/// DX12 GPU Timestamp Profiler
///
/// Records GPU timestamps via ID3D12QueryHeap for per-pass and per-frame timing.
/// Triple-buffered: frame N writes timestamps, frame N-2 reads results from readback.
/// Results are resolved in milliseconds using the GPU timestamp frequency.

#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <vector>

namespace uz {

using Microsoft::WRL::ComPtr;

class DX12Device;

/// Maximum number of timestamp pairs (begin+end) per frame.
static constexpr uint32_t kMaxGpuTimestamps = 64;

/// Triple-buffer count matching frame pacing.
static constexpr uint32_t kProfilerFramesInFlight = 3;

/// A single GPU timing result in milliseconds.
struct GpuTimingResult {
    const char* name = nullptr;  ///< Pass/scope name (string literal, not owned)
    float ms = 0.0f;            ///< Elapsed time in milliseconds
};

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
    /// Initializes query heap and readback buffer. Returns false on failure.
    bool Init(DX12Device* device);

    /// Marks the beginning of a profiled frame. Resets the timestamp counter.
    void BeginFrame(ID3D12GraphicsCommandList* cmdList);

    /// Records a begin timestamp for a named scope.
    void BeginTimestamp(ID3D12GraphicsCommandList* cmdList, const char* name);

    /// Records an end timestamp for a named scope.
    void EndTimestamp(ID3D12GraphicsCommandList* cmdList, const char* name);

    /// Resolves query data into the readback buffer at end of frame.
    void EndFrame(ID3D12GraphicsCommandList* cmdList);

    /// Reads back resolved timestamps from frame N-2 and computes ms deltas.
    void ResolveFrame();

    /// Returns the most recent resolved timing results.
    const std::vector<GpuTimingResult>& GetResults() const { return m_results; }

    /// Returns total GPU frame time (first BeginTimestamp to last EndTimestamp).
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
    const char* m_names[kMaxGpuTimestamps] = {};

    std::vector<GpuTimingResult> m_results;
    float m_frameTimeMs = 0.0f;
};

} // namespace uz
