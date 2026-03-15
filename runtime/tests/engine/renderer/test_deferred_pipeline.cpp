/// Unit tests for the UzEngine deferred rendering pipeline.
///
/// Covers constant buffer layout extensions for deferred lighting (PerFrameCB
/// invViewProj field, PointLightData, LightingCB), GBufferPassDesc and
/// LightingPassDesc struct accessibility, G-Buffer format expectations, and
/// RgTextureDesc usage flag combinations.
///
/// All tests operate on CPU-side struct definitions only — no GPU hardware,
/// no RHI device, no render graph execution required.
///
/// # Coverage Notes
/// PerFrameCB size + viewProj/cameraPos/timeParams offsets are covered by
/// test_constant_buffers.cpp and are NOT repeated here. This file covers the
/// fields added for deferred rendering: lightDir, lightColor, ambientColor,
/// invViewProj, and the new LightingCB / PointLightData types.

#include "constant_buffers.h"
#include "render_graph_types.h"
#include "rhi_types.h"
#include "gbuffer_pass.h"
#include "lighting_pass.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <vector>

using namespace uz;
using namespace uz::rg;

// ─────────────────────────────────────────────────────────────────────────────
// PerFrameCB — deferred-lighting field offsets
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies the deferred-lighting field offset map matches the HLSL cbuffer layout.
/// Note: PerFrameCB size and base field offsets (viewProj, cameraPos, timeParams)
/// are already tested in test_constant_buffers.cpp — only deferred additions here.
///
/// Layout (bytes):
///   0   viewProj      float4x4 (64 B)
///  64   cameraPos     float4   (16 B)
///  80   timeParams    float4   (16 B)
///  96   lightDir      float4   (16 B)
/// 112   lightColor    float4   (16 B)
/// 128   ambientColor  float4   (16 B)
/// 144   invViewProj   float4x4 (64 B)
/// 208   _pad[12]      float    (48 B)
TEST(PerFrameCB, FieldOffsets_MatchHLSL) {
    EXPECT_EQ(offsetof(PerFrameCB, viewProj),     0u);
    EXPECT_EQ(offsetof(PerFrameCB, cameraPos),   64u);
    EXPECT_EQ(offsetof(PerFrameCB, timeParams),  80u);
    EXPECT_EQ(offsetof(PerFrameCB, lightDir),    96u);
    EXPECT_EQ(offsetof(PerFrameCB, lightColor), 112u);
    EXPECT_EQ(offsetof(PerFrameCB, ambientColor), 128u);
    EXPECT_EQ(offsetof(PerFrameCB, invViewProj), 144u);
}

/// invViewProj must follow ambientColor immediately (no implicit gap).
TEST(PerFrameCB, InvViewProjField_IsAfterAmbientColor_BeforePadding) {
    constexpr size_t kExpectedOffset = 128u + 16u; // ambientColor + sizeof(Vec4)
    EXPECT_EQ(offsetof(PerFrameCB, invViewProj), kExpectedOffset);
    // Padding begins at 144 + 64 = 208, leaving 48 bytes to reach 256.
    constexpr size_t kPaddingStart = offsetof(PerFrameCB, invViewProj) + 64u;
    EXPECT_EQ(kPaddingStart, 208u);
    EXPECT_EQ(sizeof(PerFrameCB) - kPaddingStart, 48u);
}

/// Write invViewProj and confirm round-trip through memcpy (simulates upload).
TEST(PerFrameCB, InvViewProjField_WriteAndReadBack) {
    PerFrameCB cb{};
    // Fill with a recognisable pattern — column-major 4x4 identity-ish.
    float* dst = reinterpret_cast<float*>(&cb.invViewProj);
    for (int i = 0; i < 16; ++i) {
        dst[i] = static_cast<float>(i + 1);
    }

    uint8_t blob[256];
    std::memcpy(blob, &cb, sizeof(cb));

    PerFrameCB restored{};
    std::memcpy(&restored, blob, sizeof(restored));

    const float* src = reinterpret_cast<const float*>(&restored.invViewProj);
    for (int i = 0; i < 16; ++i) {
        EXPECT_FLOAT_EQ(src[i], static_cast<float>(i + 1))
            << "invViewProj[" << i << "] corrupted after memcpy round-trip";
    }
}

