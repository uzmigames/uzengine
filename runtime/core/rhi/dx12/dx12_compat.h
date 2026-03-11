#pragma once

/// Cross-compiler helpers for DX12 COM methods that differ between MSVC and MinGW ABI.
///
/// MSVC returns small structs by value; MinGW/Clang use an out-parameter.
/// These wrappers normalize the calling convention so engine code compiles
/// with both toolchains.

#include <d3d12.h>

namespace uz::dx12 {

inline D3D12_CPU_DESCRIPTOR_HANDLE GetCPUStart(ID3D12DescriptorHeap* heap) {
#if defined(_MSC_VER) || !defined(_WIN32)
    return heap->GetCPUDescriptorHandleForHeapStart();
#else
    D3D12_CPU_DESCRIPTOR_HANDLE h = {};
    heap->GetCPUDescriptorHandleForHeapStart(&h);
    return h;
#endif
}

inline D3D12_GPU_DESCRIPTOR_HANDLE GetGPUStart(ID3D12DescriptorHeap* heap) {
#if defined(_MSC_VER) || !defined(_WIN32)
    return heap->GetGPUDescriptorHandleForHeapStart();
#else
    D3D12_GPU_DESCRIPTOR_HANDLE h = {};
    heap->GetGPUDescriptorHandleForHeapStart(&h);
    return h;
#endif
}

inline D3D12_DESCRIPTOR_HEAP_DESC GetHeapDesc(ID3D12DescriptorHeap* heap) {
#if defined(_MSC_VER) || !defined(_WIN32)
    return heap->GetDesc();
#else
    D3D12_DESCRIPTOR_HEAP_DESC d = {};
    heap->GetDesc(&d);
    return d;
#endif
}

} // namespace uz::dx12
