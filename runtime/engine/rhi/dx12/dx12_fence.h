#pragma once

/// DX12 backend implementation of IRHIFence.
///
/// Wraps an ID3D12Fence and a Win32 event for CPU-side waits.
/// Signal() enqueues a GPU-side signal via ID3D12CommandQueue::Signal(),
/// ensuring the fence value is only reached after all preceding GPU work completes.

#include "../rhi.h"

#include <wrl/client.h>
#include <d3d12.h>

namespace uz {

/// Period of the GPU wait watchdog: a CPU wait on a fence (frame fence, WaitIdle) that lasts
/// longer than this logs an error with the device-removed reason and keeps waiting. A hung
/// GPU used to stop the process in an INFINITE wait with nothing in the log.
static constexpr uint32_t kGpuWaitWatchdogMs = 10000;

using Microsoft::WRL::ComPtr;

// ─────────────────────────────────────────────────────────────────────────────
// DX12Fence
// ─────────────────────────────────────────────────────────────────────────────

/// DX12 implementation of IRHIFence using ID3D12Fence + Win32 event.
///
/// Fences are monotonically increasing: each Signal(value) must use a value
/// greater than any previously signaled value.
///
/// Signal() uses the GPU command queue, NOT the CPU, so the fence value
/// is only reached after all preceding ExecuteCommandLists() work finishes.
class DX12Fence final : public IRHIFence {
public:
    DX12Fence() = default;
    ~DX12Fence() override;

    /// Creates the D3D12 fence (initial value 0) and a Win32 auto-reset event.
    ///
    /// # Arguments
    /// * `device` — D3D12 device to create the fence on.
    /// * `queue` — Command queue used for GPU-side Signal() calls.
    ///
    /// # Returns
    /// `true` on success, `false` on failure (cleans up partial state).
    bool Init(ID3D12Device* device, ID3D12CommandQueue* queue);

    // ── IRHIFence overrides ──────────────────────────────────────────────

    /// Enqueues a GPU-side signal on the command queue.
    ///
    /// The fence value is only reached after all previously submitted
    /// command lists on this queue have completed execution.
    void Signal(uint64_t value) override;

    /// Blocks the calling CPU thread until the fence reaches the given value.
    void WaitCPU(uint64_t value) override;

    /// Returns true if the fence has reached or passed the given value.
    bool IsComplete(uint64_t value) const override;

    /// Returns the highest value the fence has completed.
    uint64_t GetCompletedValue() const override;

    /// Returns the raw ID3D12Fence for GPU queue signal operations.
    ID3D12Fence* GetNativeFence() const { return m_fence.Get(); }

private:
    ComPtr<ID3D12Fence> m_fence;
    ID3D12CommandQueue* m_queue = nullptr;
    HANDLE m_event = nullptr;
};

} // namespace uz
