#pragma once

/// DX12 backend implementation of IRHIFence.
///
/// Wraps an ID3D12Fence and a Win32 event for CPU-side waits.
/// GPU queue signaling is done externally via ID3D12CommandQueue::Signal();
/// this class exposes CPU-side Signal() and blocking WaitCPU().

#include "../rhi.h"

#include <wrl/client.h>
#include <d3d12.h>

namespace uz {

using Microsoft::WRL::ComPtr;

// ─────────────────────────────────────────────────────────────────────────────
// DX12Fence
// ─────────────────────────────────────────────────────────────────────────────

/// DX12 implementation of IRHIFence using ID3D12Fence + Win32 event.
///
/// Fences are monotonically increasing: each Signal(value) must use a value
/// greater than any previously signaled value.
class DX12Fence final : public IRHIFence {
public:
    DX12Fence() = default;
    ~DX12Fence() override;

    /// Creates the D3D12 fence (initial value 0) and a Win32 auto-reset event.
    ///
    /// # Arguments
    /// * `device` — D3D12 device to create the fence on.
    ///
    /// # Returns
    /// `true` on success, `false` on failure (cleans up partial state).
    bool Init(ID3D12Device* device);

    // ── IRHIFence overrides ──────────────────────────────────────────────

    /// Signals the fence to the given value from the CPU side.
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
    HANDLE m_event = nullptr;
};

} // namespace uz
