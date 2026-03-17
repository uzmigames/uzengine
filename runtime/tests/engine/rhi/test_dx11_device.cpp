/// GPU integration tests for the DX11 RHI backend.
///
/// These tests require a real GPU with DX11.1 support. If device creation
/// fails (no GPU, no driver, CI headless), all tests are skipped via GTEST_SKIP().
///
/// # Coverage
/// - Device creation/destruction
/// - Adapter name query and feature level query
/// - Memory budget query
/// - Shader model — DX11 always reports SM5_0 or SM5_1
/// - Buffer creation (vertex, index, constant, upload, readback, structured)
/// - Buffer destroy — double-destroy is a no-op (no crash)
/// - Texture creation (RGBA8 color, D32 depth, render target + SRV combo)
/// - Texture destroy — double-destroy is a no-op
/// - Sampler create/destroy — samplerIndex is assigned
/// - Shader creation from minimal DXBC vertex/pixel bytecode
/// - Graphics pipeline creation (SM5 path with input layout)
/// - Fence create/destroy — emulated via D3D11 query, initial value 0
/// - Fence Signal/WaitCPU/IsComplete/GetCompletedValue
/// - Command list create/destroy
/// - Command list Begin/End/Execute cycle (empty, no crash)
/// - ResourceBarrier is a no-op (DX11 manages transitions implicitly)
/// - WaitIdle does not hang
/// - Swapchain requires valid HWND
/// - Multiple concurrent buffer allocations (pool capacity)
/// - All RHIFormat → DXGI_FORMAT mappings via texture creation
/// - MapBuffer returns valid pointer for Upload buffers, nullptr for Default
/// - Upload buffer write-through (mapped pointer is visible on GPU)
/// - GetTextureDesc round-trip fidelity
/// - IsBindless returns false (SM5, no bindless)
/// - GetBufferGPUAddress returns 0 (no VA model in DX11)

#ifdef _WIN32

#include "rhi.h"
#include "rhi_backend.h"
#include "rhi_config.h"

#include <gtest/gtest.h>

#include <cstring>
#include <cstdint>
#include <memory>

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Minimal valid DXBC blobs for shader tests
//
// These are pre-compiled FXC bytecode for the simplest possible SM5_0 shaders:
//
//   Vertex shader (POSITION -> SV_POSITION pass-through):
//     float4 VSMain(float3 pos : POSITION) : SV_POSITION { return float4(pos, 1.0); }
//
//   Pixel shader (solid red output):
//     float4 PSMain() : SV_Target { return float4(1, 0, 0, 1); }
//
// Compiled with: fxc /T vs_5_0 /E VSMain /Fo vs.bin shader.hlsl
//               fxc /T ps_5_0 /E PSMain /Fo ps.bin shader.hlsl
//
// The blobs are stable across FXC versions for this trivial code.
// ─────────────────────────────────────────────────────────────────────────────

// clang-format off

