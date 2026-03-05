#pragma once

// gizmo_utils.h — Shared utilities for gizmo GPU resource creation.

#include "engine/renderer/renderer.h"  // For Vertex struct

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

#include <d3d12.h>
#include <wrl/client.h>

namespace uz {

/// Upload a LINE_LIST vertex buffer to an upload heap.
/// Shared by all gizmo implementations to avoid duplicating DX12 boilerplate.
inline void gizmo_upload_vb(
    ID3D12Device* device,
    const Vertex* verts, uint32_t vertCount,
    Microsoft::WRL::ComPtr<ID3D12Resource>& outBuffer,
    D3D12_VERTEX_BUFFER_VIEW& outView,
    const char* debugName)
{
    const UINT bufferSize = sizeof(Vertex) * vertCount;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type                 = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask     = 1;
    heapProps.VisibleNodeMask      = 1;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width              = bufferSize;
    bufferDesc.Height             = 1;
    bufferDesc.DepthOrArraySize   = 1;
    bufferDesc.MipLevels          = 1;
    bufferDesc.Format             = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc         = { 1, 0 };
    bufferDesc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags              = D3D12_RESOURCE_FLAG_NONE;

    auto hr = device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&outBuffer));
    if (FAILED(hr)) {
        throw std::runtime_error(std::string("Failed to create gizmo VB: ") + debugName);
    }

    void* mappedData = nullptr;
    D3D12_RANGE readRange{ 0, 0 };
    outBuffer->Map(0, &readRange, &mappedData);
    std::memcpy(mappedData, verts, bufferSize);
    outBuffer->Unmap(0, nullptr);

    outView.BufferLocation = outBuffer->GetGPUVirtualAddress();
    outView.StrideInBytes  = sizeof(Vertex);
    outView.SizeInBytes    = bufferSize;
}

} // namespace uz
