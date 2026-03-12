#pragma once

/// Cross-compiler helpers for DX12 COM methods that differ between MSVC and MinGW ABI.
///
/// MSVC's COM ABI returns small structs (like `D3D12_CPU_DESCRIPTOR_HANDLE`)
/// by value. MinGW/Clang uses an out-parameter calling convention for the same
/// methods. These inline wrappers normalize the calling convention so engine code
/// compiles identically with both toolchains.
///
/// # Usage
/// Use `uz::dx12::GetCPUStart()` instead of `heap->GetCPUDescriptorHandleForHeapStart()`.
/// Use `uz::dx12::GetGPUStart()` instead of `heap->GetGPUDescriptorHandleForHeapStart()`.
/// Use `uz::dx12::GetHeapDesc()` instead of `heap->GetDesc()`.

#include <d3d12.h>

namespace uz::dx12 {

/// Returns the CPU descriptor handle for the start of the heap (ABI-safe).
inline D3D12_CPU_DESCRIPTOR_HANDLE GetCPUStart(ID3D12DescriptorHeap* heap) {
#if defined(_MSC_VER) || !defined(_WIN32)
    return heap->GetCPUDescriptorHandleForHeapStart();
#else
    D3D12_CPU_DESCRIPTOR_HANDLE h = {};
    heap->GetCPUDescriptorHandleForHeapStart(&h);
    return h;
#endif
}

/// Returns the GPU descriptor handle for the start of the heap (ABI-safe).
inline D3D12_GPU_DESCRIPTOR_HANDLE GetGPUStart(ID3D12DescriptorHeap* heap) {
#if defined(_MSC_VER) || !defined(_WIN32)
    return heap->GetGPUDescriptorHandleForHeapStart();
#else
    D3D12_GPU_DESCRIPTOR_HANDLE h = {};
    heap->GetGPUDescriptorHandleForHeapStart(&h);
    return h;
#endif
}

/// Returns the descriptor heap description (ABI-safe).
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
