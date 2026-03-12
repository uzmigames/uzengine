/// Unit tests for RHI type definitions — handle validity sentinel values,
/// enum numeric stability, descriptor struct field defaults, and
/// backend-independent type size/alignment constraints.
#include "rhi_types.h"
#include "rhi.h"
#include "rhi_backend.h"

#include <gtest/gtest.h>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// RHIHandle Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIHandle, InvalidHasZeroGeneration) {
    auto h = RHIBufferHandle::Invalid();
    EXPECT_EQ(h.index, 0u);
    EXPECT_EQ(h.generation, 0u);
    EXPECT_FALSE(h.IsValid());
}

TEST(RHIHandle, ValidHandleHasNonZeroGeneration) {
    RHIBufferHandle h{1, 1};
    EXPECT_TRUE(h.IsValid());
}

TEST(RHIHandle, EqualityWithSameIndexAndGeneration) {
    RHIBufferHandle a{5, 3};
    RHIBufferHandle b{5, 3};
    EXPECT_EQ(a, b);
}

TEST(RHIHandle, InequalityWithDifferentGeneration) {
    RHIBufferHandle a{5, 1};
    RHIBufferHandle b{5, 2};
    EXPECT_NE(a, b);
}

TEST(RHIHandle, InequalityWithDifferentIndex) {
    RHIBufferHandle a{1, 1};
    RHIBufferHandle b{2, 1};
    EXPECT_NE(a, b);
}

TEST(RHIHandle, DifferentTagTypesAreDistinct) {
    // Compile-time check: these are different types
    static_assert(!std::is_same_v<RHIBufferHandle, RHITextureHandle>);
    static_assert(!std::is_same_v<RHITextureHandle, RHIPipelineHandle>);
    static_assert(!std::is_same_v<RHIPipelineHandle, RHIShaderHandle>);
    static_assert(!std::is_same_v<RHIShaderHandle, RHISamplerHandle>);
}

// ─────────────────────────────────────────────────────────────────────────────
// Bitwise Flag Operations
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIBufferUsage, CombineFlags) {
    auto flags = RHIBufferUsage::VertexBuffer | RHIBufferUsage::CopySrc;
    EXPECT_TRUE(HasFlag(flags, RHIBufferUsage::VertexBuffer));
    EXPECT_TRUE(HasFlag(flags, RHIBufferUsage::CopySrc));
    EXPECT_FALSE(HasFlag(flags, RHIBufferUsage::IndexBuffer));
}

TEST(RHIBufferUsage, NoneHasNoFlags) {
    EXPECT_FALSE(HasFlag(RHIBufferUsage::None, RHIBufferUsage::VertexBuffer));
    EXPECT_FALSE(HasFlag(RHIBufferUsage::None, RHIBufferUsage::IndexBuffer));
}

TEST(RHIBufferUsage, BitwiseAndFilters) {
    auto flags = RHIBufferUsage::VertexBuffer | RHIBufferUsage::IndexBuffer | RHIBufferUsage::CopySrc;
    auto masked = flags & RHIBufferUsage::VertexBuffer;
    EXPECT_TRUE(HasFlag(masked, RHIBufferUsage::VertexBuffer));
    EXPECT_FALSE(HasFlag(masked, RHIBufferUsage::IndexBuffer));
}

TEST(RHITextureUsage, CombineFlags) {
    auto flags = RHITextureUsage::ShaderResource | RHITextureUsage::RenderTarget;
    EXPECT_TRUE(HasFlag(flags, RHITextureUsage::ShaderResource));
    EXPECT_TRUE(HasFlag(flags, RHITextureUsage::RenderTarget));
    EXPECT_FALSE(HasFlag(flags, RHITextureUsage::DepthStencil));
}

TEST(RHITextureUsage, NoneHasNoFlags) {
    EXPECT_FALSE(HasFlag(RHITextureUsage::None, RHITextureUsage::ShaderResource));
}

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor Defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIBufferDesc, DefaultValues) {
    RHIBufferDesc desc{};
    EXPECT_EQ(desc.sizeBytes, 0u);
    EXPECT_EQ(desc.stride, 0u);
    EXPECT_EQ(desc.usage, RHIBufferUsage::None);
    EXPECT_EQ(desc.memory, RHIMemoryType::Default);
    EXPECT_EQ(desc.debugName, nullptr);
}

