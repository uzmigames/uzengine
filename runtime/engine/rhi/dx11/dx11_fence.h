#pragma once

/// DX11 backend implementation of IRHIFence.
///
/// DX11 has no native timeline fences. This implementation emulates them
/// using `D3D11_QUERY_EVENT` queries. Each `Signal(value)` creates an
/// event query that the GPU will complete after all preceding work.
/// `WaitCPU(value)` polls the query until completion.
///
/// # Performance
/// This is inherently slower than DX12's native fence — use sparingly.
/// Typical usage: once per frame for frame pacing.

#include "../rhi.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11_1.h>
#include <cstdint>
#include <vector>

namespace uz {

class DX11Device;

// ─────────────────────────────────────────────────────────────────────────────
// DX11Fence
// ─────────────────────────────────────────────────────────────────────────────

/// DX11 implementation of IRHIFence using emulated event queries.
///
/// Fences are monotonically increasing: each Signal(value) must use a value
/// greater than any previously signaled value.
class DX11Fence final : public IRHIFence {
public:
    DX11Fence() = default;
    ~DX11Fence() override;

    /// Initializes the fence with a reference to the parent device.
    ///
    /// # Arguments
    /// * `device` — Parent DX11 device (must outlive this fence).
    ///
    /// # Returns
    /// `true` on success, `false` on failure.
    bool Init(DX11Device* device);

    // ── IRHIFence overrides ──────────────────────────────────────────────

    /// Signals the fence by creating an event query on the immediate context.
    void Signal(uint64_t value) override;

    /// Blocks the calling CPU thread until the fence reaches the given value.
    void WaitCPU(uint64_t value) override;

    /// Returns true if the fence has reached or passed the given value.
    bool IsComplete(uint64_t value) const override;

    /// Returns the highest value the fence has completed.
    uint64_t GetCompletedValue() const override;

private:
    /// Polls pending queries and advances the completed value.
    void PollPending();

    /// A pending GPU event query paired with a fence value.
    struct PendingQuery {
        ID3D11Query* query = nullptr;  ///< D3D11_QUERY_EVENT query
        uint64_t value = 0;            ///< Associated fence value
    };

    DX11Device*               m_device = nullptr;
    std::vector<PendingQuery> m_pending;
    uint64_t                  m_completedValue = 0;
    uint64_t                  m_lastSignaledValue = 0;
};

} // namespace uz