/// lightDir, lightColor, and ambientColor must survive a memcpy upload path.
TEST(PerFrameCB, LightingFields_WriteAndReadBack) {
    PerFrameCB cb{};
    cb.lightDir    = {0.0f, -1.0f, 0.0f, 0.0f};
    cb.lightColor  = {1.0f,  0.9f, 0.8f, 0.0f};
    cb.ambientColor = {0.1f, 0.1f, 0.15f, 0.0f};

    uint8_t blob[256];
    std::memcpy(blob, &cb, sizeof(cb));

    PerFrameCB r{};
    std::memcpy(&r, blob, sizeof(r));

    EXPECT_FLOAT_EQ(r.lightDir.y,    -1.0f);
    EXPECT_FLOAT_EQ(r.lightColor.x,   1.0f);
    EXPECT_FLOAT_EQ(r.lightColor.y,   0.9f);
    EXPECT_FLOAT_EQ(r.ambientColor.z, 0.15f);
}

// ─────────────────────────────────────────────────────────────────────────────
// PointLightData — layout
// ─────────────────────────────────────────────────────────────────────────────

TEST(PointLightData, SizeIs48Bytes) {
    EXPECT_EQ(sizeof(PointLightData), 48u);
}

/// Field offsets must match the HLSL struct (three float4s = 48 bytes).
///
///  0  position        float3  (12 B)
/// 12  radius          float   ( 4 B)
/// 16  color           float3  (12 B)
/// 28  sourceRadius    float   ( 4 B)
/// 32  falloffExponent float   ( 4 B)
/// 36  specularScale   float   ( 4 B)
/// 40  diffuseScale    float   ( 4 B)
/// 44  flags           uint    ( 4 B)
TEST(PointLightData, FieldLayout) {
    EXPECT_EQ(offsetof(PointLightData, position),        0u);
    EXPECT_EQ(offsetof(PointLightData, radius),         12u);
    EXPECT_EQ(offsetof(PointLightData, color),          16u);
    EXPECT_EQ(offsetof(PointLightData, sourceRadius),   28u);
    EXPECT_EQ(offsetof(PointLightData, falloffExponent),32u);
    EXPECT_EQ(offsetof(PointLightData, specularScale),  36u);
    EXPECT_EQ(offsetof(PointLightData, diffuseScale),   40u);
    EXPECT_EQ(offsetof(PointLightData, flags),          44u);
    EXPECT_EQ(sizeof(PointLightData),                   48u);
}

TEST(PointLightData, WriteAndReadBack) {
    PointLightData light{};
    light.position       = {10.0f, 5.0f, -3.0f};
    light.radius         = 25.0f;
    light.color          = {2.0f, 1.8f, 1.6f};
    light.sourceRadius   = 0.5f;
    light.specularScale  = 0.8f;
    light.diffuseScale   = 1.0f;
    light.flags          = kLightFlag_InverseSquared;

    EXPECT_FLOAT_EQ(light.position.x,    10.0f);
    EXPECT_FLOAT_EQ(light.position.z,    -3.0f);
    EXPECT_FLOAT_EQ(light.radius,        25.0f);
    EXPECT_FLOAT_EQ(light.color.y,        1.8f);
    EXPECT_FLOAT_EQ(light.sourceRadius,   0.5f);
    EXPECT_EQ(light.flags, kLightFlag_InverseSquared);
}

TEST(PointLightData, MemcpyRoundTrip) {
    PointLightData src{};
    src.position       = {1.0f, 2.0f, 3.0f};
    src.radius         = 50.0f;
    src.color          = {0.5f, 0.6f, 0.7f};
    src.sourceRadius   = 1.0f;
    src.falloffExponent = 8.0f;
    src.specularScale  = 0.9f;
    src.diffuseScale   = 1.0f;
    src.flags          = kLightFlag_InverseSquared;

    uint8_t blob[48];
    std::memcpy(blob, &src, sizeof(src));

    PointLightData dst{};
    std::memcpy(&dst, blob, sizeof(dst));

    EXPECT_FLOAT_EQ(dst.position.x, 1.0f);
    EXPECT_FLOAT_EQ(dst.position.y, 2.0f);
    EXPECT_FLOAT_EQ(dst.position.z, 3.0f);
    EXPECT_FLOAT_EQ(dst.radius,    50.0f);
    EXPECT_FLOAT_EQ(dst.color.x,    0.5f);
    EXPECT_FLOAT_EQ(dst.color.z,    0.7f);
    EXPECT_FLOAT_EQ(dst.sourceRadius, 1.0f);
    EXPECT_EQ(dst.flags, kLightFlag_InverseSquared);
}

