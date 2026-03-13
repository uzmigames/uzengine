/// DX12 GPU Breadcrumb Markers Implementation
///
/// Uses WriteBufferImmediate to write sequential uint32 markers into a readback buffer.
/// The GPU writes markers in order — on a hang, the last written value identifies
/// the failing operation. Readback is always possible since the buffer is CPU-visible.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx12_gpu_breadcrumbs.h"
#include "dx12_device.h"
#include "dx12_guids.h"
#include "log.h"
#include "profiler.h"

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace uz {

/// Creates a readback buffer for breadcrumb marker writes.
bool DX12GpuBreadcrumbs::Init(DX12Device* device) {
    UZ_PROFILE_FUNCTION();
    m_device = device;
    auto* d3dDevice = device->GetDevice();

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width = sizeof(uint32_t) * kMaxBreadcrumbs;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = d3dDevice->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&m_breadcrumbBuffer));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "Failed to create breadcrumb buffer, hr=0x{:08X}",
               static_cast<uint32_t>(hr));
        return false;
    }

    UZ_LOG(RHI, Info, "GPU breadcrumbs initialized (max {} markers/frame)", kMaxBreadcrumbs);
    return true;
}

/// Resets marker counter for a new frame.
void DX12GpuBreadcrumbs::BeginFrame() {
    m_markerCount = 0;
}

/// Writes a sequential breadcrumb marker via WriteBufferImmediate.
void DX12GpuBreadcrumbs::Mark(ID3D12GraphicsCommandList* cmdList, const char* name) {
    if (m_markerCount >= kMaxBreadcrumbs) return;

    m_names[m_markerCount] = name;

    // WriteBufferImmediate writes a uint32 to the GPU buffer at the current execution point
    D3D12_WRITEBUFFERIMMEDIATE_PARAMETER param = {};
    param.Dest = m_breadcrumbBuffer->GetGPUVirtualAddress()
                 + static_cast<uint64_t>(m_markerCount) * sizeof(uint32_t);
    param.Value = m_markerCount + 1;  // 1-based so 0 means "never reached"

    D3D12_WRITEBUFFERIMMEDIATE_MODE mode = D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT;

    // WriteBufferImmediate is on ID3D12GraphicsCommandList2+
    ComPtr<ID3D12GraphicsCommandList2> cmdList2;
    if (SUCCEEDED(cmdList->QueryInterface(IID_ID3D12GraphicsCommandList2,
                                          reinterpret_cast<void**>(cmdList2.GetAddressOf())))) {
        cmdList2->WriteBufferImmediate(1, &param, &mode);
    }

    m_markerCount++;
}

/// Maps the breadcrumb buffer and logs the last completed marker.
void DX12GpuBreadcrumbs::LogLastMarker() {
    UZ_PROFILE_FUNCTION();
    if (!m_breadcrumbBuffer) return;

    uint32_t* mapped = nullptr;
    D3D12_RANGE readRange = {0, sizeof(uint32_t) * kMaxBreadcrumbs};
    HRESULT hr = m_breadcrumbBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "Failed to map breadcrumb buffer for readback");
        return;
    }

    // Find the highest marker value (1-based)
    uint32_t lastCompleted = 0;
    for (uint32_t i = 0; i < kMaxBreadcrumbs; ++i) {
        if (mapped[i] > lastCompleted) {
            lastCompleted = mapped[i];
        }
    }

    D3D12_RANGE writeRange = {0, 0};
    m_breadcrumbBuffer->Unmap(0, &writeRange);

    UZ_LOG(RHI, Error, "=== GPU BREADCRUMBS ===");
    if (lastCompleted == 0) {
        UZ_LOG(RHI, Error, "No breadcrumb markers were reached — GPU hung before first marker");
    } else {
        uint32_t lastIdx = lastCompleted - 1;  // Convert to 0-based
        UZ_LOG(RHI, Error, "Last completed marker: [{}] '{}'",
               lastIdx, (lastIdx < kMaxBreadcrumbs && m_names[lastIdx]) ? m_names[lastIdx] : "(unnamed)");

        // Log the trail
        for (uint32_t i = 0; i < m_markerCount; ++i) {
            const char* status = (i < lastIdx) ? "DONE" : (i == lastIdx) ? ">>> LAST" : "NOT REACHED";
            UZ_LOG(RHI, Error, "  [{:3}] {} — {}", i,
                   m_names[i] ? m_names[i] : "(unnamed)", status);
        }
    }
    UZ_LOG(RHI, Error, "=== END BREADCRUMBS ===");
}

} // namespace uz
