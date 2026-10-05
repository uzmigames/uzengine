/// DX12 Fence Implementation
///
/// CPU-GPU synchronization via ID3D12Fence + Win32 event.
/// Supports Signal (GPU increments), WaitCPU (block until reached), and status queries.
/// Single event per fence; shared across multiple Signal/Wait pairs.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx12_fence.h"
#include "dx12_guids.h"

// IID_PPV_ARGS(&m_fence) needs __uuidof(ID3D12Fence). On Clang/MinGW that
// resolves through __mingw_uuidof<T>, whose primary template is declared but
// never defined — without the __CRT_UUID_DECL entries this compiles and then
// fails at link with "undefined symbol: __mingw_uuidof<ID3D12Fence>()".
#include "dx12_guids.h"

#include "log.h"
#include "profiler.h"

#include <windows.h>

namespace uz {

/// Cleanup: closes Win32 event if allocated.
DX12Fence::~DX12Fence() {
    if (m_event) {
        CloseHandle(m_event);
        m_event = nullptr;
    }
}

bool DX12Fence::Init(ID3D12Device* device, ID3D12CommandQueue* queue) {
    UZ_PROFILE_FUNCTION();
    if (!queue) {
        UZ_LOG(RHI, Error, "DX12Fence::Init — null command queue");
        return false;
    }

    HRESULT hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "Failed to create ID3D12Fence, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    m_queue = queue;

    m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_event) {
        UZ_LOG(RHI, Error, "Failed to create Win32 event for fence");
        m_fence.Reset();
        return false;
    }

    UZ_LOG(RHI, Trace, "DX12Fence created");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// IRHIFence implementation
// ─────────────────────────────────────────────────────────────────────────────

void DX12Fence::Signal(uint64_t value) {
    HRESULT hr = m_queue->Signal(m_fence.Get(), value);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "DX12Fence::Signal({}) failed, hr=0x{:08X}", value, static_cast<uint32_t>(hr));
    }
}

void DX12Fence::WaitCPU(uint64_t value) {
    UZ_PROFILE_FUNCTION();
    if (m_fence->GetCompletedValue() >= value) {
        return;
    }

    HRESULT hr = m_fence->SetEventOnCompletion(value, m_event);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "DX12Fence::SetEventOnCompletion({}) failed, hr=0x{:08X}",
               value, static_cast<uint32_t>(hr));
        return;
    }

    WaitForSingleObject(m_event, INFINITE);
}

bool DX12Fence::IsComplete(uint64_t value) const {
    return m_fence->GetCompletedValue() >= value;
}

uint64_t DX12Fence::GetCompletedValue() const {
    return m_fence->GetCompletedValue();
}

} // namespace uz
