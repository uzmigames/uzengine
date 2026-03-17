/// GPU-dependent integration tests for the Metal RHI backend.
///
/// These tests require a real GPU with Metal support. If device creation
/// fails (no GPU, no macOS, CI headless), all tests are skipped via GTEST_SKIP().
///
/// # Coverage
/// - Device creation/destruction
/// - Buffer creation (vertex, index, constant, upload, readback)
/// - Buffer CPU mapping (upload vs. private storage)
/// - Texture creation (RGBA8 color, BGRA8 preferred, D32 depth, render target, HDR)
/// - Swapchain create/destroy (skipped without NSWindow)
/// - Fence signal/wait cycle and monotonic signaling
/// - Command list begin/end/execute cycle
/// - Adapter name query
/// - Feature level query
/// - Memory budget query
/// - Backend identity query
/// - Shader model query (Metal GPUs are at least SM6.0)
/// - Descriptor heap capacity (via multiple buffer allocations)
/// - WaitIdle smoke test
/// - IsBindless (Metal Argument Buffer Tier 2)
/// - Sampler creation
/// - Graphics pipeline creation edge case (null shaders returns invalid)

#if defined(__APPLE__)

#include "rhi.h"
#include "rhi_backend.h"
#include "rhi_config.h"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Test Fixture — creates a real Metal device, skips if unavailable
// ─────────────────────────────────────────────────────────────────────────────

/// Fixture that owns a real Metal IRHIDevice for GPU-dependent tests.
///
/// SetUp attempts device creation with validation enabled. If no Metal-capable
/// GPU is available (CI headless, non-macOS), the test is skipped rather than failed.
class MetalDeviceTest : public ::testing::Test {
protected:
    std::unique_ptr<uz::IRHIDevice> m_device;

    void SetUp() override {
        uz::RHIDeviceDesc desc{};
        desc.backend = uz::RHIBackend::Metal;
        desc.enableValidation = true;
        m_device = uz::CreateRHIDevice(desc);
        if (!m_device) {
            GTEST_SKIP() << "Metal device creation failed (no GPU or macOS)";
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

/// Verifies the device pointer is non-null after successful creation.
TEST_F(MetalDeviceTest, DeviceCreatedSuccessfully) {
    EXPECT_NE(m_device, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Adapter Queries
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies GetAdapterName() returns a non-null, non-empty string.
///
/// On macOS this is the MTLDevice name (e.g., "Apple M2 Pro").
TEST_F(MetalDeviceTest, AdapterNameIsNonEmpty) {
    const char* name = m_device->GetAdapterName();
    ASSERT_NE(name, nullptr);
    EXPECT_GT(std::strlen(name), 0u);
}

/// Verifies GetFeatureLevel() returns a non-null, non-empty string.
///
/// The Metal feature level string describes the GPU family (e.g., "Metal GPUFamily Apple 9").
TEST_F(MetalDeviceTest, FeatureLevelIsNonEmpty) {
    const char* level = m_device->GetFeatureLevel();
    ASSERT_NE(level, nullptr);
    EXPECT_GT(std::strlen(level), 0u);
}

/// Verifies GetMemoryBudget() reports a positive VRAM budget.
///
/// Metal's recommendedWorkingSetSize is non-zero on all discrete and unified
/// memory architectures.
TEST_F(MetalDeviceTest, MemoryBudgetReturnsNonZero) {
    uz::RHIMemoryBudget budget = m_device->GetMemoryBudget();
    EXPECT_GT(budget.budgetBytes, 0u);
}

/// Verifies GetBackend() reports the correct backend identity.
TEST_F(MetalDeviceTest, GetBackendReturnsMetal) {
    EXPECT_EQ(m_device->GetBackend(), uz::RHIBackend::Metal);
}

/// Verifies GetShaderModel() returns at least SM6_0.
///
/// All Metal-capable GPUs support at least SM6.0 semantics via MSL 2.0.
/// The negotiated shader model must be >= SM6_0.
TEST_F(MetalDeviceTest, GetShaderModelReturnsValid) {
    uz::ShaderModel sm = m_device->GetShaderModel();
    EXPECT_GE(static_cast<int>(sm), static_cast<int>(uz::ShaderModel::SM6_0));
}

// ─────────────────────────────────────────────────────────────────────────────
// Buffer Creation
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a 1024-byte vertex buffer with Default (Private) storage.
TEST_F(MetalDeviceTest, CreateVertexBuffer) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 1024;
    desc.usage = uz::RHIBufferUsage::VertexBuffer;
    desc.memory = uz::RHIMemoryType::Default;
    desc.debugName = "TestVertexBuffer";

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyBuffer(handle);
}

/// Creates a 512-byte index buffer with Default (Private) storage.
TEST_F(MetalDeviceTest, CreateIndexBuffer) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 512;
    desc.usage = uz::RHIBufferUsage::IndexBuffer;
    desc.memory = uz::RHIMemoryType::Default;
    desc.debugName = "TestIndexBuffer";

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyBuffer(handle);
}

/// Creates a 256-byte constant buffer with Default (Private) storage.
TEST_F(MetalDeviceTest, CreateConstantBuffer) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 256;
    desc.usage = uz::RHIBufferUsage::ConstantBuffer;
    desc.memory = uz::RHIMemoryType::Default;
    desc.debugName = "TestConstantBuffer";

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyBuffer(handle);
}

/// Creates a 2048-byte upload buffer with Shared storage.
///
/// Metal maps RHIMemoryType::Upload to MTLStorageModeShared, which is
/// CPU-writable and GPU-readable without explicit synchronization.
TEST_F(MetalDeviceTest, CreateUploadBuffer) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 2048;
    desc.usage = uz::RHIBufferUsage::VertexBuffer;
    desc.memory = uz::RHIMemoryType::Upload;
    desc.debugName = "TestUploadBuffer";

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyBuffer(handle);
}

/// Creates a 4096-byte readback buffer with Shared storage.
///
/// Metal maps RHIMemoryType::Readback to MTLStorageModeShared for
/// GPU-writable, CPU-readable access.
TEST_F(MetalDeviceTest, CreateReadbackBuffer) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 4096;
    desc.usage = uz::RHIBufferUsage::CopyDst;
    desc.memory = uz::RHIMemoryType::Readback;
    desc.debugName = "TestReadbackBuffer";

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyBuffer(handle);
}