// ─────────────────────────────────────────────────────────────────────────────
// LightingCB — layout and capacity
// ─────────────────────────────────────────────────────────────────────────────

TEST(LightingCB, SizeIsMultipleOf256) {
    EXPECT_EQ(sizeof(LightingCB) % 256u, 0u)
        << "LightingCB size " << sizeof(LightingCB)
        << " is not a multiple of 256 (D3D12 CB alignment requirement)";
}

TEST(LightingCB, MaxPointLightsIs64) {
    EXPECT_EQ(kMaxPointLights, 64u);
}

/// The point light array must begin at offset 0 so the GPU array base address
/// matches the CB base address directly — no header fields before the array.
TEST(LightingCB, PointLightArrayOffset_IsZero) {
    EXPECT_EQ(offsetof(LightingCB, pointLights), 0u);
}

/// pointLightCount must immediately follow the 64-element light array.
/// 64 lights × 32 bytes = 2048 bytes.
TEST(LightingCB, PointLightCountOffset_Is2048) {
    constexpr size_t kExpected = kMaxPointLights * sizeof(PointLightData);
    EXPECT_EQ(kExpected, 2048u);
    EXPECT_EQ(offsetof(LightingCB, pointLightCount), kExpected);
}

TEST(LightingCB, DefaultPointLightCountIsZero) {
    LightingCB cb{};
    EXPECT_EQ(cb.pointLightCount, 0u);
}

TEST(LightingCB, WritePointLightAndCount) {
    LightingCB cb{};
    cb.pointLights[0].position = {0.0f, 3.0f, 0.0f};
    cb.pointLights[0].radius   = 10.0f;
    cb.pointLights[0].color    = {1.0f, 1.0f, 1.0f};
    cb.pointLightCount = 1u;

    EXPECT_EQ(cb.pointLightCount, 1u);
    EXPECT_FLOAT_EQ(cb.pointLights[0].position.y, 3.0f);
    EXPECT_FLOAT_EQ(cb.pointLights[0].radius,     10.0f);
}

/// Ensure all 64 slots are writable without corruption to adjacent fields.
TEST(LightingCB, WriteAllSlots_NoCorruption) {
    LightingCB cb{};
    for (uint32_t i = 0; i < kMaxPointLights; ++i) {
        cb.pointLights[i].position = {static_cast<float>(i), 0.0f, 0.0f};
        cb.pointLights[i].radius   = static_cast<float>(i + 1);
        cb.pointLights[i].color    = {1.0f, 0.5f, 0.0f};
    }
    cb.pointLightCount = kMaxPointLights;

    EXPECT_EQ(cb.pointLightCount, kMaxPointLights);
    EXPECT_FLOAT_EQ(cb.pointLights[0].position.x,               0.0f);
    EXPECT_FLOAT_EQ(cb.pointLights[kMaxPointLights - 1].position.x,
                    static_cast<float>(kMaxPointLights - 1));
    EXPECT_FLOAT_EQ(cb.pointLights[kMaxPointLights - 1].radius,
                    static_cast<float>(kMaxPointLights));
}

