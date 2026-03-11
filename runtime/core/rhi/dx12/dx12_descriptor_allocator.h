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

    /// Initializes the allocator with a descriptor heap and its capacity.
    ///
    /// # Arguments
    /// * `heap` — The D3D12 descriptor heap to allocate from (must outlive this allocator).
    /// * `capacity` — Total number of descriptor slots in the heap.
    /// * `incrementSize` — Size of each descriptor in bytes (from `GetDescriptorHandleIncrementSize`).
    /// * `reservedCount` — Indices `[0, reservedCount)` are skipped (e.g., for swap chain RTVs).
    void Init(ID3D12DescriptorHeap* heap, uint32_t capacity,
              uint32_t incrementSize, uint32_t reservedCount = 0);

    /// Allocates a single descriptor index from the heap.
    ///
    /// # Returns
    /// A valid descriptor index, or `kInvalidIndex` if the heap is full.
    [[nodiscard]] uint32_t Allocate();

    /// Frees a previously allocated index back to the pool for reuse.
    ///
    /// # Arguments
    /// * `index` — Previously allocated index. `kInvalidIndex` is a safe no-op.
    void Free(uint32_t index);

    /// Computes the CPU descriptor handle for the given index.
    ///
    /// # Arguments
    /// * `index` — Descriptor index (from `Allocate()`).
    ///
    /// # Returns
    /// `D3D12_CPU_DESCRIPTOR_HANDLE` at `cpuBase + index * incrementSize`.
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle(uint32_t index) const;

    /// Computes the GPU descriptor handle for the given index.
    ///
    /// Only valid for shader-visible heaps (CBV_SRV_UAV, Sampler).
    /// Returns a zeroed handle for non-shader-visible heaps (RTV, DSV).
    ///
    /// # Arguments
    /// * `index` — Descriptor index (from `Allocate()`).
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(uint32_t index) const;

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
