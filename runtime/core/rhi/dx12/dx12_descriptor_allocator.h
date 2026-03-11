#pragma once

/// DX12 descriptor heap index allocator.
///
/// Manages free indices within a fixed-capacity descriptor heap.
/// Thread-safe for allocate/free (single mutex). Used for both
/// shader-visible heaps (CBV_SRV_UAV, Sampler) and CPU-only heaps
/// (RTV, DSV).
///
/// # Design
/// Uses a free-list stack for O(1) allocate/free. Indices are allocated
/// starting from 0 upward; freed indices are recycled via the stack.
/// A high-water mark tracks the furthest-ever-allocated index for
/// defragmentation diagnostics.

#include <cstdint>
#include <vector>
#include <d3d12.h>
#include <wrl/client.h>

namespace uz {

using Microsoft::WRL::ComPtr;

// ─────────────────────────────────────────────────────────────────────────────
// DescriptorAllocator
// ─────────────────────────────────────────────────────────────────────────────

/// Allocates and frees descriptor indices from a D3D12 descriptor heap.
///
/// Each allocation returns a uint32_t index into the heap. The caller
/// can compute CPU/GPU handles via:
///   cpuBase + index * incrementSize
///   gpuBase + index * incrementSize (shader-visible heaps only)
class DescriptorAllocator {
public:
    /// Sentinel value indicating allocation failure.
    static constexpr uint32_t kInvalidIndex = UINT32_MAX;

    DescriptorAllocator() = default;
    ~DescriptorAllocator() = default;

    // Non-copyable, movable
    DescriptorAllocator(const DescriptorAllocator&) = delete;
    DescriptorAllocator& operator=(const DescriptorAllocator&) = delete;
    DescriptorAllocator(DescriptorAllocator&&) = default;
    DescriptorAllocator& operator=(DescriptorAllocator&&) = default;

    /// Initialize with a descriptor heap and its capacity.
    /// reservedCount indices at the start are skipped (e.g., for swapchain RTVs).
    void Init(ID3D12DescriptorHeap* heap, uint32_t capacity,
              uint32_t incrementSize, uint32_t reservedCount = 0);

    /// Allocate a single descriptor index. Returns kInvalidIndex if full.
    uint32_t Allocate();

    /// Free a previously allocated index back to the pool.
    void Free(uint32_t index);

    /// Returns the CPU handle for a given index.
    D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle(uint32_t index) const;

    /// Returns the GPU handle for a given index (only valid for shader-visible heaps).
    D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(uint32_t index) const;

    /// Number of currently allocated descriptors.
    uint32_t AllocatedCount() const { return m_allocated; }

    /// Total capacity of the heap.
    uint32_t Capacity() const { return m_capacity; }

private:
    ID3D12DescriptorHeap*        m_heap = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE  m_cpuBase = {};
    D3D12_GPU_DESCRIPTOR_HANDLE  m_gpuBase = {};
    uint32_t                     m_incrementSize = 0;
    uint32_t                     m_capacity = 0;

    std::vector<uint32_t>        m_freeList;  ///< Stack of free indices
    uint32_t                     m_nextIndex = 0;  ///< High-water mark
    uint32_t                     m_allocated = 0;  ///< Current count
};

} // namespace uz
