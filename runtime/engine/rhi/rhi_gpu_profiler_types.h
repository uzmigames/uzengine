#pragma once

/// Shared GPU profiler types and constants used by all RHI backends.
///
/// Both DX12 and DX11 GPU profilers share these definitions. This header
/// is backend-agnostic — it contains no DX12, DX11, Vulkan, or Metal types.

#include <cstdint>

namespace uz {

/// Maximum number of timestamp pairs (begin + end) per frame.
///
/// Each pair uses 2 timestamp queries. Total queries per frame = `kMaxGpuTimestamps * 2`.
static constexpr uint32_t kMaxGpuTimestamps = 64;

/// Triple-buffer count for timestamp readback latency hiding.
///
/// Frame N writes timestamps, frame N-2 reads results (allows 2 frames of GPU latency).
static constexpr uint32_t kProfilerFramesInFlight = 3;

/// A single GPU timing result resolved from a begin/end timestamp pair.
struct GpuTimingResult {
    const char* name = nullptr;  ///< Pass/scope name (string literal, not owned).
    float ms = 0.0F;            ///< Elapsed GPU time in milliseconds.
};

} // namespace uz