TEST(RHITextureDesc, DefaultValues) {
    RHITextureDesc desc{};
    EXPECT_EQ(desc.width, 1u);
    EXPECT_EQ(desc.height, 1u);
    EXPECT_EQ(desc.depthOrArraySize, 1u);
    EXPECT_EQ(desc.mipLevels, 1u);
    EXPECT_EQ(desc.format, RHIFormat::RGBA8Unorm);
    EXPECT_EQ(desc.usage, RHITextureUsage::ShaderResource);
    EXPECT_EQ(desc.debugName, nullptr);
}

TEST(RHISwapChainDesc, DefaultValues) {
    RHISwapChainDesc desc{};
    EXPECT_EQ(desc.windowHandle, nullptr);
    EXPECT_EQ(desc.width, 0u);
    EXPECT_EQ(desc.height, 0u);
    EXPECT_EQ(desc.bufferCount, 3u);
    EXPECT_EQ(desc.format, RHIFormat::RGBA8Unorm);
    EXPECT_TRUE(desc.vsync);
}

TEST(RHIShaderDesc, DefaultValues) {
    RHIShaderDesc desc{};
    EXPECT_EQ(desc.bytecode, nullptr);
    EXPECT_EQ(desc.bytecodeSize, 0u);
    EXPECT_EQ(desc.stage, RHIShaderStage::Vertex);
    EXPECT_STREQ(desc.entryPoint, "main");
    EXPECT_EQ(desc.debugName, nullptr);
}

TEST(RHIGraphicsPipelineDesc, DefaultValues) {
    RHIGraphicsPipelineDesc desc{};
    EXPECT_FALSE(desc.vertexShader.IsValid());
    EXPECT_FALSE(desc.pixelShader.IsValid());
    EXPECT_EQ(desc.topology, RHIPrimitiveTopology::TriangleList);
    EXPECT_EQ(desc.rasterizer.cullMode, RHICullMode::Back);
    EXPECT_EQ(desc.rasterizer.fillMode, RHIFillMode::Solid);
    EXPECT_TRUE(desc.depthStencil.depthEnable);
    EXPECT_TRUE(desc.depthStencil.depthWrite);
    EXPECT_EQ(desc.depthStencil.depthFunc, RHICompareFunc::Less);
    EXPECT_EQ(desc.renderTargetCount, 1u);
    EXPECT_EQ(desc.depthStencilFormat, RHIFormat::Unknown);
}

TEST(RHISamplerDesc, DefaultValues) {
    RHISamplerDesc desc{};
    EXPECT_EQ(desc.filter, RHIFilterMode::Linear);
    EXPECT_EQ(desc.addressU, RHIAddressMode::Wrap);
    EXPECT_EQ(desc.addressV, RHIAddressMode::Wrap);
    EXPECT_EQ(desc.addressW, RHIAddressMode::Wrap);
    EXPECT_EQ(desc.maxAnisotropy, 1u);
    EXPECT_FLOAT_EQ(desc.minLod, 0.0f);
    EXPECT_FLOAT_EQ(desc.maxLod, 1000.0f);
}

TEST(RHIViewport, DefaultValues) {
    RHIViewport vp{};
    EXPECT_FLOAT_EQ(vp.x, 0.0f);
    EXPECT_FLOAT_EQ(vp.y, 0.0f);
    EXPECT_FLOAT_EQ(vp.width, 0.0f);
    EXPECT_FLOAT_EQ(vp.height, 0.0f);
    EXPECT_FLOAT_EQ(vp.minDepth, 0.0f);
    EXPECT_FLOAT_EQ(vp.maxDepth, 1.0f);
}

TEST(RHIRenderPassDesc, DefaultValues) {
    RHIRenderPassDesc desc{};
    EXPECT_EQ(desc.colorAttachmentCount, 0u);
    EXPECT_FALSE(desc.hasDepth);
    EXPECT_FLOAT_EQ(desc.depthAttachment.clearDepth, 1.0f);
    EXPECT_EQ(desc.depthAttachment.clearStencil, 0u);
}