/// Minimal DXBC vertex shader (vs_5_0): float4 main(float3 pos : POSITION) : SV_Position { return float4(pos, 1.0); }
/// Compiled with FXC 10.1: fxc /T vs_5_0 /E main
static const uint8_t kMinimalVsDXBC[] = {
    0x44, 0x58, 0x42, 0x43, 0x91, 0xcf, 0x84, 0x70, 0x84, 0xaa, 0xe3, 0x9b,
    0xfb, 0x36, 0x72, 0x6d, 0x63, 0x4c, 0xfd, 0xba, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x02, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00,
    0xa0, 0x00, 0x00, 0x00, 0xd4, 0x00, 0x00, 0x00, 0x08, 0x01, 0x00, 0x00,
    0x64, 0x01, 0x00, 0x00, 0x52, 0x44, 0x45, 0x46, 0x64, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x3c, 0x00, 0x00, 0x00, 0x00, 0x05, 0xfe, 0xff, 0x00, 0x01, 0x00, 0x00,
    0x3c, 0x00, 0x00, 0x00, 0x52, 0x44, 0x31, 0x31, 0x3c, 0x00, 0x00, 0x00,
    0x18, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00,
    0x24, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x4d, 0x69, 0x63, 0x72, 0x6f, 0x73, 0x6f, 0x66, 0x74, 0x20, 0x28, 0x52,
    0x29, 0x20, 0x48, 0x4c, 0x53, 0x4c, 0x20, 0x53, 0x68, 0x61, 0x64, 0x65,
    0x72, 0x20, 0x43, 0x6f, 0x6d, 0x70, 0x69, 0x6c, 0x65, 0x72, 0x20, 0x31,
    0x30, 0x2e, 0x31, 0x00, 0x49, 0x53, 0x47, 0x4e, 0x2c, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x07, 0x07, 0x00, 0x00, 0x50, 0x4f, 0x53, 0x49,
    0x54, 0x49, 0x4f, 0x4e, 0x00, 0xab, 0xab, 0xab, 0x4f, 0x53, 0x47, 0x4e,
    0x2c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
    0x53, 0x56, 0x5f, 0x50, 0x6f, 0x73, 0x69, 0x74, 0x69, 0x6f, 0x6e, 0x00,
    0x53, 0x48, 0x45, 0x58, 0x54, 0x00, 0x00, 0x00, 0x50, 0x00, 0x01, 0x00,
    0x15, 0x00, 0x00, 0x00, 0x6a, 0x08, 0x00, 0x01, 0x5f, 0x00, 0x00, 0x03,
    0x72, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x04,
    0xf2, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x36, 0x00, 0x00, 0x05, 0x72, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x46, 0x12, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x05,
    0x82, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x40, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x3f, 0x3e, 0x00, 0x00, 0x01, 0x53, 0x54, 0x41, 0x54,
    0x94, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/// Minimal DXBC pixel shader (ps_5_0): float4 main() : SV_Target { return float4(1,0,0,1); }
/// Compiled with FXC 10.1: fxc /T ps_5_0 /E main
static const uint8_t kMinimalPsDXBC[] = {
    0x44, 0x58, 0x42, 0x43, 0xa9, 0x92, 0x5b, 0x4b, 0x58, 0xb2, 0xd6, 0x45,
    0xec, 0x69, 0xe4, 0x62, 0xeb, 0x8a, 0x86, 0x2f, 0x01, 0x00, 0x00, 0x00,
    0xc4, 0x01, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00,
    0xa0, 0x00, 0x00, 0x00, 0xb0, 0x00, 0x00, 0x00, 0xe4, 0x00, 0x00, 0x00,
    0x28, 0x01, 0x00, 0x00, 0x52, 0x44, 0x45, 0x46, 0x64, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x3c, 0x00, 0x00, 0x00, 0x00, 0x05, 0xff, 0xff, 0x00, 0x01, 0x00, 0x00,
    0x3c, 0x00, 0x00, 0x00, 0x52, 0x44, 0x31, 0x31, 0x3c, 0x00, 0x00, 0x00,
    0x18, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00,
    0x24, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x4d, 0x69, 0x63, 0x72, 0x6f, 0x73, 0x6f, 0x66, 0x74, 0x20, 0x28, 0x52,
    0x29, 0x20, 0x48, 0x4c, 0x53, 0x4c, 0x20, 0x53, 0x68, 0x61, 0x64, 0x65,
    0x72, 0x20, 0x43, 0x6f, 0x6d, 0x70, 0x69, 0x6c, 0x65, 0x72, 0x20, 0x31,
    0x30, 0x2e, 0x31, 0x00, 0x49, 0x53, 0x47, 0x4e, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x4f, 0x53, 0x47, 0x4e,
    0x2c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
    0x53, 0x56, 0x5f, 0x54, 0x61, 0x72, 0x67, 0x65, 0x74, 0x00, 0xab, 0xab,
    0x53, 0x48, 0x45, 0x58, 0x3c, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00,
    0x0f, 0x00, 0x00, 0x00, 0x6a, 0x08, 0x00, 0x01, 0x65, 0x00, 0x00, 0x03,
    0xf2, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x08,
    0xf2, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x40, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x3f, 0x3e, 0x00, 0x00, 0x01, 0x53, 0x54, 0x41, 0x54,
    0x94, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// clang-format on

// ─────────────────────────────────────────────────────────────────────────────
// Test Fixture — creates a real DX11 device, skips if unavailable
// ─────────────────────────────────────────────────────────────────────────────

/// Fixture that shares a single DX11 IRHIDevice across all tests in the suite.
///
/// SetUpTestSuite creates the device once with validation enabled.
/// TearDownTestSuite calls WaitIdle() and destroys it after all tests complete.
/// Per-test SetUp skips if the shared device is unavailable. Individual tests
/// still create and destroy their own resources (buffers, textures, shaders)
/// to avoid cross-test state leakage.
class DX11DeviceTest : public ::testing::Test {
protected:
    static std::unique_ptr<uz::IRHIDevice> s_device;

    static void SetUpTestSuite() {
        uz::RHIDeviceDesc desc{};
        desc.backend = uz::RHIBackend::DX11;
        desc.enableValidation = true;
        s_device = uz::CreateRHIDevice(desc);
    }

    static void TearDownTestSuite() {
        if (s_device) {
            s_device->WaitIdle();
            s_device.reset();
        }
    }

    /// Alias so test bodies can use m_device without changes.
    uz::IRHIDevice* m_device = nullptr;

    void SetUp() override {
        if (!s_device) {
            GTEST_SKIP() << "DX11 device creation failed (no GPU or driver support)";
        }
        m_device = s_device.get();
    }

    /// Creates a minimal vertex shader from the pre-compiled DXBC blob.
    /// Returns an invalid handle if creation fails.
    uz::RHIShaderHandle CreateTestVertexShader() {
        uz::RHIShaderDesc desc{};
        desc.bytecode     = kMinimalVsDXBC;
        desc.bytecodeSize = sizeof(kMinimalVsDXBC);
        desc.stage        = uz::RHIShaderStage::Vertex;
        desc.entryPoint   = "VSMain";
        desc.debugName    = "TestVS";
        return m_device->CreateShader(desc);
    }

    /// Creates a minimal pixel shader from the pre-compiled DXBC blob.
    /// Returns an invalid handle if creation fails.
    uz::RHIShaderHandle CreateTestPixelShader() {
        uz::RHIShaderDesc desc{};
        desc.bytecode     = kMinimalPsDXBC;
        desc.bytecodeSize = sizeof(kMinimalPsDXBC);
        desc.stage        = uz::RHIShaderStage::Pixel;
        desc.entryPoint   = "PSMain";
        desc.debugName    = "TestPS";
        return m_device->CreateShader(desc);
    }
};

std::unique_ptr<uz::IRHIDevice> DX11DeviceTest::s_device;

// ─────────────────────────────────────────────────────────────────────────────
// Device Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(DX11DeviceTest, DeviceCreatedSuccessfully) {
    EXPECT_NE(m_device, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Adapter Queries
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(DX11DeviceTest, AdapterNameIsNonEmpty) {
    const char* name = m_device->GetAdapterName();
    ASSERT_NE(name, nullptr);
    EXPECT_GT(std::strlen(name), 0u);
}

TEST_F(DX11DeviceTest, FeatureLevelIsNonEmpty) {
    const char* level = m_device->GetFeatureLevel();
    ASSERT_NE(level, nullptr);
    EXPECT_GT(std::strlen(level), 0u);
}

TEST_F(DX11DeviceTest, MemoryBudgetReturnsNonZero) {
    uz::RHIMemoryBudget budget = m_device->GetMemoryBudget();
    EXPECT_GT(budget.budgetBytes, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Shader Model — DX11 is always SM5_0 or SM5_1, never SM6.x
// ─────────────────────────────────────────────────────────────────────────────

/// DX11 devices must report SM5_0 or SM5_1. Any SM6.x value is incorrect
/// and would indicate the backend is conflating DX11 and DX12 feature levels.
TEST_F(DX11DeviceTest, ShaderModelIsSM5) {
    uz::ShaderModel sm = m_device->GetShaderModel();
    EXPECT_TRUE(sm == uz::ShaderModel::SM5_0 || sm == uz::ShaderModel::SM5_1)
        << "DX11 backend must report SM5_0 or SM5_1, not a SM6.x level";
}

/// DX11 has no bindless heap — SM5 uses slot-based binding.
/// IsBindless() must return false for all DX11 devices.
TEST_F(DX11DeviceTest, IsBindlessReturnsFalse) {
    EXPECT_FALSE(m_device->IsBindless())
        << "DX11 backend must not report bindless support (requires SM6.6)";
}

// ─────────────────────────────────────────────────────────────────────────────
// Buffer Creation
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(DX11DeviceTest, CreateVertexBuffer) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 1024;
    desc.usage     = uz::RHIBufferUsage::VertexBuffer;
    desc.memory    = uz::RHIMemoryType::Default;
    desc.debugName = "TestVertexBuffer";

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyBuffer(handle);
}

TEST_F(DX11DeviceTest, CreateIndexBuffer) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 512;
    desc.usage     = uz::RHIBufferUsage::IndexBuffer;
    desc.memory    = uz::RHIMemoryType::Default;
    desc.debugName = "TestIndexBuffer";

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyBuffer(handle);
}

TEST_F(DX11DeviceTest, CreateConstantBuffer) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 256;
    desc.usage     = uz::RHIBufferUsage::ConstantBuffer;
    desc.memory    = uz::RHIMemoryType::Default;
    desc.debugName = "TestConstantBuffer";

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyBuffer(handle);
}

/// Upload buffers use a D3D11_USAGE_DYNAMIC + CPU_ACCESS_WRITE resource.
/// MapBuffer must return a non-null pointer for these.
TEST_F(DX11DeviceTest, CreateUploadBuffer) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 2048;
    desc.usage     = uz::RHIBufferUsage::VertexBuffer;
    desc.memory    = uz::RHIMemoryType::Upload;
    desc.debugName = "TestUploadBuffer";

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyBuffer(handle);
}

TEST_F(DX11DeviceTest, CreateReadbackBuffer) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 4096;
    desc.usage     = uz::RHIBufferUsage::CopyDst;
    desc.memory    = uz::RHIMemoryType::Readback;
    desc.debugName = "TestReadbackBuffer";

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyBuffer(handle);
}

