/// Unit tests for Metal resource types, format conversion functions, and ResourcePool.
///
/// Tests MetalBuffer, MetalTexture, MetalShader, MetalSampler, and MetalPipeline
/// struct default values, all format/mode/topology conversion functions, and
/// ResourcePool generational handle validation with MetalPipeline.
///
/// All tests are CPU-only — no Metal device or GPU is created.
/// Metal framework headers are not included; metal_resources.h uses only
/// forward-declared MTL types and integer constants.

#if defined(__APPLE__)

#include "metal_resources.h"

#include <gtest/gtest.h>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// MetalBuffer Defaults
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies that a zero-initialized MetalBuffer has all expected default values.
///
/// All pointer fields must be null, numeric fields zero, and srvIndex UINT32_MAX
/// (sentinel meaning "no argument buffer descriptor allocated").
TEST(MetalBuffer, DefaultValues) {
    MetalBuffer buffer{};
    EXPECT_EQ(buffer.buffer, nullptr);
    EXPECT_EQ(buffer.mappedPtr, nullptr);
    EXPECT_EQ(buffer.gpuAddress, 0u);
    EXPECT_EQ(buffer.srvIndex, UINT32_MAX);
    EXPECT_EQ(buffer.generation, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// MetalTexture Defaults
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies that a zero-initialized MetalTexture has all expected default values.
///
/// The descriptor indices (srvIndex, rtvIndex, dsvIndex) are UINT32_MAX until
/// the backend allocates them. ownsTexture defaults to true — the backend
/// will release the MTLTexture on destruction unless it came from a drawable.
TEST(MetalTexture, DefaultValues) {
    MetalTexture texture{};
    EXPECT_EQ(texture.texture, nullptr);
    EXPECT_EQ(texture.srvIndex, UINT32_MAX);
    EXPECT_EQ(texture.rtvIndex, UINT32_MAX);
    EXPECT_EQ(texture.dsvIndex, UINT32_MAX);
    EXPECT_EQ(texture.generation, 0u);
    EXPECT_TRUE(texture.ownsTexture);
}

// ─────────────────────────────────────────────────────────────────────────────
// MetalShader Defaults
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies that a zero-initialized MetalShader has all expected default values.
///
/// function and library are null until compilation succeeds.
/// stage defaults to Vertex — the creation path always overwrites this
/// with the actual stage from RHIShaderDesc.
TEST(MetalShader, DefaultValues) {
    MetalShader shader{};
    EXPECT_EQ(shader.function, nullptr);
    EXPECT_EQ(shader.library, nullptr);
    EXPECT_EQ(shader.stage, RHIShaderStage::Vertex);
    EXPECT_EQ(shader.generation, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// MetalSampler Defaults
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies that a zero-initialized MetalSampler has all expected default values.
///
/// samplerIndex is UINT32_MAX until the backend allocates a slot in the
/// global argument buffer sampler heap.
TEST(MetalSampler, DefaultValues) {
    MetalSampler sampler{};
    EXPECT_EQ(sampler.sampler, nullptr);
    EXPECT_EQ(sampler.samplerIndex, UINT32_MAX);
    EXPECT_EQ(sampler.generation, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// MetalPipeline Defaults
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies that a zero-initialized MetalPipeline has all expected default values.
///
/// Both renderPipeline and computePipeline are null — at most one is set after
/// creation. topology defaults to TriangleList (the most common draw primitive).
TEST(MetalPipeline, DefaultValues) {
    MetalPipeline pipeline{};
    EXPECT_EQ(pipeline.renderPipeline, nullptr);
    EXPECT_EQ(pipeline.computePipeline, nullptr);
    EXPECT_EQ(pipeline.topology, RHIPrimitiveTopology::TriangleList);
    EXPECT_EQ(pipeline.generation, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// ToMTLPixelFormat — All RHIFormat values
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies ToMTLPixelFormat maps every RHIFormat to the correct MTLPixelFormat integer.
///
/// Values are cross-referenced against the MTLPixelFormat enum in Metal.h.
/// RGB32Float has no Metal equivalent — it is promoted to RGBA32Float (125).
TEST(MetalFormatConversion, ToMTLPixelFormat_AllFormats) {
    // Unknown → 0 (MTLPixelFormatInvalid)
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::Unknown), MetalPixelFormat::kInvalid);

    // 8-bit channels
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::R8Unorm),    MetalPixelFormat::kR8Unorm);
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::RG8Unorm),   MetalPixelFormat::kRG8Unorm);
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::RGBA8Unorm), MetalPixelFormat::kRGBA8Unorm);
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::RGBA8Srgb),  MetalPixelFormat::kRGBA8Srgb);
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::BGRA8Unorm), MetalPixelFormat::kBGRA8Unorm);
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::BGRA8Srgb),  MetalPixelFormat::kBGRA8Srgb);

    // Float formats
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::R16Float),    MetalPixelFormat::kR16Float);
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::RG16Float),   MetalPixelFormat::kRG16Float);
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::RGBA16Float), MetalPixelFormat::kRGBA16Float);
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::R32Float),    MetalPixelFormat::kR32Float);
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::RG32Float),   MetalPixelFormat::kRG32Float);
    // RGB32Float: Metal has no 3-channel float; promoted to RGBA32Float (125)
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::RGB32Float),  MetalPixelFormat::kRGBA32Float);
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::RGBA32Float), MetalPixelFormat::kRGBA32Float);

    // Uint formats
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::R32Uint),    MetalPixelFormat::kR32Uint);
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::RG32Uint),   MetalPixelFormat::kRG32Uint);
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::RGBA32Uint), MetalPixelFormat::kRGBA32Uint);

    // Depth formats
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::D16Unorm),       MetalPixelFormat::kDepth16Unorm);
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::D32Float),       MetalPixelFormat::kDepth32Float);
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::D24UnormS8Uint), MetalPixelFormat::kDepth24UnormStencil8);
    EXPECT_EQ(ToMTLPixelFormat(RHIFormat::D32FloatS8Uint), MetalPixelFormat::kDepth32FloatStencil8);

    // Verify raw integer values match the Metal API exactly
    EXPECT_EQ(MetalPixelFormat::kR8Unorm,         10u);
    EXPECT_EQ(MetalPixelFormat::kRG8Unorm,         30u);
    EXPECT_EQ(MetalPixelFormat::kRGBA8Unorm,       70u);
    EXPECT_EQ(MetalPixelFormat::kRGBA8Srgb,        71u);
    EXPECT_EQ(MetalPixelFormat::kBGRA8Unorm,       80u);
    EXPECT_EQ(MetalPixelFormat::kBGRA8Srgb,        81u);
    EXPECT_EQ(MetalPixelFormat::kR16Float,         25u);
    EXPECT_EQ(MetalPixelFormat::kRG16Float,        65u);
    EXPECT_EQ(MetalPixelFormat::kRGBA16Float,     115u);
    EXPECT_EQ(MetalPixelFormat::kR32Float,         55u);
    EXPECT_EQ(MetalPixelFormat::kRG32Float,       105u);
    EXPECT_EQ(MetalPixelFormat::kRGBA32Float,     125u);
    EXPECT_EQ(MetalPixelFormat::kR32Uint,          53u);
    EXPECT_EQ(MetalPixelFormat::kRG32Uint,        103u);
    EXPECT_EQ(MetalPixelFormat::kRGBA32Uint,      123u);
    EXPECT_EQ(MetalPixelFormat::kDepth16Unorm,    250u);
    EXPECT_EQ(MetalPixelFormat::kDepth32Float,    252u);
    EXPECT_EQ(MetalPixelFormat::kDepth24UnormStencil8, 255u);
    EXPECT_EQ(MetalPixelFormat::kDepth32FloatStencil8, 260u);
}