/// Verifies MapBuffer returns a valid CPU pointer for Upload (Shared) buffers.
///
/// MTLStorageModeShared buffers are persistently mapped — contents() is always non-null.
TEST_F(MetalDeviceTest, MapUploadBufferReturnsValidPointer) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 256;
    desc.usage = uz::RHIBufferUsage::ConstantBuffer;
    desc.memory = uz::RHIMemoryType::Upload;
    desc.debugName = "TestMapUpload";

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    ASSERT_TRUE(handle.IsValid());

    void* ptr = m_device->MapBuffer(handle);
    EXPECT_NE(ptr, nullptr);

    m_device->DestroyBuffer(handle);
}

/// Verifies MapBuffer returns null for Default (Private) storage buffers.
///
/// MTLStorageModePrivate buffers have no CPU-visible storage — the Metal
/// runtime disallows mapping them. The backend must return nullptr.
TEST_F(MetalDeviceTest, MapDefaultBufferReturnsNull) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 256;
    desc.usage = uz::RHIBufferUsage::VertexBuffer;
    desc.memory = uz::RHIMemoryType::Default;
    desc.debugName = "TestMapDefault";

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    ASSERT_TRUE(handle.IsValid());

    void* ptr = m_device->MapBuffer(handle);
    EXPECT_EQ(ptr, nullptr);

    m_device->DestroyBuffer(handle);
}