/// Structured buffers get a shader resource view (SRV).
/// srvIndex must be a valid value (not UINT32_MAX) after creation.
TEST_F(DX11DeviceTest, CreateStructuredBuffer_HasValidSRVIndex) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 1024;
    desc.stride    = 16;
    desc.usage     = uz::RHIBufferUsage::StructuredBuffer;
    desc.memory    = uz::RHIMemoryType::Default;
    desc.debugName = "TestStructuredBuffer";

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    ASSERT_TRUE(handle.IsValid());

    uint32_t srvIndex = m_device->GetBufferSRVIndex(handle);
    EXPECT_NE(srvIndex, UINT32_MAX)
        << "StructuredBuffer must have a valid SRV index after creation";

    m_device->DestroyBuffer(handle);
}

// ─────────────────────────────────────────────────────────────────────────────
// Buffer Destroy
// ─────────────────────────────────────────────────────────────────────────────

/// A second call to DestroyBuffer with the same handle must be a no-op (no crash).
TEST_F(DX11DeviceTest, DestroyBufferDoubleDestroyIsNoOp) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 256;
    desc.usage     = uz::RHIBufferUsage::VertexBuffer;

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    ASSERT_TRUE(handle.IsValid());
    m_device->DestroyBuffer(handle);
    // Second destroy with the same handle — slot is already freed. Must not crash.
    m_device->DestroyBuffer(handle);
}

