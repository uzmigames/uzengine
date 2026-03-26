/// Integration tests for the distortion accumulation and resolve passes — task 5.3.
///
/// Verifies the CPU-side struct layouts, default values, and sorting logic for
/// the distortion pass pipeline without requiring GPU hardware. All tests are
/// CPU-only.
///
/// # Scope
/// - DistortionConstants layout: size, field offsets, defaults, roundtrip.
/// - DistortionResolveConstants layout: size, field offsets, defaults, roundtrip.
/// - DistortionEmitterData defaults: pipeline validity, buffer pointer, distances,
///   SRV sentinel values, CB allocation sentinel state.
/// - DistortionPassDesc: default emitter vector is empty; push_back and retrieval.
/// - DistortionResolveDesc: default pipeline and SRV indices.
/// - Back-to-front sort: 3-emitter, empty, and single-emitter cases.
/// - MockDevice integration: GPUParticleBuffer allocated via MockRHIDevice,
///   assigned to DistortionEmitterData, buffer pointer is non-null and valid.

#include <gtest/gtest.h>

#include "engine/renderer/passes/vfx/distortion_pass.h"
#include "engine/renderer/passes/vfx/distortion_resolve_pass.h"
#include "engine/vfx/core/gpu_particle_buffer.h"
#include "mock_rhi_device.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace uz;
using uz::test::MockRHIDevice;

// =============================================================================
// DistortionConstants — struct layout and default values
// =============================================================================

/// The HLSL cbuffer layout is 4 floats (16 bytes). The CPU mirror must be
/// byte-identical so that a memcpy into the upload heap produces the correct
/// shader input.
TEST(DistortionPass, DistortionConstants_SizeIs16Bytes) {
    EXPECT_EQ(sizeof(DistortionConstants), 16u)
        << "DistortionConstants must be exactly 16 bytes to match HLSL cbuffer layout";
}

TEST(DistortionPass, DistortionConstants_DistortionStrength_DefaultIs1f) {
    DistortionConstants c;
    EXPECT_FLOAT_EQ(c.distortionStrength, 1.0f)
        << "distortionStrength default must be 1.0f per @ref Vlachos 2015";
}

TEST(DistortionPass, DistortionConstants_NormalMapSrvIndex_DefaultIsUINT32MAX) {
    DistortionConstants c;
    EXPECT_EQ(c.normalMapSrvIndex, UINT32_MAX)
        << "normalMapSrvIndex default must be UINT32_MAX (no texture bound)";
}

TEST(DistortionPass, DistortionConstants_DistortionStrength_OffsetIsZero) {
    EXPECT_EQ(offsetof(DistortionConstants, distortionStrength), 0u)
        << "distortionStrength must be at byte offset 0 (first field in cbuffer b0)";
}

TEST(DistortionPass, DistortionConstants_NormalMapSrvIndex_OffsetIs4) {
    EXPECT_EQ(offsetof(DistortionConstants, normalMapSrvIndex), 4u)
        << "normalMapSrvIndex must be at byte offset 4 (second field in cbuffer b0)";
}

/// Roundtrip: set all public fields, read them back without corruption.
TEST(DistortionPass, DistortionConstants_FieldRoundtrip) {
    DistortionConstants c;
    c.distortionStrength = 2.5f;
    c.normalMapSrvIndex  = 42u;
    c._pad0              = 0.0f;
    c._pad1              = 0.0f;

    EXPECT_FLOAT_EQ(c.distortionStrength, 2.5f)
        << "distortionStrength must survive a write-read roundtrip";
    EXPECT_EQ(c.normalMapSrvIndex, 42u)
        << "normalMapSrvIndex must survive a write-read roundtrip";
}

// =============================================================================
// DistortionResolveConstants — struct layout and default values
// =============================================================================

/// The resolve shader receives 4 x uint32 root constants (16 bytes total).
TEST(DistortionResolvePass, DistortionResolveConstants_SizeIs16Bytes) {
    EXPECT_EQ(sizeof(DistortionResolveConstants), 16u)
        << "DistortionResolveConstants must be exactly 16 bytes (4 x uint32)";
}