// ─────────────────────────────────────────────────────────────────────────────
// ToMTLStorageMode — All RHIMemoryType values
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies ToMTLStorageMode maps each RHIMemoryType to the correct MTLStorageMode integer.
///
/// Default → Private (2): GPU-only, fastest GPU bandwidth, no CPU mapping.
/// Upload  → Shared  (0): CPU-writable, GPU-readable without explicit sync.
/// Readback→ Shared  (0): GPU-writable, CPU-readable without explicit sync.
TEST(MetalFormatConversion, ToMTLStorageMode_AllTypes) {
    EXPECT_EQ(ToMTLStorageMode(RHIMemoryType::Default),  MetalStorageMode::kPrivate);
    EXPECT_EQ(ToMTLStorageMode(RHIMemoryType::Upload),   MetalStorageMode::kShared);
    EXPECT_EQ(ToMTLStorageMode(RHIMemoryType::Readback), MetalStorageMode::kShared);

    // Verify raw integer values match MTLStorageMode enum
    EXPECT_EQ(MetalStorageMode::kShared,  0u);
    EXPECT_EQ(MetalStorageMode::kManaged, 1u);
    EXPECT_EQ(MetalStorageMode::kPrivate, 2u);
}

// ─────────────────────────────────────────────────────────────────────────────
// ToMTLSamplerAddressMode — All RHIAddressMode values
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies ToMTLSamplerAddressMode maps each RHIAddressMode to the correct integer.
///
/// Wrap   → Repeat      (2): coordinates wrap modulo 1.0.
/// Clamp  → ClampToEdge (0): coordinates clamped to [0, 1].
/// Mirror → MirrorRepeat(1): coordinates mirrored each repeat.
/// Border → ClampToBorder(3): out-of-range samples border color (macOS 10.12+).
TEST(MetalFormatConversion, ToMTLSamplerAddressMode_AllModes) {
    EXPECT_EQ(ToMTLSamplerAddressMode(RHIAddressMode::Wrap),   MetalSamplerAddressMode::kRepeat);
    EXPECT_EQ(ToMTLSamplerAddressMode(RHIAddressMode::Clamp),  MetalSamplerAddressMode::kClampToEdge);
    EXPECT_EQ(ToMTLSamplerAddressMode(RHIAddressMode::Mirror), MetalSamplerAddressMode::kMirrorRepeat);
    EXPECT_EQ(ToMTLSamplerAddressMode(RHIAddressMode::Border), MetalSamplerAddressMode::kClampToBorder);

    // Verify raw integer values match MTLSamplerAddressMode enum
    EXPECT_EQ(MetalSamplerAddressMode::kClampToEdge,   0u);
    EXPECT_EQ(MetalSamplerAddressMode::kMirrorRepeat,  1u);
    EXPECT_EQ(MetalSamplerAddressMode::kRepeat,        2u);
    EXPECT_EQ(MetalSamplerAddressMode::kClampToBorder, 3u);
}

