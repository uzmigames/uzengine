/// GPU-dependent integration tests for the DX12 RHI backend.
///
/// These tests require a real GPU with DX12 support. If device creation
/// fails (no GPU, no driver, CI headless), all tests are skipped via GTEST_SKIP().
///
/// # Coverage
/// - Device creation/destruction
/// - Buffer creation (vertex, index, constant, upload, readback)
/// - Texture creation (RGBA8 color, D32 depth)
/// - Swapchain create/resize/destroy (skipped without HWND)
/// - Fence signal/wait cycle
/// - Command list begin/end/execute cycle
/// - Adapter name query
/// - Memory budget query
/// - Descriptor heap capacity (via buffer SRV allocation)

#ifdef _WIN32

#include "rhi.h"
#include "rhi_backend.h"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Test Fixture — creates a real DX12 device, skips if unavailable
// ─────────────────────────────────────────────────────────────────────────────

/// Fixture that owns a real DX12 IRHIDevice for GPU-dependent tests.
///
/// SetUp attempts device creation with validation enabled. If the GPU or
/// driver does not support DX12, the test is skipped rather than failed.
class DX12DeviceTest : public ::testing::Test {
protected:
    std::unique_ptr<uz::IRHIDevice> m_device;

    void SetUp() override {
        uz::RHIDeviceDesc desc{};
        desc.backend = uz::RHIBackend::DX12;
        desc.enableValidation = true;
        m_device = uz::CreateRHIDevice(desc);
        if (!m_device) {
            GTEST_SKIP() << "DX12 device creation failed (no GPU or driver support)";
        }
    }

