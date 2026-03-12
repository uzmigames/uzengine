/// DX11 Fence Implementation
///
/// Emulated timeline fence using D3D11_QUERY_EVENT queries.
/// Each Signal() creates an event query; WaitCPU() polls until completion.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx11_fence.h"
#include "dx11_device.h"

#include "log.h"
#include "profiler.h"

#include <windows.h>

namespace uz {

DX11Fence::~DX11Fence() {
    // Release any remaining pending queries
    for (auto& pq : m_pending) {
        if (pq.query) {
            pq.query->Release();
            pq.query = nullptr;
        }
    }
    m_pending.clear();
}

bool DX11Fence::Init(DX11Device* device) {
    UZ_PROFILE_FUNCTION();
    if (!device) {
        UZ_LOG(RHI, Error, "DX11Fence::Init — null device");
        return false;
    }

    m_device = device;
    UZ_LOG(RHI, Trace, "DX11Fence created (emulated via event queries)");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// IRHIFence implementation
// ─────────────────────────────────────────────────────────────────────────────

void DX11Fence::Signal(uint64_t value) {
    UZ_PROFILE_FUNCTION();
    if (!m_device) return;

    D3D11_QUERY_DESC qd = {};
    qd.Query = D3D11_QUERY_EVENT;

    ID3D11Query* query = nullptr;
    HRESULT hr = m_device->GetDevice()->CreateQuery(&qd, &query);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "DX11Fence::Signal — CreateQuery failed, hr=0x{:08X}",
               static_cast<uint32_t>(hr));
        return;
    }

    // End() on the immediate context marks the point where all preceding GPU work
    // must complete before this query returns TRUE.
    m_device->GetImmediateContext()->End(query);

    m_pending.push_back({query, value});
    m_lastSignaledValue = value;
}

void DX11Fence::WaitCPU(uint64_t value) {
    UZ_PROFILE_FUNCTION();
    PollPending();

    while (m_completedValue < value) {
        Sleep(0);
        PollPending();
    }
}

bool DX11Fence::IsComplete(uint64_t value) const {
    // const_cast is safe — PollPending only modifies internal bookkeeping
    const_cast<DX11Fence*>(this)->PollPending();
    return m_completedValue >= value;
}

uint64_t DX11Fence::GetCompletedValue() const {
    const_cast<DX11Fence*>(this)->PollPending();
    return m_completedValue;
}

void DX11Fence::PollPending() {
    if (!m_device || m_pending.empty()) return;

    auto* ctx = m_device->GetImmediateContext();

    while (!m_pending.empty()) {
        auto& front = m_pending.front();
        BOOL data = FALSE;
        HRESULT hr = ctx->GetData(front.query, &data, sizeof(data), 0);

        if (hr == S_OK && data) {
            m_completedValue = front.value;
            front.query->Release();
            m_pending.erase(m_pending.begin());
        } else {
            // Not ready yet — stop polling (queries complete in order)
            break;
        }
    }
}

} // namespace uz