// ─────────────────────────────────────────────────────────────────────────────
// ToMTLSamplerMinMagFilter — All RHIFilterMode values
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies ToMTLSamplerMinMagFilter maps each RHIFilterMode to the correct integer.
///
/// Point       → Nearest (0): nearest-neighbor (no interpolation).
/// Linear      → Linear  (1): bilinear interpolation.
/// Anisotropic → Linear  (1): Metal controls anisotropy via maxAnisotropy, not the filter enum.
TEST(MetalFormatConversion, ToMTLSamplerMinMagFilter_AllFilters) {
    EXPECT_EQ(ToMTLSamplerMinMagFilter(RHIFilterMode::Point),       MetalSamplerFilter::kNearest);
    EXPECT_EQ(ToMTLSamplerMinMagFilter(RHIFilterMode::Linear),      MetalSamplerFilter::kLinear);
    EXPECT_EQ(ToMTLSamplerMinMagFilter(RHIFilterMode::Anisotropic), MetalSamplerFilter::kLinear);

    // Verify raw integer values match MTLSamplerMinMagFilter enum
    EXPECT_EQ(MetalSamplerFilter::kNearest, 0u);
    EXPECT_EQ(MetalSamplerFilter::kLinear,  1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// ToMTLCompareFunction — All RHICompareFunc values
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies ToMTLCompareFunction maps each RHICompareFunc to the correct integer (0–7).
///
/// Values match the MTLCompareFunction enum in the Metal framework exactly.
TEST(MetalFormatConversion, ToMTLCompareFunction_AllFuncs) {
    EXPECT_EQ(ToMTLCompareFunction(RHICompareFunc::Never),        MetalCompareFunction::kNever);
    EXPECT_EQ(ToMTLCompareFunction(RHICompareFunc::Less),         MetalCompareFunction::kLess);
    EXPECT_EQ(ToMTLCompareFunction(RHICompareFunc::Equal),        MetalCompareFunction::kEqual);
    EXPECT_EQ(ToMTLCompareFunction(RHICompareFunc::LessEqual),    MetalCompareFunction::kLessEqual);
    EXPECT_EQ(ToMTLCompareFunction(RHICompareFunc::Greater),      MetalCompareFunction::kGreater);
    EXPECT_EQ(ToMTLCompareFunction(RHICompareFunc::NotEqual),     MetalCompareFunction::kNotEqual);
    EXPECT_EQ(ToMTLCompareFunction(RHICompareFunc::GreaterEqual), MetalCompareFunction::kGreaterEqual);
    EXPECT_EQ(ToMTLCompareFunction(RHICompareFunc::Always),       MetalCompareFunction::kAlways);

    // Verify raw integer values match MTLCompareFunction enum
    EXPECT_EQ(MetalCompareFunction::kNever,        0u);
    EXPECT_EQ(MetalCompareFunction::kLess,         1u);
    EXPECT_EQ(MetalCompareFunction::kEqual,        2u);
    EXPECT_EQ(MetalCompareFunction::kLessEqual,    3u);
    EXPECT_EQ(MetalCompareFunction::kGreater,      4u);
    EXPECT_EQ(MetalCompareFunction::kNotEqual,     5u);
    EXPECT_EQ(MetalCompareFunction::kGreaterEqual, 6u);
    EXPECT_EQ(MetalCompareFunction::kAlways,       7u);
}

// ─────────────────────────────────────────────────────────────────────────────
// ToMTLPrimitiveType — All RHIPrimitiveTopology values
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies ToMTLPrimitiveType maps each RHIPrimitiveTopology to the correct integer.
///
/// Unlike DX12/Vulkan, Metal sets the primitive type at draw time via the
/// drawPrimitives call, not in the PSO. The MetalPipeline stores the topology
/// so it can be forwarded at draw time.
TEST(MetalFormatConversion, ToMTLPrimitiveType_AllTopologies) {
    EXPECT_EQ(ToMTLPrimitiveType(RHIPrimitiveTopology::TriangleList),  MetalPrimitiveType::kTriangle);
    EXPECT_EQ(ToMTLPrimitiveType(RHIPrimitiveTopology::TriangleStrip), MetalPrimitiveType::kTriangleStrip);
    EXPECT_EQ(ToMTLPrimitiveType(RHIPrimitiveTopology::LineList),      MetalPrimitiveType::kLine);
    EXPECT_EQ(ToMTLPrimitiveType(RHIPrimitiveTopology::LineStrip),     MetalPrimitiveType::kLineStrip);
    EXPECT_EQ(ToMTLPrimitiveType(RHIPrimitiveTopology::PointList),     MetalPrimitiveType::kPoint);

    // Verify raw integer values match MTLPrimitiveType enum
    EXPECT_EQ(MetalPrimitiveType::kPoint,         0u);
    EXPECT_EQ(MetalPrimitiveType::kLine,          1u);
    EXPECT_EQ(MetalPrimitiveType::kLineStrip,     2u);
    EXPECT_EQ(MetalPrimitiveType::kTriangle,      3u);
    EXPECT_EQ(MetalPrimitiveType::kTriangleStrip, 4u);
}

// ─────────────────────────────────────────────────────────────────────────────
// ResourcePool — Generational Handle Validation with MetalPipeline
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies Allocate returns slot index 0 with a generation > 0 for the first allocation.
///
/// The ResourcePool assigns sequential indices. The first allocation always
/// gets index 0. Generation starts at 1 (0 is the "invalid" sentinel).
TEST(MetalResourcePool, AllocateReturnsValidSlot) {
    ResourcePool<MetalPipeline> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);
    EXPECT_EQ(idx, 0u);
    EXPECT_GT(gen, 0u);

    MetalPipeline* slot = pool.Get(idx, gen);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->renderPipeline, nullptr);
    EXPECT_EQ(slot->computePipeline, nullptr);
}

