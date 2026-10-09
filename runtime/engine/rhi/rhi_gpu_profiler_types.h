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

/// Ring depth of the timestamp readback: one slot more than the command-list ring (3 frames
/// in flight, `DX12CommandList::kFramesInFlight`). At the end of frame N the slot resolved
/// belongs to frame N-3, whose fence the CPU waited before recording frame N. A 3-deep ring
/// resolved frame N-2 while it could still be executing (readback race, until 2026-10-05).
static constexpr uint32_t kProfilerFramesInFlight = 4;
/// Capacity of a pass name copied into a profiler slot, terminator included. Names are copied
/// because a pass name may be a temporary string and because the slot is read three frames
/// after it was recorded.
static constexpr uint32_t kGpuProfilerNameCapacity = 48;

/// A single GPU timing result resolved from a begin/end timestamp pair.
struct GpuTimingResult {
    const char* name = nullptr;  ///< Pass/scope name (string literal, not owned).
    float ms = 0.0F;            ///< Elapsed GPU time in milliseconds.
};

} // namespace uz