TEST(DistortionResolvePass, DistortionResolveConstants_ColorTextureSrvIndex_DefaultIsUINT32MAX) {
    DistortionResolveConstants c;
    EXPECT_EQ(c.colorTextureSrvIndex, UINT32_MAX)
        << "colorTextureSrvIndex default must be UINT32_MAX (no texture bound)";
}

TEST(DistortionResolvePass, DistortionResolveConstants_DistortionTextureSrvIndex_DefaultIsUINT32MAX) {
    DistortionResolveConstants c;
    EXPECT_EQ(c.distortionTextureSrvIndex, UINT32_MAX)
        << "distortionTextureSrvIndex default must be UINT32_MAX (no texture bound)";
}

TEST(DistortionResolvePass, DistortionResolveConstants_ColorTextureSrvIndex_OffsetIsZero) {
    EXPECT_EQ(offsetof(DistortionResolveConstants, colorTextureSrvIndex), 0u)
        << "colorTextureSrvIndex must be at byte offset 0 (first root constant)";
}

TEST(DistortionResolvePass, DistortionResolveConstants_DistortionTextureSrvIndex_OffsetIs4) {
    EXPECT_EQ(offsetof(DistortionResolveConstants, distortionTextureSrvIndex), 4u)
        << "distortionTextureSrvIndex must be at byte offset 4 (second root constant)";
}

/// Roundtrip: set all public fields, read them back without corruption.
TEST(DistortionResolvePass, DistortionResolveConstants_FieldRoundtrip) {
    DistortionResolveConstants c;
    c.colorTextureSrvIndex      = 7u;
    c.distortionTextureSrvIndex = 12u;
    c._pad0                     = 0u;
    c._pad1                     = 0u;

    EXPECT_EQ(c.colorTextureSrvIndex, 7u)
        << "colorTextureSrvIndex must survive a write-read roundtrip";
    EXPECT_EQ(c.distortionTextureSrvIndex, 12u)
        << "distortionTextureSrvIndex must survive a write-read roundtrip";
}

// =============================================================================
// DistortionEmitterData — default values
// =============================================================================

/// RHIPipelineHandle default-constructed must be invalid (generation == 0).
TEST(DistortionPass, DistortionEmitterData_DefaultPipeline_IsInvalid) {
    DistortionEmitterData e;
    EXPECT_FALSE(e.pipeline.IsValid())
        << "Default DistortionEmitterData.pipeline must be invalid (not yet set)";
}

TEST(DistortionPass, DistortionEmitterData_DefaultBuffer_IsNull) {
    DistortionEmitterData e;
    EXPECT_EQ(e.buffer, nullptr)
        << "Default DistortionEmitterData.buffer must be nullptr (no GPU buffer assigned)";
}

TEST(DistortionPass, DistortionEmitterData_DefaultDistanceToCamera_IsZero) {
    DistortionEmitterData e;
    EXPECT_FLOAT_EQ(e.distanceToCamera, 0.0f)
        << "Default DistortionEmitterData.distanceToCamera must be 0.0f";
}

TEST(DistortionPass, DistortionEmitterData_DefaultReadSetSrvIndex_IsUINT32MAX) {
    DistortionEmitterData e;
    EXPECT_EQ(e.readSetSrvIndex, UINT32_MAX)
        << "Default DistortionEmitterData.readSetSrvIndex must be UINT32_MAX (not yet bound)";
}

/// CBAllocation is a plain struct with no constructor. Default-initializing
/// DistortionEmitterData via aggregate initialisation yields cpuPtr == nullptr.
TEST(DistortionPass, DistortionEmitterData_DefaultBillboardCB_CpuPtrIsNull) {
    DistortionEmitterData e;
    EXPECT_EQ(e.billboardCB.cpuPtr, nullptr)
        << "Default billboardCB.cpuPtr must be nullptr (allocation not yet issued)";
}

TEST(DistortionPass, DistortionEmitterData_DefaultDistortionCB_CpuPtrIsNull) {
    DistortionEmitterData e;
    EXPECT_EQ(e.distortionCB.cpuPtr, nullptr)
        << "Default distortionCB.cpuPtr must be nullptr (allocation not yet issued)";
}

// =============================================================================
// DistortionPassDesc — descriptor defaults and mutation
// =============================================================================