/// Verifies Get returns null when queried with the wrong generation.
///
/// Both gen+1 (future generation) and 0 (pre-allocation sentinel) must
/// be rejected by the generational validation.
TEST(MetalResourcePool, GetWithWrongGenerationReturnsNull) {
    ResourcePool<MetalPipeline> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    EXPECT_EQ(pool.Get(idx, gen + 1), nullptr);
    EXPECT_EQ(pool.Get(idx, 0u), nullptr);
}

/// Verifies that freeing a slot and reallocating reuses the same index with a bumped generation.
///
/// The old generation must be invalidated so stale handles are rejected after free.
TEST(MetalResourcePool, FreeAndReallocate) {
    ResourcePool<MetalPipeline> pool;
    uint32_t gen1 = 0;
    uint32_t idx1 = pool.Allocate(gen1);
    pool.Free(idx1);

    // Reallocate should reuse the freed slot.
    uint32_t gen2 = 0;
    uint32_t idx2 = pool.Allocate(gen2);
    EXPECT_EQ(idx2, idx1);
    EXPECT_GT(gen2, gen1);

    // Old generation handle must now be stale.
    EXPECT_EQ(pool.Get(idx1, gen1), nullptr);
    // New generation handle must be valid.
    EXPECT_NE(pool.Get(idx2, gen2), nullptr);
}

/// Verifies that multiple sequential allocations produce consecutive slot indices.
///
/// Indices 0, 1, 2 must be returned in order, each with a valid generation.
TEST(MetalResourcePool, MultipleAllocations) {
    ResourcePool<MetalPipeline> pool;
    uint32_t gen0 = 0, gen1 = 0, gen2 = 0;
    uint32_t idx0 = pool.Allocate(gen0);
    uint32_t idx1 = pool.Allocate(gen1);
    uint32_t idx2 = pool.Allocate(gen2);

    EXPECT_EQ(idx0, 0u);
    EXPECT_EQ(idx1, 1u);
    EXPECT_EQ(idx2, 2u);

    EXPECT_NE(pool.Get(idx0, gen0), nullptr);
    EXPECT_NE(pool.Get(idx1, gen1), nullptr);
    EXPECT_NE(pool.Get(idx2, gen2), nullptr);
}

#endif // defined(__APPLE__)