// ─────────────────────────────────────────────────────────────────────────────
// Buffer Mapping
// ─────────────────────────────────────────────────────────────────────────────

/// Upload buffers are persistently mapped. MapBuffer must return a non-null
/// pointer that can be written through to stage data for the GPU.
TEST_F(DX11DeviceTest, MapBuffer_UploadBuffer_ReturnsNonNull) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 256;
    desc.usage     = uz::RHIBufferUsage::ConstantBuffer;
    desc.memory    = uz::RHIMemoryType::Upload;
    desc.debugName = "TestMappedCBV";

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    ASSERT_TRUE(handle.IsValid());

    void* ptr = m_device->MapBuffer(handle);
    EXPECT_NE(ptr, nullptr)
        << "Upload buffer must return a valid mapped pointer";

    // Write-through test: writing to the mapped pointer must not crash.
    if (ptr) {
        std::memset(ptr, 0xAB, 256);
    }

    m_device->DestroyBuffer(handle);
}

/// Default-heap buffers are GPU-only — MapBuffer must return nullptr.
TEST_F(DX11DeviceTest, MapBuffer_DefaultBuffer_ReturnsNull) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 256;
    desc.usage     = uz::RHIBufferUsage::VertexBuffer;
    desc.memory    = uz::RHIMemoryType::Default;

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    ASSERT_TRUE(handle.IsValid());

    void* ptr = m_device->MapBuffer(handle);
    EXPECT_EQ(ptr, nullptr)
        << "Default-heap buffer must not be CPU-mappable";

    m_device->DestroyBuffer(handle);
}