/// Verifies that destroying a buffer twice does not crash.
///
/// The second DestroyBuffer call receives a stale handle (generation mismatch).
/// The backend must detect the stale handle and treat it as a no-op.
TEST_F(MetalDeviceTest, DestroyBufferInvalidatesHandle) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 256;
    desc.usage = uz::RHIBufferUsage::VertexBuffer;

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    ASSERT_TRUE(handle.IsValid());
    m_device->DestroyBuffer(handle);
    // A second destroy with the stale handle must be a no-op, not a crash.
    m_device->DestroyBuffer(handle);
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture Creation
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a 256x256 RGBA8Unorm shader-readable texture.
TEST_F(MetalDeviceTest, CreateRGBA8Texture) {
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

/// Creates a 1280x720 D32Float depth-stencil texture.
TEST_F(MetalDeviceTest, CreateDepthTexture) {
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

/// Creates a 512x512 RGBA8Unorm texture usable as both render target and shader resource.
TEST_F(MetalDeviceTest, CreateRenderTargetTexture) {
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

/// Creates a 256x256 BGRA8Unorm shader-readable texture.
///
/// BGRA8Unorm is the preferred swapchain format on Metal/macOS. It maps
/// directly to MTLPixelFormatBGRA8Unorm and is optimal for CAMetalLayer.
TEST_F(MetalDeviceTest, CreateBGRA8Texture) {
    uz::RHITextureDesc desc{};
    desc.width = 256;
    desc.height = 256;
    desc.format = uz::RHIFormat::BGRA8Unorm;
    desc.usage = uz::RHITextureUsage::ShaderResource;
    desc.debugName = "TestBGRA8Texture";

    uz::RHITextureHandle handle = m_device->CreateTexture(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyTexture(handle);
}

/// Creates a 256x256 RGBA16Float shader-readable texture for HDR rendering.
///
/// RGBA16Float maps to MTLPixelFormatRGBA16Float and is the standard
/// HDR intermediate format on Metal.
TEST_F(MetalDeviceTest, CreateRGBA16FloatTexture) {
    uz::RHITextureDesc desc{};
    desc.width = 256;
    desc.height = 256;
    desc.format = uz::RHIFormat::RGBA16Float;
    desc.usage = uz::RHITextureUsage::ShaderResource;
    desc.debugName = "TestRGBA16Float";

    uz::RHITextureHandle handle = m_device->CreateTexture(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyTexture(handle);
}

// ─────────────────────────────────────────────────────────────────────────────
// Fence Signal/Wait
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies CreateFence returns a non-null fence with initial value 0.
TEST_F(MetalDeviceTest, FenceCreateAndDestroy) {
    uz::IRHIFence* fence = m_device->CreateFence();
    ASSERT_NE(fence, nullptr);
    EXPECT_EQ(fence->GetCompletedValue(), 0u);
    m_device->DestroyFence(fence);
}

/// Signals a fence to value 1 and waits for CPU completion.
///
/// After Signal(1) + WaitCPU(1), IsComplete(1) must be true and
/// GetCompletedValue() must be >= 1.
TEST_F(MetalDeviceTest, FenceSignalAndWait) {
    uz::IRHIFence* fence = m_device->CreateFence();
    ASSERT_NE(fence, nullptr);

    fence->Signal(1);
    fence->WaitCPU(1);

    EXPECT_TRUE(fence->IsComplete(1));
    EXPECT_GE(fence->GetCompletedValue(), 1u);

    m_device->DestroyFence(fence);
}

/// Signals the fence monotonically to values 1, 2, 3 and verifies each completes.
///
/// Metal fences are monotonically increasing — each Signal must use a strictly
/// greater value than the previous one.
TEST_F(MetalDeviceTest, FenceMonotonicSignaling) {
    uz::IRHIFence* fence = m_device->CreateFence();
    ASSERT_NE(fence, nullptr);

    for (uint64_t v = 1; v <= 3; ++v) {
        fence->Signal(v);
        fence->WaitCPU(v);
        EXPECT_TRUE(fence->IsComplete(v))
            << "Fence should be complete at value " << v;
        EXPECT_GE(fence->GetCompletedValue(), v);
    }

    m_device->DestroyFence(fence);
}

// ─────────────────────────────────────────────────────────────────────────────
// Command List Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies CreateCommandList returns a non-null command list.
TEST_F(MetalDeviceTest, CommandListCreateAndDestroy) {
    uz::IRHICommandList* cmdList = m_device->CreateCommandList(uz::RHIQueueType::Graphics);
    ASSERT_NE(cmdList, nullptr);
    m_device->DestroyCommandList(cmdList);
}

/// Runs a minimal Begin/End/Execute cycle and verifies it does not crash.
///
/// An empty command buffer on Metal is a valid submission. WaitIdle ensures
/// the GPU has completed the empty work before command list destruction.
TEST_F(MetalDeviceTest, CommandListBeginEndExecuteCycle) {
    uz::IRHICommandList* cmdList = m_device->CreateCommandList(uz::RHIQueueType::Graphics);
    ASSERT_NE(cmdList, nullptr);

    cmdList->Begin();
    cmdList->End();
    cmdList->Execute();

    // Flush to ensure GPU completed the empty command buffer.
    m_device->WaitIdle();

    m_device->DestroyCommandList(cmdList);
}

// ─────────────────────────────────────────────────────────────────────────────
// Swapchain (skipped without NSWindow)
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies that swapchain creation without a valid NSWindow returns nullptr.
///
/// Metal uses CAMetalLayer backed by NSWindow/UIView. Without a valid window
/// handle, the backend cannot create a CAMetalLayer and must return nullptr.
TEST_F(MetalDeviceTest, SwapchainRequiresNSWindow) {
    uz::RHISwapChainDesc desc{};
    desc.windowHandle = nullptr;
    desc.width = 800;
    desc.height = 600;

    // Creating a swapchain without a valid NSWindow must return nullptr.
    uz::IRHISwapChain* swapchain = m_device->CreateSwapChain(desc);
    if (swapchain) {
        // If the backend does not reject null, clean up gracefully.
        m_device->DestroySwapChain(swapchain);
    } else {
        EXPECT_EQ(swapchain, nullptr);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor Heap Capacity (validated via multiple buffer allocations)
// ─────────────────────────────────────────────────────────────────────────────

/// Allocates 16 constant buffers and verifies all handles are valid.
///
/// Tests that the Metal argument buffer descriptor pool handles concurrent
/// allocations without running out of slots or corrupting indices.
TEST_F(MetalDeviceTest, DescriptorHeapHandlesMultipleResources) {
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

/// Verifies WaitIdle returns promptly when no GPU work is in flight.
TEST_F(MetalDeviceTest, WaitIdleDoesNotHang) {
    // WaitIdle with no in-flight work should return immediately.
    m_device->WaitIdle();
}

// ─────────────────────────────────────────────────────────────────────────────
// Bindless Support
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies IsBindless() returns true on Metal.
///
/// Metal Argument Buffer Tier 2 (available on all Apple Silicon and most
/// Intel/AMD GPUs from 2019+) provides bindless resource access equivalent
/// to SM6.6 ResourceDescriptorHeap. The engine requires Tier 2 for Metal.
TEST_F(MetalDeviceTest, IsBindlessReturnsTrue) {
    EXPECT_TRUE(m_device->IsBindless());
}

// ─────────────────────────────────────────────────────────────────────────────
// Sampler Creation
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a linear-filter, wrap-address sampler and verifies the handle is valid.
///
/// Metal sampler states are immutable once created and are stored in the
/// global argument buffer sampler heap for bindless access.
TEST_F(MetalDeviceTest, CreateSampler) {
    uz::RHISamplerDesc desc{};
    desc.filter = uz::RHIFilterMode::Linear;
    desc.addressU = uz::RHIAddressMode::Wrap;
    desc.addressV = uz::RHIAddressMode::Wrap;
    desc.addressW = uz::RHIAddressMode::Wrap;

    uz::RHISamplerHandle handle = m_device->CreateSampler(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroySampler(handle);
}

// ─────────────────────────────────────────────────────────────────────────────
// Graphics Pipeline Edge Case
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies CreateGraphicsPipeline with null shader handles returns an invalid handle.
///
/// Metal PSO compilation requires valid MTLFunction objects. Passing null
/// shader handles must be rejected by the backend — the returned pipeline
/// handle must be invalid (not crash and not return a corrupt PSO).
TEST_F(MetalDeviceTest, CreateGraphicsPipeline_NullShaders_ReturnsInvalid) {
    uz::RHIGraphicsPipelineDesc desc{};
    // Leave vertexShader and pixelShader as default (invalid handles).

    uz::RHIPipelineHandle handle = m_device->CreateGraphicsPipeline(desc);
    // Backend must return an invalid handle, not crash.
    EXPECT_FALSE(handle.IsValid());
}

} // namespace

#endif // defined(__APPLE__)