TEST(RHIMemoryBudget, DefaultValues) {
    RHIMemoryBudget budget{};
    EXPECT_EQ(budget.budgetBytes, 0u);
    EXPECT_EQ(budget.usageBytes, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Backend Enum & DeviceDesc
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIDeviceDesc, DefaultValues) {
    RHIDeviceDesc desc{};
    EXPECT_EQ(desc.backend, RHIBackend::DX12);
    EXPECT_FALSE(desc.enableValidation);
    EXPECT_FALSE(desc.enableGpuValidation);
    EXPECT_EQ(desc.adapterIndex, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Interface Compile-Time Checks
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIInterfaces, AreAbstract) {
    static_assert(std::is_abstract_v<IRHIFence>);
    static_assert(std::is_abstract_v<IRHICommandList>);
    static_assert(std::is_abstract_v<IRHISwapChain>);
    static_assert(std::is_abstract_v<IRHIDevice>);
}

TEST(RHIInterfaces, HaveVirtualDestructors) {
    static_assert(std::has_virtual_destructor_v<IRHIFence>);
    static_assert(std::has_virtual_destructor_v<IRHICommandList>);
    static_assert(std::has_virtual_destructor_v<IRHISwapChain>);
    static_assert(std::has_virtual_destructor_v<IRHIDevice>);
}

// ─────────────────────────────────────────────────────────────────────────────
// BytesPerTexel Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(BytesPerTexel, UnknownReturnsZero) {
    EXPECT_EQ(BytesPerTexel(RHIFormat::Unknown), 0u);
}

TEST(BytesPerTexel, SingleChannel8Bit) {
    EXPECT_EQ(BytesPerTexel(RHIFormat::R8Unorm), 1u);
}

TEST(BytesPerTexel, TwoChannel8Bit) {
    EXPECT_EQ(BytesPerTexel(RHIFormat::RG8Unorm), 2u);
}

TEST(BytesPerTexel, FourChannel8Bit) {
    EXPECT_EQ(BytesPerTexel(RHIFormat::RGBA8Unorm), 4u);
    EXPECT_EQ(BytesPerTexel(RHIFormat::RGBA8Srgb), 4u);
    EXPECT_EQ(BytesPerTexel(RHIFormat::BGRA8Unorm), 4u);
    EXPECT_EQ(BytesPerTexel(RHIFormat::BGRA8Srgb), 4u);
}

TEST(BytesPerTexel, HalfFloatFormats) {
    EXPECT_EQ(BytesPerTexel(RHIFormat::R16Float), 2u);
    EXPECT_EQ(BytesPerTexel(RHIFormat::RG16Float), 4u);
    EXPECT_EQ(BytesPerTexel(RHIFormat::RGBA16Float), 8u);
}

TEST(BytesPerTexel, FullFloatFormats) {
    EXPECT_EQ(BytesPerTexel(RHIFormat::R32Float), 4u);
    EXPECT_EQ(BytesPerTexel(RHIFormat::RG32Float), 8u);
    EXPECT_EQ(BytesPerTexel(RHIFormat::RGB32Float), 12u);
    EXPECT_EQ(BytesPerTexel(RHIFormat::RGBA32Float), 16u);
}

TEST(BytesPerTexel, UintFormats) {
    EXPECT_EQ(BytesPerTexel(RHIFormat::R32Uint), 4u);
    EXPECT_EQ(BytesPerTexel(RHIFormat::RG32Uint), 8u);
    EXPECT_EQ(BytesPerTexel(RHIFormat::RGBA32Uint), 16u);
}

TEST(BytesPerTexel, DepthFormats) {
    EXPECT_EQ(BytesPerTexel(RHIFormat::D16Unorm), 2u);
    EXPECT_EQ(BytesPerTexel(RHIFormat::D32Float), 4u);
    EXPECT_EQ(BytesPerTexel(RHIFormat::D24UnormS8Uint), 4u);
    EXPECT_EQ(BytesPerTexel(RHIFormat::D32FloatS8Uint), 8u);
}

TEST(BytesPerTexel, IsConstexpr) {
    constexpr uint32_t rgba8 = BytesPerTexel(RHIFormat::RGBA8Unorm);
    static_assert(rgba8 == 4, "RGBA8Unorm should be 4 bytes");
    constexpr uint32_t unknown = BytesPerTexel(RHIFormat::Unknown);
    static_assert(unknown == 0, "Unknown should be 0 bytes");
}