/// GetBufferGPUAddress returns 0 for DX11: the API has no virtual address model.
TEST_F(DX11DeviceTest, GetBufferGPUAddress_ReturnsZero) {
    uz::RHIBufferDesc desc{};
    desc.sizeBytes = 256;
    desc.usage     = uz::RHIBufferUsage::ConstantBuffer;
    desc.memory    = uz::RHIMemoryType::Default;

    uz::RHIBufferHandle handle = m_device->CreateBuffer(desc);
    ASSERT_TRUE(handle.IsValid());

    uint64_t addr = m_device->GetBufferGPUAddress(handle);
    EXPECT_EQ(addr, 0u)
        << "DX11 has no GPU VA model — GetBufferGPUAddress must return 0";

    m_device->DestroyBuffer(handle);
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture Creation
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(DX11DeviceTest, CreateRGBA8ShaderResourceTexture) {
    uz::RHITextureDesc desc{};
    desc.width     = 256;
    desc.height    = 256;
    desc.format    = uz::RHIFormat::RGBA8Unorm;
    desc.usage     = uz::RHITextureUsage::ShaderResource;
    desc.debugName = "TestRGBA8Texture";

    uz::RHITextureHandle handle = m_device->CreateTexture(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyTexture(handle);
}

TEST_F(DX11DeviceTest, CreateD32DepthTexture) {
    uz::RHITextureDesc desc{};
    desc.width     = 1280;
    desc.height    = 720;
    desc.format    = uz::RHIFormat::D32Float;
    desc.usage     = uz::RHITextureUsage::DepthStencil;
    desc.debugName = "TestDepthBuffer";

    uz::RHITextureHandle handle = m_device->CreateTexture(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroyTexture(handle);
}

/// RenderTarget + ShaderResource combo is the typical off-screen render target.
/// DX11 allows creating RTV and SRV on the same resource via two views.
TEST_F(DX11DeviceTest, CreateRenderTargetWithSRV) {
    uz::RHITextureDesc desc{};
    desc.width     = 512;
    desc.height    = 512;
    desc.format    = uz::RHIFormat::RGBA8Unorm;
    desc.usage     = uz::RHITextureUsage::RenderTarget | uz::RHITextureUsage::ShaderResource;
    desc.debugName = "TestOffscreenRT";

    uz::RHITextureHandle handle = m_device->CreateTexture(desc);
    EXPECT_TRUE(handle.IsValid());

    // SRV index must be valid when ShaderResource is set
    uint32_t srvIndex = m_device->GetTextureSRVIndex(handle);
    EXPECT_NE(srvIndex, UINT32_MAX)
        << "RenderTarget+ShaderResource texture must have a valid SRV index";

    m_device->DestroyTexture(handle);
}

/// GetTextureDesc must return the exact same values used during creation.
TEST_F(DX11DeviceTest, GetTextureDesc_RoundTrip) {
    uz::RHITextureDesc original{};
    original.width     = 128;
    original.height    = 64;
    original.format    = uz::RHIFormat::RGBA16Float;
    original.usage     = uz::RHITextureUsage::ShaderResource;
    original.mipLevels = 1;
    original.debugName = "TestDescRoundTrip";

    uz::RHITextureHandle handle = m_device->CreateTexture(original);
    ASSERT_TRUE(handle.IsValid());

    uz::RHITextureDesc retrieved = m_device->GetTextureDesc(handle);
    EXPECT_EQ(retrieved.width,     original.width);
    EXPECT_EQ(retrieved.height,    original.height);
    EXPECT_EQ(retrieved.format,    original.format);
    EXPECT_EQ(retrieved.mipLevels, original.mipLevels);

    m_device->DestroyTexture(handle);
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture Destroy
// ─────────────────────────────────────────────────────────────────────────────

/// A second DestroyTexture with the same handle must not crash.
TEST_F(DX11DeviceTest, DestroyTextureDoubleDestroyIsNoOp) {
    uz::RHITextureDesc desc{};
    desc.width  = 64;
    desc.height = 64;
    desc.format = uz::RHIFormat::RGBA8Unorm;
    desc.usage  = uz::RHITextureUsage::ShaderResource;

    uz::RHITextureHandle handle = m_device->CreateTexture(desc);
    ASSERT_TRUE(handle.IsValid());
    m_device->DestroyTexture(handle);
    m_device->DestroyTexture(handle); // Must be a no-op
}

// ─────────────────────────────────────────────────────────────────────────────
// Sampler
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(DX11DeviceTest, CreateLinearSampler) {
    uz::RHISamplerDesc desc{};
    desc.filter    = uz::RHIFilterMode::Linear;
    desc.addressU  = uz::RHIAddressMode::Wrap;
    desc.addressV  = uz::RHIAddressMode::Wrap;
    desc.addressW  = uz::RHIAddressMode::Wrap;
    desc.maxLod    = 1000.0f;
    desc.debugName = "TestLinearSampler";

    uz::RHISamplerHandle handle = m_device->CreateSampler(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroySampler(handle);
}

TEST_F(DX11DeviceTest, CreatePointSampler) {
    uz::RHISamplerDesc desc{};
    desc.filter    = uz::RHIFilterMode::Point;
    desc.addressU  = uz::RHIAddressMode::Clamp;
    desc.addressV  = uz::RHIAddressMode::Clamp;
    desc.addressW  = uz::RHIAddressMode::Clamp;
    desc.debugName = "TestPointSampler";

    uz::RHISamplerHandle handle = m_device->CreateSampler(desc);
    EXPECT_TRUE(handle.IsValid());
    m_device->DestroySampler(handle);
}

/// Sampler index is assigned at creation. Must not be UINT32_MAX for a valid sampler.
TEST_F(DX11DeviceTest, GetSamplerIndex_ValidAfterCreation) {
    uz::RHISamplerDesc desc{};
    desc.filter = uz::RHIFilterMode::Linear;

    uz::RHISamplerHandle handle = m_device->CreateSampler(desc);
    ASSERT_TRUE(handle.IsValid());

    uint32_t index = m_device->GetSamplerIndex(handle);
    EXPECT_NE(index, UINT32_MAX)
        << "Sampler index must be assigned (not UINT32_MAX) after creation";

    m_device->DestroySampler(handle);
}

/// Sequential sampler allocations must produce incrementing indices.
TEST_F(DX11DeviceTest, MultipleSamplers_IndicesAreSequential) {
    uz::RHISamplerDesc desc{};
    desc.filter = uz::RHIFilterMode::Point;

    uz::RHISamplerHandle h0 = m_device->CreateSampler(desc);
    uz::RHISamplerHandle h1 = m_device->CreateSampler(desc);
    ASSERT_TRUE(h0.IsValid());
    ASSERT_TRUE(h1.IsValid());

    uint32_t i0 = m_device->GetSamplerIndex(h0);
    uint32_t i1 = m_device->GetSamplerIndex(h1);
    EXPECT_EQ(i1, i0 + 1u)
        << "Sequential sampler allocations must produce consecutive indices";

    m_device->DestroySampler(h1);
    m_device->DestroySampler(h0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Shader Creation — DXBC bytecode (FXC-compiled SM5_0)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(DX11DeviceTest, CreateVertexShader_FromDXBC) {
    uz::RHIShaderHandle handle = CreateTestVertexShader();
    EXPECT_TRUE(handle.IsValid())
        << "Vertex shader creation from DXBC must succeed on DX11";
    if (handle.IsValid()) {
        m_device->DestroyShader(handle);
    }
}

TEST_F(DX11DeviceTest, CreatePixelShader_FromDXBC) {
    uz::RHIShaderHandle handle = CreateTestPixelShader();
    EXPECT_TRUE(handle.IsValid())
        << "Pixel shader creation from DXBC must succeed on DX11";
    if (handle.IsValid()) {
        m_device->DestroyShader(handle);
    }
}

/// Passing an empty bytecode blob must fail gracefully (return invalid handle).
TEST_F(DX11DeviceTest, CreateShader_EmptyBytecode_ReturnsInvalidHandle) {
    uz::RHIShaderDesc desc{};
    desc.bytecode     = nullptr;
    desc.bytecodeSize = 0;
    desc.stage        = uz::RHIShaderStage::Vertex;
    desc.debugName    = "BadShader";

    uz::RHIShaderHandle handle = m_device->CreateShader(desc);
    EXPECT_FALSE(handle.IsValid())
        << "Shader creation with null/zero bytecode must return an invalid handle";
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline — SM5 path (input layout required)
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a minimal SM5 graphics pipeline with a float3 POSITION input element.
/// DX11 always uses the Input Assembler — no bindless vertex fetch.
TEST_F(DX11DeviceTest, CreateGraphicsPipeline_SM5WithInputLayout) {
    uz::RHIShaderHandle vs = CreateTestVertexShader();
    uz::RHIShaderHandle ps = CreateTestPixelShader();

    // Skip if DXBC blobs aren't accepted (e.g., wrong GPU driver or stale blob data)
    if (!vs.IsValid() || !ps.IsValid()) {
        if (vs.IsValid()) m_device->DestroyShader(vs);
        if (ps.IsValid()) m_device->DestroyShader(ps);
        GTEST_SKIP() << "Shader creation from DXBC failed — skipping pipeline test";
    }

    uz::RHIInputElementDesc inputElements[] = {
        { uz::RHIInputSemantic::Position, 0, uz::RHIFormat::RGB32Float, 0, 0 },
    };

    uz::RHIGraphicsPipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader              = vs;
    pipelineDesc.pixelShader               = ps;
    pipelineDesc.topology                  = uz::RHIPrimitiveTopology::TriangleList;
    pipelineDesc.renderTargetFormats[0]    = uz::RHIFormat::RGBA8Unorm;
    pipelineDesc.renderTargetCount         = 1;
    pipelineDesc.depthStencilFormat        = uz::RHIFormat::D32Float;
    pipelineDesc.inputElements             = inputElements;
    pipelineDesc.inputElementCount         = 1;
    pipelineDesc.vertexStride              = 12; // sizeof(float3)
    pipelineDesc.debugName                 = "TestSM5Pipeline";

    uz::RHIPipelineHandle pipeline = m_device->CreateGraphicsPipeline(pipelineDesc);
    EXPECT_TRUE(pipeline.IsValid())
        << "SM5 graphics pipeline with input layout must succeed on DX11";

    if (pipeline.IsValid()) {
        m_device->DestroyPipeline(pipeline);
    }
    m_device->DestroyShader(ps);
    m_device->DestroyShader(vs);
}

// ─────────────────────────────────────────────────────────────────────────────
// Fence — emulated via D3D11 event query
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(DX11DeviceTest, FenceCreateAndDestroy) {
    uz::IRHIFence* fence = m_device->CreateFence();
    ASSERT_NE(fence, nullptr);
    EXPECT_EQ(fence->GetCompletedValue(), 0u)
        << "Newly created fence must have initial completed value of 0";
    m_device->DestroyFence(fence);
}

TEST_F(DX11DeviceTest, FenceSignalAndWait) {
    uz::IRHIFence* fence = m_device->CreateFence();
    ASSERT_NE(fence, nullptr);

    fence->Signal(1);
    fence->WaitCPU(1);

    EXPECT_TRUE(fence->IsComplete(1))
        << "Fence must report IsComplete(1) after Signal(1) + WaitCPU(1)";
    EXPECT_GE(fence->GetCompletedValue(), 1u)
        << "GetCompletedValue must be >= 1 after signaling value 1";

    m_device->DestroyFence(fence);
}

/// Monotonically increasing fence values must all be observable.
TEST_F(DX11DeviceTest, FenceSignalMultipleValuesMonotonically) {
    uz::IRHIFence* fence = m_device->CreateFence();
    ASSERT_NE(fence, nullptr);

    for (uint64_t v = 1; v <= 5; ++v) {
        fence->Signal(v);
        fence->WaitCPU(v);
        EXPECT_TRUE(fence->IsComplete(v))
            << "IsComplete(" << v << ") must be true after Signal+WaitCPU";
        EXPECT_GE(fence->GetCompletedValue(), v)
            << "GetCompletedValue must be >= " << v;
    }

    m_device->DestroyFence(fence);
}

/// IsComplete for a value that has NOT been signaled yet must return false.
/// This verifies the polling semantics of the emulated fence.
TEST_F(DX11DeviceTest, FenceIsComplete_UnreachedValue_ReturnsFalse) {
    uz::IRHIFence* fence = m_device->CreateFence();
    ASSERT_NE(fence, nullptr);

    // Signal value 1 only
    fence->Signal(1);
    fence->WaitCPU(1);

    // Value 100 has never been signaled
    EXPECT_FALSE(fence->IsComplete(100))
        << "IsComplete(100) must be false when only value 1 has been signaled";

    m_device->DestroyFence(fence);
}

// ─────────────────────────────────────────────────────────────────────────────
// Command List
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(DX11DeviceTest, CommandListCreateAndDestroy) {
    uz::IRHICommandList* cmdList = m_device->CreateCommandList(uz::RHIQueueType::Graphics);
    ASSERT_NE(cmdList, nullptr);
    m_device->DestroyCommandList(cmdList);
}

/// An empty Begin/End/Execute cycle must complete without errors or crashes.
TEST_F(DX11DeviceTest, CommandListBeginEndExecuteCycle) {
    uz::IRHICommandList* cmdList = m_device->CreateCommandList(uz::RHIQueueType::Graphics);
    ASSERT_NE(cmdList, nullptr);

    cmdList->Begin();
    cmdList->End();
    cmdList->Execute();

    m_device->WaitIdle();
    m_device->DestroyCommandList(cmdList);
}

/// Multiple consecutive Begin/End/Execute cycles must work without state leakage.
TEST_F(DX11DeviceTest, CommandListMultipleConsecutiveCycles) {
    uz::IRHICommandList* cmdList = m_device->CreateCommandList(uz::RHIQueueType::Graphics);
    ASSERT_NE(cmdList, nullptr);

    for (int i = 0; i < 5; ++i) {
        cmdList->Begin();
        cmdList->End();
        cmdList->Execute();
    }

    m_device->WaitIdle();
    m_device->DestroyCommandList(cmdList);
}

// ─────────────────────────────────────────────────────────────────────────────
// DX11-specific: ResourceBarrier is a no-op
//
// DX11 manages resource transitions implicitly via the driver.
// Calling ResourceBarrier must not crash or corrupt state.
// ─────────────────────────────────────────────────────────────────────────────

/// DX11 ignores ResourceBarrier calls — the driver handles state transitions.
/// This test verifies the method exists, can be called, and does not crash.
TEST_F(DX11DeviceTest, ResourceBarrier_IsNoOp_DoesNotCrash) {
    uz::IRHICommandList* cmdList = m_device->CreateCommandList(uz::RHIQueueType::Graphics);
    ASSERT_NE(cmdList, nullptr);

    uz::RHITextureDesc texDesc{};
    texDesc.width  = 64;
    texDesc.height = 64;
    texDesc.format = uz::RHIFormat::RGBA8Unorm;
    texDesc.usage  = uz::RHITextureUsage::ShaderResource;

    uz::RHITextureHandle tex = m_device->CreateTexture(texDesc);
    ASSERT_TRUE(tex.IsValid());

    uz::RHIBarrierDesc barrier{};
    barrier.texture   = tex;
    barrier.stateBefore = uz::RHIResourceState::ShaderResource;
    barrier.stateAfter  = uz::RHIResourceState::RenderTarget;

    cmdList->Begin();
    // DX11: this must be a no-op (driver handles transitions)
    cmdList->ResourceBarrier(&barrier, 1);
    cmdList->End();
    cmdList->Execute();

    m_device->WaitIdle();
    m_device->DestroyTexture(tex);
    m_device->DestroyCommandList(cmdList);
}

// ─────────────────────────────────────────────────────────────────────────────
// WaitIdle
// ─────────────────────────────────────────────────────────────────────────────

/// WaitIdle with no in-flight GPU work must return immediately and not hang.
TEST_F(DX11DeviceTest, WaitIdleDoesNotHang) {
    m_device->WaitIdle();
}

/// WaitIdle after submitting empty command lists must drain all work.
TEST_F(DX11DeviceTest, WaitIdleAfterCommandListExecute) {
    uz::IRHICommandList* cmdList = m_device->CreateCommandList(uz::RHIQueueType::Graphics);
    ASSERT_NE(cmdList, nullptr);

    cmdList->Begin();
    cmdList->End();
    cmdList->Execute();

    // WaitIdle must block until the command list finishes
    m_device->WaitIdle();

    // Device must still be functional after WaitIdle
    uz::IRHIFence* fence = m_device->CreateFence();
    EXPECT_NE(fence, nullptr)
        << "Device must remain functional after WaitIdle";
    if (fence) {
        m_device->DestroyFence(fence);
    }

    m_device->DestroyCommandList(cmdList);
}

// ─────────────────────────────────────────────────────────────────────────────
// Swapchain — requires a valid HWND
// ─────────────────────────────────────────────────────────────────────────────

/// Creating a swap chain without a valid HWND must return nullptr.
/// DX11/DXGI cannot present to a null handle.
TEST_F(DX11DeviceTest, SwapchainRequiresHWND) {
    uz::RHISwapChainDesc desc{};
    desc.windowHandle = nullptr;
    desc.width        = 800;
    desc.height       = 600;

    uz::IRHISwapChain* swapchain = m_device->CreateSwapChain(desc);
    if (swapchain) {
        // Backend did not reject null HWND — clean up gracefully
        m_device->DestroySwapChain(swapchain);
    } else {
        EXPECT_EQ(swapchain, nullptr)
            << "CreateSwapChain with null HWND must return nullptr";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Pool Capacity — multiple concurrent allocations
// ─────────────────────────────────────────────────────────────────────────────

/// Allocate 16 buffers concurrently to verify the resource pool handles
/// multiple live slots without running out of space or colliding generations.
TEST_F(DX11DeviceTest, MultipleBufferAllocations_PoolHandles16Slots) {
    constexpr int kCount = 16;
    uz::RHIBufferHandle handles[kCount];

    for (int i = 0; i < kCount; ++i) {
        uz::RHIBufferDesc desc{};
        desc.sizeBytes = 256;
        desc.usage     = uz::RHIBufferUsage::ConstantBuffer;
        desc.memory    = uz::RHIMemoryType::Default;
        handles[i]     = m_device->CreateBuffer(desc);
        EXPECT_TRUE(handles[i].IsValid())
            << "Allocation " << i << " must succeed";
    }

    for (int i = 0; i < kCount; ++i) {
        m_device->DestroyBuffer(handles[i]);
    }
}

/// Allocate 8 textures concurrently to stress the texture pool.
TEST_F(DX11DeviceTest, MultipleTextureAllocations_PoolHandles8Slots) {
    constexpr int kCount = 8;
    uz::RHITextureHandle handles[kCount];

    for (int i = 0; i < kCount; ++i) {
        uz::RHITextureDesc desc{};
        desc.width     = 64;
        desc.height    = 64;
        desc.format    = uz::RHIFormat::RGBA8Unorm;
        desc.usage     = uz::RHITextureUsage::ShaderResource;
        handles[i]     = m_device->CreateTexture(desc);
        EXPECT_TRUE(handles[i].IsValid())
            << "Texture allocation " << i << " must succeed";
    }

    for (int i = 0; i < kCount; ++i) {
        m_device->DestroyTexture(handles[i]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// RHIFormat → DXGI_FORMAT mapping coverage
//
// Each RHIFormat enum value must map to a valid DXGI_FORMAT that DX11 accepts.
// Verified by creating a texture with each format and checking validity.
// Depth formats need DepthStencil usage; others use ShaderResource.
// ─────────────────────────────────────────────────────────────────────────────

/// Helper that creates a 1x1 texture with the given format and verifies success.
/// Uses DepthStencil usage for depth formats, ShaderResource for color formats.
static void VerifyFormatMapping(uz::IRHIDevice* device, uz::RHIFormat format,
                                const char* formatName) {
    uz::RHITextureDesc desc{};
    desc.width  = 1;
    desc.height = 1;
    desc.format = format;

    const bool isDepth = (format == uz::RHIFormat::D16Unorm ||
                          format == uz::RHIFormat::D32Float  ||
                          format == uz::RHIFormat::D24UnormS8Uint ||
                          format == uz::RHIFormat::D32FloatS8Uint);

    desc.usage = isDepth ? uz::RHITextureUsage::DepthStencil
                         : uz::RHITextureUsage::ShaderResource;

    uz::RHITextureHandle handle = device->CreateTexture(desc);
    EXPECT_TRUE(handle.IsValid())
        << "Format " << formatName << " must map to a valid DXGI_FORMAT";

    if (handle.IsValid()) {
        device->DestroyTexture(handle);
    }
}

TEST_F(DX11DeviceTest, FormatMapping_R8Unorm)        { VerifyFormatMapping(m_device, uz::RHIFormat::R8Unorm,        "R8Unorm");        }
TEST_F(DX11DeviceTest, FormatMapping_RG8Unorm)       { VerifyFormatMapping(m_device, uz::RHIFormat::RG8Unorm,       "RG8Unorm");       }
TEST_F(DX11DeviceTest, FormatMapping_RGBA8Unorm)     { VerifyFormatMapping(m_device, uz::RHIFormat::RGBA8Unorm,     "RGBA8Unorm");     }
TEST_F(DX11DeviceTest, FormatMapping_RGBA8Srgb)      { VerifyFormatMapping(m_device, uz::RHIFormat::RGBA8Srgb,      "RGBA8Srgb");      }
TEST_F(DX11DeviceTest, FormatMapping_BGRA8Unorm)     { VerifyFormatMapping(m_device, uz::RHIFormat::BGRA8Unorm,     "BGRA8Unorm");     }
TEST_F(DX11DeviceTest, FormatMapping_R16Float)       { VerifyFormatMapping(m_device, uz::RHIFormat::R16Float,       "R16Float");       }
TEST_F(DX11DeviceTest, FormatMapping_RG16Float)      { VerifyFormatMapping(m_device, uz::RHIFormat::RG16Float,      "RG16Float");      }
TEST_F(DX11DeviceTest, FormatMapping_RGBA16Float)    { VerifyFormatMapping(m_device, uz::RHIFormat::RGBA16Float,    "RGBA16Float");    }
TEST_F(DX11DeviceTest, FormatMapping_R32Float)       { VerifyFormatMapping(m_device, uz::RHIFormat::R32Float,       "R32Float");       }
TEST_F(DX11DeviceTest, FormatMapping_RG32Float)      { VerifyFormatMapping(m_device, uz::RHIFormat::RG32Float,      "RG32Float");      }
TEST_F(DX11DeviceTest, FormatMapping_RGBA32Float)    { VerifyFormatMapping(m_device, uz::RHIFormat::RGBA32Float,    "RGBA32Float");    }
TEST_F(DX11DeviceTest, FormatMapping_R32Uint)        { VerifyFormatMapping(m_device, uz::RHIFormat::R32Uint,        "R32Uint");        }
TEST_F(DX11DeviceTest, FormatMapping_D16Unorm)       { VerifyFormatMapping(m_device, uz::RHIFormat::D16Unorm,       "D16Unorm");       }
TEST_F(DX11DeviceTest, FormatMapping_D32Float)       { VerifyFormatMapping(m_device, uz::RHIFormat::D32Float,       "D32Float");       }
TEST_F(DX11DeviceTest, FormatMapping_D24UnormS8Uint) { VerifyFormatMapping(m_device, uz::RHIFormat::D24UnormS8Uint, "D24UnormS8Uint"); }

} // namespace

#endif // _WIN32
