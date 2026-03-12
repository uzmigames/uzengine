/// DX12 Descriptor Allocator Implementation
///
/// O(1) free-list allocator for descriptor heap slots (CBV_SRV_UAV, Sampler, RTV, DSV).
/// Tracks CPU and GPU descriptor handles separately.
/// Supports reserved slots for future allocation and reuses freed indices.

#include "dx12_descriptor_allocator.h"
#include "dx12_compat.h"
#include "dx12_guids.h"
#include "log.h"
#include "profiler.h"

namespace uz {

/// Initializes allocator with descriptor heap and capacity.
///
/// # Arguments
/// * `heap` - ID3D12DescriptorHeap to allocate from
/// * `capacity` - Total descriptor slots available
/// * `incrementSize` - Size of each descriptor (varies by heap type)
/// * `reservedCount` - Slots reserved at start (allocations start after this)
///
/// # Behavior
/// - Caches CPU and GPU handles from heap for O(1) offset calculations
/// - GPU handle only cached if heap is shader-visible
void DescriptorAllocator::Init(ID3D12DescriptorHeap* heap, uint32_t capacity,
                                uint32_t incrementSize, uint32_t reservedCount) {
    UZ_PROFILE_FUNCTION();
    m_heap = heap;
    m_capacity = capacity;
    m_incrementSize = incrementSize;
    m_nextIndex = reservedCount;
    m_allocated = 0;

    m_cpuBase = dx12::GetCPUStart(heap);

    // GPU handle is only valid for shader-visible heaps
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = dx12::GetHeapDesc(heap);
    if (heapDesc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
        m_gpuBase = dx12::GetGPUStart(heap);
    } else {
        m_gpuBase = {};
    }

    m_freeList.reserve(256);
}

// ─────────────────────────────────────────────────────────────────────────────
// Allocate / Free
// ─────────────────────────────────────────────────────────────────────────────

uint32_t DescriptorAllocator::Allocate() {
    uint32_t index;

    if (!m_freeList.empty()) {
        // Recycle a freed index
        index = m_freeList.back();
        m_freeList.pop_back();
    } else if (m_nextIndex < m_capacity) {
        // Allocate from high-water mark
        index = m_nextIndex++;
    } else {
        UZ_LOG(RHI, Error, "DescriptorAllocator: heap full (capacity={})", m_capacity);
        return kInvalidIndex;
    }

    m_allocated++;
    return index;
}

void DescriptorAllocator::Free(uint32_t index) {
    if (index == kInvalidIndex) return;

    m_freeList.push_back(index);
    m_allocated--;
}

// ─────────────────────────────────────────────────────────────────────────────
// Handle Computation
// ─────────────────────────────────────────────────────────────────────────────

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocator::CpuHandle(uint32_t index) const {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_cpuBase;
    handle.ptr += static_cast<SIZE_T>(index) * m_incrementSize;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorAllocator::GpuHandle(uint32_t index) const {
    D3D12_GPU_DESCRIPTOR_HANDLE handle = m_gpuBase;
    handle.ptr += static_cast<UINT64>(index) * m_incrementSize;
    return handle;
}

} // namespace uz
