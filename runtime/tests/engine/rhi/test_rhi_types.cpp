/// Unit tests for RHI type definitions — handle validity sentinel values,
/// enum numeric stability, descriptor struct field defaults, depth convention
/// constants (reversed-Z and shadow-Z), and backend-independent type
/// size/alignment constraints.
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
    EXPECT_EQ(desc.depthStencil.depthFunc, RHICompareFunc::Greater); // Reversed-Z default
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
    EXPECT_FLOAT_EQ(desc.depthAttachment.clearDepth, kDepthFar); // Reversed-Z default (0.0)
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

// ─────────────────────────────────────────────────────────────────────────────
// Depth Convention Constants — reversed-Z (main view) and shadow-Z
//
// UzEngine uses two depth conventions side-by-side:
//
//   Main view (reversed-Z):  near=1.0, far=0.0  — kDepthNear/kDepthFar
//   Shadow maps (standard-Z): near=0.0, far=1.0  — kShadowDepthNear/kShadowDepthFar
//   Shadow map clear value:   1.0               — kShadowDepthClear
//
// Source: UE5 RHIDefinitions.h:278-287, ShadowDepthRendering.cpp:704.
// ─────────────────────────────────────────────────────────────────────────────

TEST(DepthConventionConstants, kDepthFar_IsZero) {
    // Main-view reversed-Z: the far plane is depth 0.0 — the clear target.
    EXPECT_FLOAT_EQ(kDepthFar, 0.0f)
        << "kDepthFar must be 0.0 for reversed-Z depth convention";
}

TEST(DepthConventionConstants, kDepthNear_IsOne) {
    // Main-view reversed-Z: the near plane is depth 1.0.
    EXPECT_FLOAT_EQ(kDepthNear, 1.0f)
        << "kDepthNear must be 1.0 for reversed-Z depth convention";
}

TEST(DepthConventionConstants, kShadowDepthClear_IsOne) {
    // Shadow maps use standard-Z: clearing to 1.0 represents the far plane.
    // The shadow sampler (CmpLess) compares shadow depth < scene depth, so
    // unrendered (cleared) shadow texels never occlude geometry.
    EXPECT_FLOAT_EQ(kShadowDepthClear, 1.0f)
        << "kShadowDepthClear must be 1.0 (standard-Z shadow far plane)";
}

TEST(DepthConventionConstants, kShadowDepthNear_IsZero) {
    // Shadow maps use standard-Z: the near boundary is 0.0.
    EXPECT_FLOAT_EQ(kShadowDepthNear, 0.0f)
        << "kShadowDepthNear must be 0.0 for standard-Z shadow convention";
}

TEST(DepthConventionConstants, kShadowDepthFar_IsOne) {
    // Shadow maps use standard-Z: the far boundary is 1.0.
    EXPECT_FLOAT_EQ(kShadowDepthFar, 1.0f)
        << "kShadowDepthFar must be 1.0 for standard-Z shadow convention";
}

TEST(DepthConventionConstants, kReversedZ_IsTrue) {
    // The compile-time flag must be true — the engine is wired for reversed-Z.
    static_assert(kReversedZ == true,
        "kReversedZ must be true at compile time — engine uses reversed-Z depth");
    EXPECT_TRUE(kReversedZ)
        << "kReversedZ must be true: depth test uses GreaterEqual, not Less";
}

TEST(DepthConventionConstants, ShadowClear_NotEqualTo_MainViewFar) {
    // This is the critical consistency check: the shadow clear value (1.0) and
    // the main-view far depth value (0.0) MUST differ.  If they were equal the
    // engine would be using the same depth convention for both, defeating the
    // purpose of the dual-convention design.
    EXPECT_NE(kShadowDepthClear, kDepthFar)
        << "Shadow clear value (standard-Z far=1.0) must differ from main-view "
           "far depth (reversed-Z far=0.0) — these are different conventions";
}

TEST(DepthConventionConstants, MainViewNear_NotEqualTo_MainViewFar) {
    // Sanity check: near and far must not be the same value; a zero-range
    // depth buffer would produce divide-by-zero in projection math.
    EXPECT_NE(kDepthNear, kDepthFar)
        << "kDepthNear (1.0) and kDepthFar (0.0) must be distinct values";
}

TEST(DepthConventionConstants, ShadowNear_NotEqualTo_ShadowFar) {
    // Sanity check: shadow near and far must differ for a valid frustum range.
    EXPECT_NE(kShadowDepthNear, kShadowDepthFar)
        << "kShadowDepthNear (0.0) and kShadowDepthFar (1.0) must be distinct values";
}

TEST(DepthConventionConstants, MainViewConvention_IsInverted_RelativeToShadow) {
    // The main-view near (1.0) equals the shadow far (1.0) and
    // the main-view far (0.0) equals the shadow near (0.0).
    // This confirms the conventions are exactly inverted from each other.
    EXPECT_FLOAT_EQ(kDepthNear, kShadowDepthFar)
        << "Main-view near (1.0) must equal shadow far (1.0)";
    EXPECT_FLOAT_EQ(kDepthFar, kShadowDepthNear)
        << "Main-view far (0.0) must equal shadow near (0.0)";
}

TEST(DepthConventionConstants, AllConstantsAreConstexpr) {
    // All depth constants must be usable in compile-time contexts —
    // they are used in static_assert and constexpr branches throughout
    // the renderer.
    static_assert(kDepthFar  == 0.0f, "kDepthFar must be constexpr 0.0f");
    static_assert(kDepthNear == 1.0f, "kDepthNear must be constexpr 1.0f");
    static_assert(kShadowDepthClear == 1.0f, "kShadowDepthClear must be constexpr 1.0f");
    static_assert(kShadowDepthNear  == 0.0f, "kShadowDepthNear must be constexpr 0.0f");
    static_assert(kShadowDepthFar   == 1.0f, "kShadowDepthFar must be constexpr 1.0f");

    // Runtime check to satisfy gtest output (static_assert is silent on pass).
    EXPECT_FLOAT_EQ(kDepthFar,         0.0f);
    EXPECT_FLOAT_EQ(kDepthNear,        1.0f);
    EXPECT_FLOAT_EQ(kShadowDepthClear, 1.0f);
    EXPECT_FLOAT_EQ(kShadowDepthNear,  0.0f);
    EXPECT_FLOAT_EQ(kShadowDepthFar,   1.0f);
}

TEST(DepthConventionConstants, RenderPassDepthAttachment_DefaultClearIsDepthFar) {
    // The default clear depth in RHIRenderPassDepthAttachment must match kDepthFar
    // (0.0 for the reversed-Z main view).  Shadow passes must explicitly override
    // this to kShadowDepthClear (1.0).
    RHIRenderPassDepthAttachment attachment{};
    EXPECT_FLOAT_EQ(attachment.clearDepth, kDepthFar)
        << "RHIRenderPassDepthAttachment default clearDepth must equal kDepthFar (0.0)";
}

TEST(DepthConventionConstants, RHIDepthStencilDesc_DefaultCompareFunc_IsGreater) {
    // For reversed-Z the closer-fragment wins with a GREATER depth value.
    // The pipeline default must be RHICompareFunc::Greater (not Less).
    RHIDepthStencilDesc dss{};
    EXPECT_EQ(dss.depthFunc, RHICompareFunc::Greater)
        << "Default depth compare func must be Greater for reversed-Z (closer = larger depth)";
}
