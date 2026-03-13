/// Unit tests for DescriptorAllocator security fixes.
///
/// Tests the allocator's bounds checking and overflow behavior.
/// Uses a real D3D12 device to create descriptor heaps — these tests
/// are Windows-only and require a D3D12-capable GPU (or WARP).

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <gtest/gtest.h>

#include "dx12_descriptor_allocator.h"
#include "dx12_compat.h"
#include "dx12_guids.h"
#include "log.h"

using Microsoft::WRL::ComPtr;

UZ_DECLARE_LOG_CATEGORY(RHI);

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Test Fixture — creates a minimal D3D12 device + descriptor heap
// ─────────────────────────────────────────────────────────────────────────────

class DescriptorAllocatorTest : public ::testing::Test {
protected:
    static constexpr uint32_t kCapacity = 4;

    void SetUp() override {
        // Create WARP device for headless testing
        ComPtr<IDXGIFactory4> factory;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
            GTEST_SKIP() << "No DXGI factory available";
        }

        ComPtr<IDXGIAdapter> warpAdapter;
        if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)))) {
            GTEST_SKIP() << "No WARP adapter available";
        }

        if (FAILED(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                     IID_PPV_ARGS(&m_device)))) {
            GTEST_SKIP() << "Could not create D3D12 device";
        }

        // Create a small CBV_SRV_UAV heap (shader-visible)
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = kCapacity;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        if (FAILED(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_heap)))) {
            GTEST_SKIP() << "Could not create descriptor heap";
        }

        m_incrementSize = m_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        m_allocator.Init(m_heap.Get(), kCapacity, m_incrementSize);
    }

    ComPtr<ID3D12Device>          m_device;
    ComPtr<ID3D12DescriptorHeap>  m_heap;
    uint32_t                      m_incrementSize = 0;
    uz::DescriptorAllocator       m_allocator;
};

// ─────────────────────────────────────────────────────────────────────────────
// Allocate / Free Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(DescriptorAllocatorTest, Allocate_HeapFull_ReturnsInvalidIndex) {
    // Fill all slots
    for (uint32_t i = 0; i < kCapacity; ++i) {
        uint32_t idx = m_allocator.Allocate();
        EXPECT_NE(idx, uz::DescriptorAllocator::kInvalidIndex)
            << "Allocation " << i << " should succeed";
    }

    // Next allocation should fail
    uint32_t overflow = m_allocator.Allocate();
    EXPECT_EQ(overflow, uz::DescriptorAllocator::kInvalidIndex)
        << "Allocation beyond capacity should return kInvalidIndex";
}

TEST_F(DescriptorAllocatorTest, Free_InvalidIndex_NoOp) {
    uint32_t before = m_allocator.AllocatedCount();
    m_allocator.Free(uz::DescriptorAllocator::kInvalidIndex);
    uint32_t after = m_allocator.AllocatedCount();
    EXPECT_EQ(before, after) << "Free(kInvalidIndex) should not change allocated count";
}

TEST_F(DescriptorAllocatorTest, Free_OutOfBounds_NoOp) {
    uint32_t before = m_allocator.AllocatedCount();
    m_allocator.Free(kCapacity + 100); // Way beyond capacity
    uint32_t after = m_allocator.AllocatedCount();
    EXPECT_EQ(before, after) << "Free(out-of-bounds) should not change allocated count";
}

TEST_F(DescriptorAllocatorTest, AllocateAndFree_Recycles) {
    uint32_t first = m_allocator.Allocate();
    ASSERT_NE(first, uz::DescriptorAllocator::kInvalidIndex);

    m_allocator.Free(first);
    uint32_t second = m_allocator.Allocate();
    EXPECT_EQ(first, second) << "Freed index should be recycled";
}

TEST_F(DescriptorAllocatorTest, CpuHandle_ValidIndex_ReturnsCorrectOffset) {
    uint32_t idx = m_allocator.Allocate();
    ASSERT_NE(idx, uz::DescriptorAllocator::kInvalidIndex);

    auto handle = m_allocator.CpuHandle(idx);
    auto baseHandle = m_allocator.CpuHandle(0);

    // Handle at index N should be base + N * incrementSize
    SIZE_T expected = baseHandle.ptr + static_cast<SIZE_T>(idx) * m_incrementSize;
    EXPECT_EQ(handle.ptr, expected);
}

TEST_F(DescriptorAllocatorTest, GpuHandle_ValidIndex_ReturnsCorrectOffset) {
    uint32_t idx = m_allocator.Allocate();
    ASSERT_NE(idx, uz::DescriptorAllocator::kInvalidIndex);

    auto handle = m_allocator.GpuHandle(idx);
    auto baseHandle = m_allocator.GpuHandle(0);

    UINT64 expected = baseHandle.ptr + static_cast<UINT64>(idx) * m_incrementSize;
    EXPECT_EQ(handle.ptr, expected);
}

TEST_F(DescriptorAllocatorTest, Capacity_ReportsCorrectValue) {
    EXPECT_EQ(m_allocator.Capacity(), kCapacity);
}

TEST_F(DescriptorAllocatorTest, AllocatedCount_TracksCorrectly) {
    EXPECT_EQ(m_allocator.AllocatedCount(), 0u);

    uint32_t a = m_allocator.Allocate();
    EXPECT_EQ(m_allocator.AllocatedCount(), 1u);

    uint32_t b = m_allocator.Allocate();
    EXPECT_EQ(m_allocator.AllocatedCount(), 2u);

    m_allocator.Free(a);
    EXPECT_EQ(m_allocator.AllocatedCount(), 1u);

    m_allocator.Free(b);
    EXPECT_EQ(m_allocator.AllocatedCount(), 0u);
}

} // anonymous namespace

#endif // _WIN32