TEST(DistortionPass, DistortionPassDesc_DefaultEmitters_IsEmpty) {
    DistortionPassDesc desc;
    EXPECT_TRUE(desc.emitters.empty())
        << "Default DistortionPassDesc.emitters must be an empty vector";
}

/// After push_back, the emitter should be retrievable with the same fields.
TEST(DistortionPass, DistortionPassDesc_PushBackEmitter_CanBeRetrieved) {
    DistortionPassDesc desc;
    DistortionEmitterData e;
    e.distanceToCamera  = 42.0f;
    e.readSetSrvIndex   = 7u;

    desc.emitters.push_back(e);

    ASSERT_EQ(desc.emitters.size(), 1u)
        << "emitters vector must contain exactly one entry after push_back";
    EXPECT_FLOAT_EQ(desc.emitters[0].distanceToCamera, 42.0f)
        << "distanceToCamera must be preserved in the emitters vector";
    EXPECT_EQ(desc.emitters[0].readSetSrvIndex, 7u)
        << "readSetSrvIndex must be preserved in the emitters vector";
}

TEST(DistortionPass, DistortionPassDesc_PushBackMultipleEmitters_SizeIsCorrect) {
    DistortionPassDesc desc;
    for (int i = 0; i < 5; ++i) {
        DistortionEmitterData e;
        e.distanceToCamera = static_cast<float>(i) * 10.0f;
        desc.emitters.push_back(e);
    }

    EXPECT_EQ(desc.emitters.size(), 5u)
        << "emitters vector must hold all 5 pushed entries";
    EXPECT_FLOAT_EQ(desc.emitters[4].distanceToCamera, 40.0f)
        << "Last pushed emitter must retain its distanceToCamera value";
}

// =============================================================================
// DistortionResolveDesc — descriptor defaults
// =============================================================================

TEST(DistortionResolvePass, DistortionResolveDesc_DefaultResolvePipeline_IsInvalid) {
    DistortionResolveDesc desc;
    EXPECT_FALSE(desc.resolvePipeline.IsValid())
        << "Default DistortionResolveDesc.resolvePipeline must be invalid (not yet set)";
}

TEST(DistortionResolvePass, DistortionResolveDesc_DefaultColorTextureSrvIndex_IsUINT32MAX) {
    DistortionResolveDesc desc;
    EXPECT_EQ(desc.colorTextureSrvIndex, UINT32_MAX)
        << "Default DistortionResolveDesc.colorTextureSrvIndex must be UINT32_MAX";
}

TEST(DistortionResolvePass, DistortionResolveDesc_DefaultDistortionTextureSrvIndex_IsUINT32MAX) {
    DistortionResolveDesc desc;
    EXPECT_EQ(desc.distortionTextureSrvIndex, UINT32_MAX)
        << "Default DistortionResolveDesc.distortionTextureSrvIndex must be UINT32_MAX";
}

// =============================================================================
// Back-to-front sorting of DistortionEmitterData
//
// AddDistortionAccumulationPass sorts emitters back-to-front (largest
// distanceToCamera first) before issuing draw calls. The comparator is:
//   a.distanceToCamera > b.distanceToCamera
// These tests exercise that comparator logic directly on a local vector.
// =============================================================================

namespace {

/// Sort a copy of `emitters` back-to-front (largest distance first) and return
/// the sorted copy. This mirrors the comparator used inside
/// AddDistortionAccumulationPass.
std::vector<DistortionEmitterData> SortBackToFront(
    std::vector<DistortionEmitterData> emitters)
{
    std::sort(emitters.begin(), emitters.end(),
              [](const DistortionEmitterData& a, const DistortionEmitterData& b) {
                  return a.distanceToCamera > b.distanceToCamera;
              });
    return emitters;
}

} // anonymous namespace