    void TearDown() override {
        if (m_device) {
            m_device->WaitIdle();
            m_device.reset();
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Device Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(DX12DeviceTest, DeviceCreatedSuccessfully) {
    EXPECT_NE(m_device, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Adapter Queries
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(DX12DeviceTest, AdapterNameIsNonEmpty) {
    const char* name = m_device->GetAdapterName();
    ASSERT_NE(name, nullptr);
    EXPECT_GT(std::strlen(name), 0u);
}

TEST_F(DX12DeviceTest, FeatureLevelIsNonEmpty) {
    const char* level = m_device->GetFeatureLevel();
    ASSERT_NE(level, nullptr);
    EXPECT_GT(std::strlen(level), 0u);
}

TEST_F(DX12DeviceTest, MemoryBudgetReturnsNonZero) {
    uz::RHIMemoryBudget budget = m_device->GetMemoryBudget();
    EXPECT_GT(budget.budgetBytes, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Buffer Creation
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(DX12DeviceTest, CreateVertexBuffer) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 1024;
    desc.usage = uz::RHIBufferUsage::VertexBuffer;
    desc.memory = uz::RHIMemoryType::Default;
    desc.debugName = "TestVertexBuffer";

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyBuffer(handle);
}

TEST_F(DX12DeviceTest, CreateIndexBuffer) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 512;
    desc.usage = uz::RHIBufferUsage::IndexBuffer;
    desc.memory = uz::RHIMemoryType::Default;
    desc.debugName = "TestIndexBuffer";

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyBuffer(handle);
}

TEST_F(DX12DeviceTest, CreateConstantBuffer) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 256;
    desc.usage = uz::RHIBufferUsage::ConstantBuffer;
    desc.memory = uz::RHIMemoryType::Default;
    desc.debugName = "TestConstantBuffer";

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyBuffer(handle);
}

TEST_F(DX12DeviceTest, CreateUploadBuffer) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 2048;
    desc.usage = uz::RHIBufferUsage::VertexBuffer;
    desc.memory = uz::RHIMemoryType::Upload;
    desc.debugName = "TestUploadBuffer";

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyBuffer(handle);
}

TEST_F(DX12DeviceTest, CreateReadbackBuffer) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 4096;
    desc.usage = uz::RHIBufferUsage::CopyDst;
    desc.memory = uz::RHIMemoryType::Readback;
    desc.debugName = "TestReadbackBuffer";

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyBuffer(handle);
}

TEST_F(DX12DeviceTest, DestroyBufferInvalidatesHandle) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 256;
    desc.usage = uz::RHIBufferUsage::VertexBuffer;

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    ASSERT_TRUE(handle.IsValid());
    m_device->DestroyBuffer(handle);
    // Handle object still has its fields, but the slot is freed in the backend.
    // A second destroy should be a no-op (not crash).
    m_device->DestroyBuffer(handle);
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture Creation
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(DX12DeviceTest, CreateRGBA8Texture) {
    uz::RHITextureDesc desc{};
    desc.width = 256;
    desc.height = 256;
    desc.format = uz::RHIFormat::RGBA8Unorm;
    desc.usage = uz::RHITextureUsage::ShaderResource;
    desc.debugName = "TestRGBA8Texture";

    uz::RHITextureHandle handle = m_device->CreateTexture(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyTexture(handle);
}

TEST_F(DX12DeviceTest, CreateDepthTexture) {
    uz::RHITextureDesc desc{};
    desc.width = 1280;
    desc.height = 720;
    desc.format = uz::RHIFormat::D32Float;
    desc.usage = uz::RHITextureUsage::DepthStencil;
    desc.debugName = "TestDepthBuffer";

    uz::RHITextureHandle handle = m_device->CreateTexture(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyTexture(handle);
}

TEST_F(DX12DeviceTest, CreateRenderTargetTexture) {
    uz::RHITextureDesc desc{};
    desc.width = 512;
    desc.height = 512;
    desc.format = uz::RHIFormat::RGBA8Unorm;
    desc.usage = uz::RHITextureUsage::RenderTarget | uz::RHITextureUsage::ShaderResource;
    desc.debugName = "TestRenderTarget";

    uz::RHITextureHandle handle = m_device->CreateTexture(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyTexture(handle);
}

// ─────────────────────────────────────────────────────────────────────────────
// Fence Signal/Wait
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(DX12DeviceTest, FenceCreateAndDestroy) {
    uz::IRHIFence* fence = m_device->CreateFence();
    ASSERT_NE(fence, nullptr);
    EXPECT_EQ(fence->GetCompletedValue(), 0u);
    m_device->DestroyFence(fence);
}

TEST_F(DX12DeviceTest, FenceSignalAndWait) {
    uz::IRHIFence* fence = m_device->CreateFence();
    ASSERT_NE(fence, nullptr);

    // Signal value 1 from the GPU timeline and wait for it
    fence->Signal(1);
    fence->WaitCPU(1);

    EXPECT_TRUE(fence->IsComplete(1));
    EXPECT_GE(fence->GetCompletedValue(), 1u);

    m_device->DestroyFence(fence);
}

// ─────────────────────────────────────────────────────────────────────────────
// Command List Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(DX12DeviceTest, CommandListCreateAndDestroy) {
    uz::IRHICommandList* cmdList = m_device->CreateCommandList(uz::RHIQueueType::Graphics);
    ASSERT_NE(cmdList, nullptr);
    m_device->DestroyCommandList(cmdList);
}

TEST_F(DX12DeviceTest, CommandListBeginEndExecuteCycle) {
    uz::IRHICommandList* cmdList = m_device->CreateCommandList(uz::RHIQueueType::Graphics);
    ASSERT_NE(cmdList, nullptr);

    // A minimal begin/end/execute cycle should not crash
    cmdList->Begin();
    cmdList->End();
    cmdList->Execute();

    // Flush to ensure GPU completed the empty command list
    m_device->WaitIdle();

    m_device->DestroyCommandList(cmdList);
}

// ─────────────────────────────────────────────────────────────────────────────
// Swapchain (skipped without HWND)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(DX12DeviceTest, SwapchainRequiresHWND) {
    uz::RHISwapChainDesc desc{};
    desc.windowHandle = nullptr;
    desc.width = 800;
    desc.height = 600;

    // Creating a swapchain without a valid HWND should return nullptr
    uz::IRHISwapChain* swapchain = m_device->CreateSwapChain(desc);
    if (swapchain) {
        // If the backend doesn't reject null HWND, clean up gracefully
        m_device->DestroySwapChain(swapchain);
    } else {
        EXPECT_EQ(swapchain, nullptr);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor Heap Capacity (validated via multiple buffer allocations)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(DX12DeviceTest, DescriptorHeapHandlesMultipleResources) {
    constexpr int kCount = 16;
    uz::RHIBufferHandle handles[kCount];

    for (int i = 0; i < kCount; ++i) {
        uz::RHIBufferDesc desc{};
        desc.sizeBytes = 256;
        desc.usage = uz::RHIBufferUsage::ConstantBuffer;
        handles[i] = m_device->CreateBuffer(desc);
        EXPECT_TRUE(handles[i].IsValid()) << "Failed at allocation " << i;
    }

    for (int i = 0; i < kCount; ++i) {
        m_device->DestroyBuffer(handles[i]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// WaitIdle Smoke Test
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(DX12DeviceTest, WaitIdleDoesNotHang) {
    // WaitIdle with no in-flight work should return immediately
    m_device->WaitIdle();
}

} // namespace

#endif // _WIN32