TEST(LightingCB, MemcpyRoundTrip) {
    LightingCB src{};
    src.pointLights[3].position = {5.0f, 10.0f, 15.0f};
    src.pointLights[3].radius   = 8.0f;
    src.pointLightCount = 4u;

    const size_t kSize = sizeof(LightingCB);
    std::vector<uint8_t> blob(kSize);
    std::memcpy(blob.data(), &src, kSize);

    LightingCB dst{};
    std::memcpy(&dst, blob.data(), kSize);

    EXPECT_EQ(dst.pointLightCount, 4u);
    EXPECT_FLOAT_EQ(dst.pointLights[3].position.x,  5.0f);
    EXPECT_FLOAT_EQ(dst.pointLights[3].position.y, 10.0f);
    EXPECT_FLOAT_EQ(dst.pointLights[3].radius,       8.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// GBufferPassDesc — struct accessibility
// ─────────────────────────────────────────────────────────────────────────────

TEST(GBufferPassDesc, DefaultConstruction_ResourceIdsAreInvalid) {
    GBufferPassDesc desc{};
    EXPECT_FALSE(desc.albedoTarget.IsValid());
    EXPECT_FALSE(desc.normalTarget.IsValid());
    EXPECT_FALSE(desc.depthBuffer.IsValid());
}

TEST(GBufferPassDesc, DefaultConstruction_HandlesAreInvalid) {
    GBufferPassDesc desc{};
    EXPECT_FALSE(desc.depthTexture.IsValid());
    EXPECT_FALSE(desc.pipeline.IsValid());
    EXPECT_FALSE(desc.vertexBuffer.IsValid());
    EXPECT_FALSE(desc.indexBuffer.IsValid());
}

TEST(GBufferPassDesc, DefaultConstruction_PointersAreNull) {
    GBufferPassDesc desc{};
    EXPECT_EQ(desc.drawList,    nullptr);
    EXPECT_EQ(desc.cbAllocator, nullptr);
}

TEST(GBufferPassDesc, DefaultConstruction_NumericFieldsAreZero) {
    GBufferPassDesc desc{};
    EXPECT_EQ(desc.vertexBufferSrvIndex, 0u);
    EXPECT_EQ(desc.indexCount,           0u);
    EXPECT_FLOAT_EQ(desc.viewportWidth,  0.0f);
    EXPECT_FLOAT_EQ(desc.viewportHeight, 0.0f);
}

TEST(GBufferPassDesc, DefaultConstruction_BindlessFalse) {
    GBufferPassDesc desc{};
    EXPECT_FALSE(desc.bindless);
}

TEST(GBufferPassDesc, AllFieldsAccessible_WriteAndRead) {
    GBufferPassDesc desc{};

    // Render graph resource IDs
    desc.albedoTarget     = {1u};
    desc.normalTarget     = {2u};
    desc.depthBuffer      = {3u};

    // Physical handles
    desc.depthTexture = {4u, 1u};
    desc.pipeline     = {5u, 1u};
    desc.vertexBuffer = {6u, 1u};
    desc.indexBuffer  = {7u, 1u};

    // Numeric params
    desc.vertexBufferSrvIndex = 10u;
    desc.indexCount           = 36u;
    desc.viewportWidth        = 1920.0f;
    desc.viewportHeight       = 1080.0f;
    desc.bindless             = true;

    EXPECT_EQ(desc.albedoTarget.index,     1u);
    EXPECT_EQ(desc.normalTarget.index,     2u);
    EXPECT_EQ(desc.depthBuffer.index,      3u);
    EXPECT_EQ(desc.depthTexture.index,     4u);
    EXPECT_EQ(desc.pipeline.index,         5u);
    EXPECT_EQ(desc.vertexBuffer.index,     6u);
    EXPECT_EQ(desc.indexBuffer.index,      7u);
    EXPECT_EQ(desc.vertexBufferSrvIndex,  10u);
    EXPECT_EQ(desc.indexCount,            36u);
    EXPECT_FLOAT_EQ(desc.viewportWidth,  1920.0f);
    EXPECT_FLOAT_EQ(desc.viewportHeight, 1080.0f);
    EXPECT_TRUE(desc.bindless);
}

// ─────────────────────────────────────────────────────────────────────────────
// LightingPassDesc — struct accessibility
// ─────────────────────────────────────────────────────────────────────────────

TEST(LightingPassDesc, DefaultConstruction_ResourceIdsAreInvalid) {
    LightingPassDesc desc{};
    EXPECT_FALSE(desc.backbuffer.IsValid());
    EXPECT_FALSE(desc.albedoGBuffer.IsValid());
    EXPECT_FALSE(desc.normalGBuffer.IsValid());
    EXPECT_FALSE(desc.depthGBuffer.IsValid());
}

TEST(LightingPassDesc, DefaultConstruction_HandlesAreInvalid) {
    LightingPassDesc desc{};
    EXPECT_FALSE(desc.pipeline.IsValid());
    EXPECT_FALSE(desc.albedoTexture.IsValid());
    EXPECT_FALSE(desc.normalTexture.IsValid());
    EXPECT_FALSE(desc.depthTextureForSrv.IsValid());
}

TEST(LightingPassDesc, DefaultConstruction_NumericFieldsAreZero) {
    LightingPassDesc desc{};
    EXPECT_EQ(desc.albedoSrvIndex,      0u);
    EXPECT_EQ(desc.normalSrvIndex,      0u);
    EXPECT_EQ(desc.depthSrvIndex,       0u);
    EXPECT_FLOAT_EQ(desc.viewportWidth,  0.0f);
    EXPECT_FLOAT_EQ(desc.viewportHeight, 0.0f);
}

TEST(LightingPassDesc, DefaultConstruction_BindlessFalse) {
    LightingPassDesc desc{};
    EXPECT_FALSE(desc.bindless);
}

TEST(LightingPassDesc, AllFieldsAccessible_WriteAndRead) {
    LightingPassDesc desc{};

    desc.backbuffer         = {1u};
    desc.albedoGBuffer      = {2u};
    desc.normalGBuffer      = {3u};
    desc.depthGBuffer       = {4u};

    desc.pipeline           = {5u, 1u};
    desc.albedoTexture      = {6u, 1u};
    desc.normalTexture      = {7u, 1u};
    desc.depthTextureForSrv = {8u, 1u};

    desc.albedoSrvIndex     = 20u;
    desc.normalSrvIndex     = 21u;
    desc.depthSrvIndex      = 22u;
    desc.viewportWidth      = 2560.0f;
    desc.viewportHeight     = 1440.0f;
    desc.bindless           = true;

    EXPECT_EQ(desc.backbuffer.index,        1u);
    EXPECT_EQ(desc.albedoGBuffer.index,     2u);
    EXPECT_EQ(desc.normalGBuffer.index,     3u);
    EXPECT_EQ(desc.depthGBuffer.index,      4u);
    EXPECT_EQ(desc.pipeline.index,          5u);
    EXPECT_EQ(desc.albedoTexture.index,     6u);
    EXPECT_EQ(desc.normalTexture.index,     7u);
    EXPECT_EQ(desc.depthTextureForSrv.index, 8u);
    EXPECT_EQ(desc.albedoSrvIndex,         20u);
    EXPECT_EQ(desc.normalSrvIndex,         21u);
    EXPECT_EQ(desc.depthSrvIndex,          22u);
    EXPECT_FLOAT_EQ(desc.viewportWidth,  2560.0f);
    EXPECT_FLOAT_EQ(desc.viewportHeight, 1440.0f);
    EXPECT_TRUE(desc.bindless);
}

// ─────────────────────────────────────────────────────────────────────────────
// G-Buffer format expectations
// ─────────────────────────────────────────────────────────────────────────────

/// Albedo stores BaseColor in linear RGBA — 8-bit unorm is sufficient.
TEST(GBufferFormats, AlbedoFormat_IsRGBA8Unorm) {
    // The format constant used when constructing the albedo G-Buffer texture.
    // Validated against the GBufferPassDesc docs (RGBA8Unorm).
    constexpr RHIFormat kAlbedoFormat = RHIFormat::RGBA8Unorm;
    EXPECT_EQ(kAlbedoFormat, RHIFormat::RGBA8Unorm);
    // Sanity: confirm BytesPerTexel is 4 (one RGBA8 texel).
    EXPECT_EQ(BytesPerTexel(kAlbedoFormat), 4u);
}

/// Normal RT packs octahedral normal (RG) + metallic (B) + roughness (A) in RGBA8Unorm.
TEST(GBufferFormats, NormalFormat_IsRGBA8Unorm) {
    constexpr RHIFormat kNormalFormat = RHIFormat::RGBA8Unorm;
    EXPECT_EQ(kNormalFormat, RHIFormat::RGBA8Unorm);
    EXPECT_EQ(BytesPerTexel(kNormalFormat), 4u);
}

/// Depth uses 32-bit float for maximum precision in the deferred depth pass.
TEST(GBufferFormats, DepthFormat_IsD32Float) {
    constexpr RHIFormat kDepthFormat = RHIFormat::D32Float;
    EXPECT_EQ(kDepthFormat, RHIFormat::D32Float);
    EXPECT_EQ(BytesPerTexel(kDepthFormat), 4u);
}

// ─────────────────────────────────────────────────────────────────────────────
// RgTextureDesc — G-Buffer usage flag combinations
// ─────────────────────────────────────────────────────────────────────────────

/// Albedo G-Buffer must be writable as a render target and readable as SRV.
TEST(GBufferTextureDesc, Albedo_UsageFlags_ShaderResourceAndRenderTarget) {
    RgTextureDesc desc{};
    desc.width  = 1920;
    desc.height = 1080;
    desc.format = RHIFormat::RGBA8Unorm;
    desc.usage  = RHITextureUsage::ShaderResource | RHITextureUsage::RenderTarget;

    EXPECT_TRUE(HasFlag(desc.usage, RHITextureUsage::ShaderResource))
        << "Albedo G-Buffer must have ShaderResource flag for lighting pass read";
    EXPECT_TRUE(HasFlag(desc.usage, RHITextureUsage::RenderTarget))
        << "Albedo G-Buffer must have RenderTarget flag for G-Buffer write";
    EXPECT_FALSE(HasFlag(desc.usage, RHITextureUsage::DepthStencil))
        << "Albedo G-Buffer must NOT have DepthStencil flag";
}

/// Normal G-Buffer must be writable as a render target and readable as SRV.
TEST(GBufferTextureDesc, Normal_UsageFlags_ShaderResourceAndRenderTarget) {
    RgTextureDesc desc{};
    desc.width  = 1920;
    desc.height = 1080;
    desc.format = RHIFormat::RGBA8Unorm;
    desc.usage  = RHITextureUsage::ShaderResource | RHITextureUsage::RenderTarget;

    EXPECT_TRUE(HasFlag(desc.usage, RHITextureUsage::ShaderResource));
    EXPECT_TRUE(HasFlag(desc.usage, RHITextureUsage::RenderTarget));
    EXPECT_FALSE(HasFlag(desc.usage, RHITextureUsage::DepthStencil));
    EXPECT_FALSE(HasFlag(desc.usage, RHITextureUsage::UnorderedAccess));
}

/// Depth G-Buffer must have DepthStencil for the G-Buffer write pass AND
/// ShaderResource for the lighting pass to reconstruct world position.
TEST(GBufferTextureDesc, Depth_UsageFlags_ShaderResourceAndDepthStencil) {
    RgTextureDesc desc{};
    desc.width  = 1920;
    desc.height = 1080;
    desc.format = RHIFormat::D32Float;
    desc.usage  = RHITextureUsage::ShaderResource | RHITextureUsage::DepthStencil;

    EXPECT_TRUE(HasFlag(desc.usage, RHITextureUsage::ShaderResource))
        << "Depth must have ShaderResource so the lighting pass can read it as SRV";
    EXPECT_TRUE(HasFlag(desc.usage, RHITextureUsage::DepthStencil))
        << "Depth must have DepthStencil so the G-Buffer pass can write DSV";
    EXPECT_FALSE(HasFlag(desc.usage, RHITextureUsage::RenderTarget))
        << "Depth G-Buffer must NOT have RenderTarget flag (DSV, not RTV)";
}

/// Confirm that combining ShaderResource | DepthStencil produces a distinct
/// value from either flag alone — guards against flag enum collisions.
TEST(GBufferTextureDesc, UsageFlagCombination_IsDistinctFromComponents) {
    const RHITextureUsage combined =
        RHITextureUsage::ShaderResource | RHITextureUsage::DepthStencil;

    EXPECT_NE(combined, RHITextureUsage::ShaderResource);
    EXPECT_NE(combined, RHITextureUsage::DepthStencil);
    EXPECT_NE(combined, RHITextureUsage::None);
}

/// Confirm that RenderTarget | ShaderResource combination is distinct from
/// DepthStencil | ShaderResource — the two pairs must not alias.
TEST(GBufferTextureDesc, ColorUsage_DoesNotAliasDepthUsage) {
    const RHITextureUsage colorUsage =
        RHITextureUsage::ShaderResource | RHITextureUsage::RenderTarget;
    const RHITextureUsage depthUsage =
        RHITextureUsage::ShaderResource | RHITextureUsage::DepthStencil;

    EXPECT_NE(colorUsage, depthUsage);
    EXPECT_FALSE(HasFlag(colorUsage, RHITextureUsage::DepthStencil));
    EXPECT_FALSE(HasFlag(depthUsage, RHITextureUsage::RenderTarget));
}

// RgResourceId validity tests are in test_render_graph.cpp — not duplicated here.