/// Three emitters at distances {5, 2, 8} should be ordered {8, 5, 2} after
/// back-to-front sort.
TEST(DistortionPass, Sort_ThreeEmitters_OrderedBackToFront) {
    std::vector<DistortionEmitterData> emitters(3);
    emitters[0].distanceToCamera = 5.0f;
    emitters[1].distanceToCamera = 2.0f;
    emitters[2].distanceToCamera = 8.0f;

    const auto sorted = SortBackToFront(emitters);

    ASSERT_EQ(sorted.size(), 3u);
    EXPECT_FLOAT_EQ(sorted[0].distanceToCamera, 8.0f)
        << "First emitter after back-to-front sort must be the farthest (8.0)";
    EXPECT_FLOAT_EQ(sorted[1].distanceToCamera, 5.0f)
        << "Second emitter after back-to-front sort must be the middle distance (5.0)";
    EXPECT_FLOAT_EQ(sorted[2].distanceToCamera, 2.0f)
        << "Third emitter after back-to-front sort must be the nearest (2.0)";
}

TEST(DistortionPass, Sort_EmptyList_IsNoOp) {
    std::vector<DistortionEmitterData> emitters;
    const auto sorted = SortBackToFront(emitters);
    EXPECT_TRUE(sorted.empty())
        << "Sorting an empty emitter list must produce an empty result";
}

TEST(DistortionPass, Sort_SingleEmitter_IsIdentity) {
    std::vector<DistortionEmitterData> emitters(1);
    emitters[0].distanceToCamera = 33.0f;

    const auto sorted = SortBackToFront(emitters);

    ASSERT_EQ(sorted.size(), 1u);
    EXPECT_FLOAT_EQ(sorted[0].distanceToCamera, 33.0f)
        << "Sorting a single emitter must leave it unchanged";
}

/// Equal distances must remain stable (no incorrect ordering between ties).
/// Using std::sort may not guarantee stability, but the order of equal elements
/// should not change the size or the distance values themselves.
TEST(DistortionPass, Sort_EqualDistances_ProducesCorrectSize) {
    std::vector<DistortionEmitterData> emitters(3);
    emitters[0].distanceToCamera = 10.0f;
    emitters[1].distanceToCamera = 10.0f;
    emitters[2].distanceToCamera = 10.0f;

    const auto sorted = SortBackToFront(emitters);

    ASSERT_EQ(sorted.size(), 3u)
        << "Sorting equal-distance emitters must not drop or duplicate entries";
    for (size_t i = 0; i < sorted.size(); ++i) {
        EXPECT_FLOAT_EQ(sorted[i].distanceToCamera, 10.0f)
            << "All equal-distance emitters must retain their distance value after sort";
    }
}

// =============================================================================
// DistortionEmitterData with MockRHIDevice
// =============================================================================

/// Create a GPUParticleBuffer using MockRHIDevice, assign it to
/// DistortionEmitterData, verify the pointer is non-null and the buffer is valid.
TEST(DistortionPass, EmitterDataWithMockDevice_BufferIsNonNullAndValid) {
    MockRHIDevice device;
    GPUParticleBuffer buf;

    const bool created = buf.Create(&device, 1024u);
    ASSERT_TRUE(created)
        << "GPUParticleBuffer::Create must succeed with MockRHIDevice";

    DistortionEmitterData e;
    e.buffer = &buf;

    EXPECT_NE(e.buffer, nullptr)
        << "Assigned buffer pointer must be non-null";
    EXPECT_TRUE(e.buffer->IsValid())
        << "Assigned GPUParticleBuffer must report IsValid() == true";
    EXPECT_EQ(e.buffer->capacity, 1024u)
        << "Assigned buffer capacity must match the value passed to Create()";

    buf.Destroy(&device);
}

/// After destroying the GPUParticleBuffer, IsValid() must return false,
/// confirming that the pointer-based contract detects stale-buffer scenarios
/// when the caller forgets to clear the DistortionEmitterData.
TEST(DistortionPass, EmitterDataWithMockDevice_DestroyedBuffer_IsNotValid) {
    MockRHIDevice device;
    GPUParticleBuffer buf;

    ASSERT_TRUE(buf.Create(&device, 512u));

    DistortionEmitterData e;
    e.buffer = &buf;

    buf.Destroy(&device);

    // The pointer is still non-null (dangling conceptually), but the buffer
    // itself reports invalid state — capacity is 0 after Destroy.
    EXPECT_FALSE(e.buffer->IsValid())
        << "Destroyed GPUParticleBuffer must report IsValid() == false";
}
