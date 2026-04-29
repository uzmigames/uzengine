/// GPU integration tests for the GI subsystem — validates the readback
/// pipeline, 3D texture creation, and GI resource lifecycle on a real DX12 device.
///
/// These tests require a real GPU with DX12 support. If device creation fails
/// (no GPU, no driver, CI headless), all tests are skipped via GTEST_SKIP().
///
/// # Coverage
/// - 3D R16F texture creation and destruction
/// - Upload → GPU → Readback pipeline for 2D R16F textures (slice-based validation)
/// - Structured buffer creation (SH probe buffers, mesh SDF metadata)
/// - GDF clipmap resource lifecycle (create all 4 cascades, destroy cleanly)
/// - Radiance cache buffer lifecycle
/// - MeshSDF-style resource creation and destruction

#ifdef _WIN32

#include "rhi.h"
#include "rhi_backend.h"
#include "gi/global_distance_field.h"
#include "gi/radiance_cache.h"
#include "gi/mesh_sdf_baker.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Half-float (IEEE 754 binary16) conversion utilities
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a 32-bit float to a 16-bit half-precision float (IEEE 754 binary16).
///
/// Handles normals, denormals, inf, and NaN. Rounding mode is round-to-nearest-even.
/// No dependency on DirectXMath or intrinsics — pure bit manipulation.
[[nodiscard]] static uint16_t FloatToHalf(float value) noexcept {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));

    const uint32_t sign     = (bits >> 16) & 0x8000u;
    const int32_t  exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127;
    const uint32_t mantissa = bits & 0x7FFFFFu;

    // Inf or NaN
    if (exponent > 15) {
        if (exponent == 128 && mantissa != 0) {
            // NaN — preserve a non-zero mantissa bit
            return static_cast<uint16_t>(sign | 0x7E00u);
        }
        // Inf or overflow → half-precision inf
        return static_cast<uint16_t>(sign | 0x7C00u);
    }

    // Underflow to zero
    if (exponent < -24) {
        return static_cast<uint16_t>(sign);
    }

    // Denormalized half
    if (exponent < -14) {
        const uint32_t shift = static_cast<uint32_t>(-14 - exponent);
        const uint32_t denormMantissa = (mantissa | 0x800000u) >> (shift + 13);
        return static_cast<uint16_t>(sign | denormMantissa);
    }

    // Normalized half
    const uint32_t halfExp      = static_cast<uint32_t>(exponent + 15) << 10;
    const uint32_t halfMantissa = mantissa >> 13;
    // Round to nearest even
    const uint32_t roundBit = (mantissa >> 12) & 1u;
    return static_cast<uint16_t>(sign | halfExp | halfMantissa | roundBit);
}

/// Converts a 16-bit half-precision float (IEEE 754 binary16) to a 32-bit float.
[[nodiscard]] static float HalfToFloat(uint16_t half) noexcept {
    const uint32_t sign     = static_cast<uint32_t>(half & 0x8000u) << 16;
    const uint32_t exponent = (half >> 10) & 0x1Fu;
    const uint32_t mantissa = half & 0x03FFu;

    uint32_t bits = 0;

    if (exponent == 0) {
        if (mantissa == 0) {
            // +/- zero
            bits = sign;
        } else {
            // Denormalized: convert to normalized float
            uint32_t m = mantissa;
            int32_t e = -14;
            while ((m & 0x0400u) == 0) {
                m <<= 1;
                --e;
            }
            m &= 0x03FFu; // Remove leading 1
            bits = sign | (static_cast<uint32_t>(e + 127) << 23) | (m << 13);
        }
    } else if (exponent == 31) {
        // Inf or NaN
        bits = sign | 0x7F800000u | (mantissa << 13);
    } else {
        // Normalized
        bits = sign | (static_cast<uint32_t>(exponent - 15 + 127) << 23) | (mantissa << 13);
    }

    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Alignment helper
// ─────────────────────────────────────────────────────────────────────────────

/// Rounds up to the nearest multiple of `alignment` (must be power of 2).
[[nodiscard]] constexpr uint32_t AlignUp(uint32_t value, uint32_t alignment) noexcept {
    return (value + alignment - 1) & ~(alignment - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Fixture — creates a real DX12 device, skips if unavailable
// ─────────────────────────────────────────────────────────────────────────────

/// Fixture that owns a real DX12 IRHIDevice for GPU-dependent GI integration tests.
///
/// SetUp attempts device creation with validation enabled. If the GPU or
/// driver does not support DX12, the test is skipped rather than failed.
class GIGpuTest : public ::testing::Test {
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

    /// Helper: creates a command list, records commands via `recordFn`,
    /// executes, and waits for GPU completion. Destroys the command list afterward.
    ///
    /// # Arguments
    /// * `recordFn` — Lambda that takes an `IRHICommandList*` and records commands.
    template <typename Fn>
    void ExecuteGPUWork(Fn&& recordFn) {
        uz::IRHICommandList* cmdList = m_device->CreateCommandList(uz::RHIQueueType::Graphics);
        ASSERT_NE(cmdList, nullptr) << "Failed to create command list";

        cmdList->Begin();
        recordFn(cmdList);
        cmdList->End();
        cmdList->Execute();

        m_device->WaitIdle();
        m_device->DestroyCommandList(cmdList);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Test 7.1: Half-Float Conversion Sanity (CPU-only prerequisite)
// ─────────────────────────────────────────────────────────────────────────────

/// Validates the FloatToHalf / HalfToFloat utilities used by all GPU readback tests.
/// These must be correct before we can trust any GPU readback comparison.
TEST_F(GIGpuTest, HalfFloatConversionRoundTrip) {
    // Test a range of values relevant to SDF data (negative inside, positive outside)
    const float testValues[] = {
        0.0f, 1.0f, -1.0f, 0.5f, -0.5f,
        0.25f, -0.25f, 100.0f, -100.0f,
        0.001f, -0.001f,
        65504.0f,  // Max finite half
        -65504.0f, // Min finite half
    };

    for (float original : testValues) {
        const uint16_t half = FloatToHalf(original);
        const float    recovered = HalfToFloat(half);
        // Half-float has limited precision — allow relative error up to ~0.1% for
        // large values, and absolute error of ~6e-8 for values near zero.
        if (std::fabs(original) > 1.0f) {
            EXPECT_NEAR(recovered, original, std::fabs(original) * 0.002f)
                << "Round-trip failed for " << original;
        } else {
            EXPECT_NEAR(recovered, original, 0.001f)
                << "Round-trip failed for " << original;
        }
    }

    // Special: +0 and -0
    EXPECT_EQ(FloatToHalf(0.0f) & 0x7FFFu, 0u);
    EXPECT_EQ(FloatToHalf(-0.0f), 0x8000u);

    // Special: infinity
    const float posInf = std::numeric_limits<float>::infinity();
    const float negInf = -posInf;
    EXPECT_EQ(FloatToHalf(posInf), 0x7C00u);
    EXPECT_EQ(FloatToHalf(negInf), 0xFC00u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7.2: 2D R16F Texture Upload → Readback Pipeline
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a 4x4 R16F texture, uploads known half-float data via staging buffer,
/// copies to readback buffer, maps and verifies values match.
///
/// This validates the entire GPU readback pipeline that all GI integration tests
/// depend on: upload buffer → CopyBufferToTexture → barrier → CopyTextureToBuffer
/// → readback buffer → MapBuffer → verify.
TEST_F(GIGpuTest, Readback2DR16FTexture) {
    constexpr uint32_t kWidth  = 4;
    constexpr uint32_t kHeight = 4;
    constexpr uint32_t kTexelSize = 2; // R16F = 2 bytes per texel
    constexpr uint32_t kSrcRowPitch = kWidth * kTexelSize;               // 8 bytes
    constexpr uint32_t kAlignedRowPitch = AlignUp(kSrcRowPitch, 256);    // 256 bytes (DX12 minimum)
    constexpr uint32_t kUploadSize = kAlignedRowPitch * kHeight;         // 1024 bytes
    constexpr uint32_t kReadbackSize = kAlignedRowPitch * kHeight;       // 1024 bytes

    // Prepare source data — 16 known half-float values representing SDF distances
    // Negative = inside mesh, positive = outside, zero = surface
    const float srcFloats[kWidth * kHeight] = {
        -0.5f,  -0.25f,  0.0f,   0.25f,
         0.5f,   1.0f,  -1.0f,   2.0f,
        -2.0f,   0.125f, 0.375f, 0.625f,
         0.875f, 10.0f, -10.0f,  0.001f,
    };

    // Convert to half-float and pack into aligned upload layout
    std::vector<uint8_t> uploadData(kUploadSize, 0);
    for (uint32_t row = 0; row < kHeight; ++row) {
        for (uint32_t col = 0; col < kWidth; ++col) {
            const uint16_t half = FloatToHalf(srcFloats[row * kWidth + col]);
            const uint32_t offset = row * kAlignedRowPitch + col * kTexelSize;
            std::memcpy(&uploadData[offset], &half, sizeof(half));
        }
    }

    // Create the target R16F texture with CopySrc + CopyDst usage
    uz::RHITextureDesc texDesc{};
    texDesc.width  = kWidth;
    texDesc.height = kHeight;
    texDesc.format = uz::RHIFormat::R16Float;
    texDesc.usage  = uz::RHITextureUsage::ShaderResource
                   | uz::RHITextureUsage::CopySrc
                   | uz::RHITextureUsage::CopyDst;
    texDesc.debugName = "GI_TestR16F_2D";

    uz::RHITextureHandle texture = m_device->CreateTexture(texDesc);
    ASSERT_TRUE(texture.IsValid()) << "Failed to create R16F 2D texture";

    // Create upload buffer (CPU → GPU staging)
    uz::RHIBufferDesc uploadBufDesc{};
    uploadBufDesc.sizeBytes = kUploadSize;
    uploadBufDesc.usage     = uz::RHIBufferUsage::CopySrc;
    uploadBufDesc.memory    = uz::RHIMemoryType::Upload;
    uploadBufDesc.debugName = "GI_TestUpload";

    uz::RHIBufferHandle uploadBuffer = m_device->CreateBuffer(uploadBufDesc);
    ASSERT_TRUE(uploadBuffer.IsValid()) << "Failed to create upload buffer";

    // Map upload buffer and copy data
    void* mapped = m_device->MapBuffer(uploadBuffer);
    ASSERT_NE(mapped, nullptr) << "Failed to map upload buffer";
    std::memcpy(mapped, uploadData.data(), kUploadSize);
    m_device->UnmapBuffer(uploadBuffer);

    // Create readback buffer (GPU → CPU)
    uz::RHIBufferDesc readbackBufDesc{};
    readbackBufDesc.sizeBytes = kReadbackSize;
    readbackBufDesc.usage     = uz::RHIBufferUsage::CopyDst;
    readbackBufDesc.memory    = uz::RHIMemoryType::Readback;
    readbackBufDesc.debugName = "GI_TestReadback";

    uz::RHIBufferHandle readbackBuffer = m_device->CreateBuffer(readbackBufDesc);
    ASSERT_TRUE(readbackBuffer.IsValid()) << "Failed to create readback buffer";

    // Execute GPU work: upload → barrier → readback
    ExecuteGPUWork([&](uz::IRHICommandList* cmd) {
        // Transition texture to CopyDst for receiving upload data
        uz::RHIBarrierDesc barrierToCopyDst{};
        barrierToCopyDst.texture     = texture;
        barrierToCopyDst.stateBefore = uz::RHIResourceState::Common;
        barrierToCopyDst.stateAfter  = uz::RHIResourceState::CopyDst;
        cmd->ResourceBarrier(&barrierToCopyDst, 1);

        // Copy upload buffer → texture
        cmd->CopyBufferToTexture(uploadBuffer, 0, kAlignedRowPitch, texture, 0, 0);

        // Transition texture to CopySrc for readback
        uz::RHIBarrierDesc barrierToCopySrc{};
        barrierToCopySrc.texture     = texture;
        barrierToCopySrc.stateBefore = uz::RHIResourceState::CopyDst;
        barrierToCopySrc.stateAfter  = uz::RHIResourceState::CopySrc;
        cmd->ResourceBarrier(&barrierToCopySrc, 1);

        // Copy texture → readback buffer
        cmd->CopyTextureToBuffer(texture, 0, readbackBuffer, 0, kAlignedRowPitch, 0);
    });

    // Map readback buffer and verify values match
    void* readbackMapped = m_device->MapBuffer(readbackBuffer);
    ASSERT_NE(readbackMapped, nullptr) << "Failed to map readback buffer";

    const auto* readbackBytes = static_cast<const uint8_t*>(readbackMapped);

    for (uint32_t row = 0; row < kHeight; ++row) {
        for (uint32_t col = 0; col < kWidth; ++col) {
            const uint32_t byteOffset = row * kAlignedRowPitch + col * kTexelSize;
            uint16_t readHalf = 0;
            std::memcpy(&readHalf, &readbackBytes[byteOffset], sizeof(readHalf));

            const float readFloat = HalfToFloat(readHalf);
            const float expected  = srcFloats[row * kWidth + col];

            // Half-precision has ~3 decimal digits of precision
            if (std::fabs(expected) > 1.0f) {
                EXPECT_NEAR(readFloat, expected, std::fabs(expected) * 0.002f)
                    << "Mismatch at (" << col << ", " << row << "): "
                    << "expected=" << expected << " got=" << readFloat
                    << " (half=0x" << std::hex << readHalf << std::dec << ")";
            } else {
                EXPECT_NEAR(readFloat, expected, 0.001f)
                    << "Mismatch at (" << col << ", " << row << "): "
                    << "expected=" << expected << " got=" << readFloat
                    << " (half=0x" << std::hex << readHalf << std::dec << ")";
            }
        }
    }

    m_device->UnmapBuffer(readbackBuffer);

    // Cleanup
    m_device->DestroyBuffer(readbackBuffer);
    m_device->DestroyBuffer(uploadBuffer);
    m_device->DestroyTexture(texture);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7.2b: 3D R16F Texture Creation and Upload
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a 4x4x4 R16F 3D volume texture (same format as mesh SDFs and GDF
/// clipmaps), verifies creation succeeds, and confirms the texture descriptor
/// round-trips correctly through the RHI.
TEST_F(GIGpuTest, Create3DR16FVolumeTexture) {
    constexpr uint32_t kSize = 4; // 4x4x4

    uz::RHITextureDesc texDesc{};
    texDesc.width            = kSize;
    texDesc.height           = kSize;
    texDesc.depthOrArraySize = static_cast<uint16_t>(kSize);
    texDesc.format           = uz::RHIFormat::R16Float;
    texDesc.usage            = uz::RHITextureUsage::ShaderResource
                             | uz::RHITextureUsage::UnorderedAccess
                             | uz::RHITextureUsage::CopySrc
                             | uz::RHITextureUsage::CopyDst;
    texDesc.is3D             = true;
    texDesc.debugName        = "GI_TestR16F_3D_4x4x4";

    uz::RHITextureHandle texture = m_device->CreateTexture(texDesc);
    ASSERT_TRUE(texture.IsValid()) << "Failed to create 4x4x4 R16F 3D texture";

    // Verify descriptor round-trip
    uz::RHITextureDesc readDesc = m_device->GetTextureDesc(texture);
    EXPECT_EQ(readDesc.width, kSize);
    EXPECT_EQ(readDesc.height, kSize);
    EXPECT_EQ(readDesc.depthOrArraySize, kSize);
    EXPECT_EQ(readDesc.format, uz::RHIFormat::R16Float);
    EXPECT_TRUE(readDesc.is3D);

    m_device->DestroyTexture(texture);
}

/// Creates a 64^3 R16F 3D texture — the exact resolution used by GDF clipmaps.
/// This verifies the RHI can handle the production-scale resource.
TEST_F(GIGpuTest, Create3DR16FTexture64Cubed) {
    constexpr uint32_t kResolution = uz::GlobalDistanceField::kResolution; // 64

    uz::RHITextureDesc texDesc{};
    texDesc.width            = kResolution;
    texDesc.height           = kResolution;
    texDesc.depthOrArraySize = static_cast<uint16_t>(kResolution);
    texDesc.format           = uz::RHIFormat::R16Float;
    texDesc.usage            = uz::RHITextureUsage::ShaderResource
                             | uz::RHITextureUsage::UnorderedAccess;
    texDesc.is3D             = true;
    texDesc.debugName        = "GI_TestGDFClipmap_64";

    uz::RHITextureHandle texture = m_device->CreateTexture(texDesc);
    ASSERT_TRUE(texture.IsValid()) << "Failed to create 64^3 R16F 3D texture";

    m_device->DestroyTexture(texture);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7.3a: GDF Clipmap Resource Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

/// Creates the full GlobalDistanceField (4 cascaded R16F 3D clipmap textures),
/// verifies all resources are valid, then destroys them without leaks.
TEST_F(GIGpuTest, GDFClipmapResourceLifecycle) {
    uz::GlobalDistanceField gdf;

    // Before creation, should not be initialized
    EXPECT_FALSE(gdf.IsInitialized());

    // Create all 4 clipmap textures
    const bool created = gdf.CreateResources(m_device.get());
    ASSERT_TRUE(created) << "GlobalDistanceField::CreateResources() failed";
    EXPECT_TRUE(gdf.IsInitialized());

    // Validate each clipmap cascade
    for (uint32_t i = 0; i < uz::GlobalDistanceField::kNumClipmaps; ++i) {
        const auto& clipmap = gdf.GetClipmap(i);
        EXPECT_TRUE(clipmap.texture.IsValid())
            << "Clipmap " << i << " texture is invalid";
        EXPECT_EQ(clipmap.extent, uz::GlobalDistanceField::kClipmapExtents[i])
            << "Clipmap " << i << " extent mismatch";
    }

    // Verify voxel sizes follow the cascade scaling (each is 4x the previous)
    for (uint32_t i = 1; i < uz::GlobalDistanceField::kNumClipmaps; ++i) {
        const float prevVoxelSize = gdf.GetVoxelSize(i - 1);
        const float currVoxelSize = gdf.GetVoxelSize(i);
        EXPECT_NEAR(currVoxelSize / prevVoxelSize, 4.0f, 0.01f)
            << "Cascade " << i << " voxel size should be 4x cascade " << (i - 1);
    }

    // Destroy — should not crash or leak
    gdf.DestroyResources();
    EXPECT_FALSE(gdf.IsInitialized());
}

/// Verifies double-destroy of GDF resources is safe (no crash, no double-free).
TEST_F(GIGpuTest, GDFDoubleDestroyIsSafe) {
    uz::GlobalDistanceField gdf;
    ASSERT_TRUE(gdf.CreateResources(m_device.get()));

    gdf.DestroyResources();
    EXPECT_FALSE(gdf.IsInitialized());

    // Second destroy should be a no-op
    gdf.DestroyResources();
    EXPECT_FALSE(gdf.IsInitialized());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7.3b: Radiance Cache Resource Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

/// Creates the RadianceCache SH buffer (StructuredBuffer<float4>, 4096 probes x
/// 7 float4s = ~449 KB), verifies creation and SRV/UAV indices, then destroys.
TEST_F(GIGpuTest, RadianceCacheResourceLifecycle) {
    uz::RadianceCache rc;

    EXPECT_FALSE(rc.IsInitialized());

    const bool created = rc.CreateResources(m_device.get(), 5000.0f);
    ASSERT_TRUE(created) << "RadianceCache::CreateResources() failed";
    EXPECT_TRUE(rc.IsInitialized());

    // SRV and UAV indices should be valid (not UINT32_MAX)
    EXPECT_NE(rc.GetProbeSHBufferSrv(), UINT32_MAX)
        << "RadianceCache SH buffer SRV index is invalid";
    EXPECT_NE(rc.GetProbeSHBufferUav(), UINT32_MAX)
        << "RadianceCache SH buffer UAV index is invalid";

    // Probe spacing should be positive and consistent with the grid size
    const float expectedSpacing = (5000.0f * 2.0f) / static_cast<float>(uz::RadianceCache::kProbesPerAxis);
    EXPECT_NEAR(rc.GetProbeSpacing(), expectedSpacing, 0.01f)
        << "Probe spacing doesn't match expected value for 5000cm extent, "
        << uz::RadianceCache::kProbesPerAxis << " probes per axis";

    // Destroy should not crash
    rc.DestroyResources();
    EXPECT_FALSE(rc.IsInitialized());
}

/// Verifies double-destroy of radiance cache is safe.
TEST_F(GIGpuTest, RadianceCacheDoubleDestroyIsSafe) {
    uz::RadianceCache rc;
    ASSERT_TRUE(rc.CreateResources(m_device.get()));

    rc.DestroyResources();
    EXPECT_FALSE(rc.IsInitialized());

    // Second destroy should be a no-op
    rc.DestroyResources();
    EXPECT_FALSE(rc.IsInitialized());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7.3c: MeshSDF-style Resource Creation and Destruction
// ─────────────────────────────────────────────────────────────────────────────

/// Creates individual resources matching the MeshSDF pattern (R16F 3D texture +
/// structured buffer for triangle data) and verifies creation/destruction.
/// Does NOT call BakeMeshGPU (requires compute pipeline + shader compilation).
TEST_F(GIGpuTest, MeshSDFResourceCreationAndDestruction) {
    constexpr uint32_t kSdfResolution = 32;

    // Create R16F 3D texture matching MeshSDF baker output format
    uz::RHITextureDesc sdfTexDesc{};
    sdfTexDesc.width            = kSdfResolution;
    sdfTexDesc.height           = kSdfResolution;
    sdfTexDesc.depthOrArraySize = static_cast<uint16_t>(kSdfResolution);
    sdfTexDesc.format           = uz::RHIFormat::R16Float;
    sdfTexDesc.usage            = uz::RHITextureUsage::ShaderResource
                                | uz::RHITextureUsage::UnorderedAccess;
    sdfTexDesc.is3D             = true;
    sdfTexDesc.debugName        = "GI_TestMeshSDF_32";

    uz::RHITextureHandle sdfTexture = m_device->CreateTexture(sdfTexDesc);
    ASSERT_TRUE(sdfTexture.IsValid()) << "Failed to create 32^3 R16F mesh SDF texture";

    // Verify texture descriptor round-trip
    uz::RHITextureDesc readDesc = m_device->GetTextureDesc(sdfTexture);
    EXPECT_EQ(readDesc.width, kSdfResolution);
    EXPECT_EQ(readDesc.height, kSdfResolution);
    EXPECT_EQ(readDesc.depthOrArraySize, kSdfResolution);
    EXPECT_TRUE(readDesc.is3D);
    EXPECT_EQ(readDesc.format, uz::RHIFormat::R16Float);

    // Create a structured buffer matching the triangle upload buffer
    // BakeMeshGPU uploads triangles as StructuredBuffer<float4> (3 float4 per triangle).
    constexpr uint32_t kTriCount      = 12; // 12 triangles (cube = 6 faces x 2)
    constexpr uint32_t kFloat4PerTri  = 3;  // 3 vertices per triangle, each as float4
    constexpr uint32_t kFloat4Stride  = 16; // sizeof(float4)
    constexpr uint32_t kTriBufSize    = kTriCount * kFloat4PerTri * kFloat4Stride; // 576 bytes

    uz::RHIBufferDesc triBufDesc{};
    triBufDesc.sizeBytes = kTriBufSize;
    triBufDesc.stride    = kFloat4Stride;
    triBufDesc.usage     = uz::RHIBufferUsage::UnorderedAccess | uz::RHIBufferUsage::CopySrc;
    triBufDesc.memory    = uz::RHIMemoryType::Default;
    triBufDesc.debugName = "GI_TestMeshSDF_TriBuffer";

    uz::RHIBufferHandle triBuffer = m_device->CreateBuffer(triBufDesc);
    ASSERT_TRUE(triBuffer.IsValid()) << "Failed to create mesh SDF triangle buffer";

    // Destroy in correct order (texture first, then buffer — like MeshSDF::pendingTriBuffer)
    m_device->DestroyTexture(sdfTexture);
    m_device->DestroyBuffer(triBuffer);
}

/// Creates multiple MeshSDF-sized resources concurrently to verify the
/// descriptor heap can handle the allocation pressure.
TEST_F(GIGpuTest, MultipleMeshSDFResourcesDescriptorPressure) {
    constexpr uint32_t kCount      = 8;
    constexpr uint32_t kResolution = 32;

    uz::RHITextureHandle textures[kCount];

    for (uint32_t i = 0; i < kCount; ++i) {
        uz::RHITextureDesc desc{};
        desc.width            = kResolution;
        desc.height           = kResolution;
        desc.depthOrArraySize = static_cast<uint16_t>(kResolution);
        desc.format           = uz::RHIFormat::R16Float;
        desc.usage            = uz::RHITextureUsage::ShaderResource
                              | uz::RHITextureUsage::UnorderedAccess;
        desc.is3D             = true;
        desc.debugName        = "GI_TestMeshSDF_Multi";

        textures[i] = m_device->CreateTexture(desc);
        ASSERT_TRUE(textures[i].IsValid())
            << "Failed to create mesh SDF texture #" << i;
    }

    // Destroy in reverse order (LIFO, like a typical frame teardown)
    for (int i = kCount - 1; i >= 0; --i) {
        m_device->DestroyTexture(textures[i]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7.3d: GDF Update and Voxel Size Computation
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies that GDF Update() tracks the camera and marks clipmaps dirty.
TEST_F(GIGpuTest, GDFUpdateTracksCameraPosition) {
    uz::GlobalDistanceField gdf;
    ASSERT_TRUE(gdf.CreateResources(m_device.get()));

    // Initial update at origin
    gdf.Update(uz::Vec3{0.0f, 0.0f, 0.0f});

    // All clipmap centers should be near origin after first update
    for (uint32_t i = 0; i < uz::GlobalDistanceField::kNumClipmaps; ++i) {
        const auto& clipmap = gdf.GetClipmap(i);
        EXPECT_NEAR(clipmap.center.x, 0.0f, 1.0f) << "Cascade " << i << " center.x";
        EXPECT_NEAR(clipmap.center.y, 0.0f, 1.0f) << "Cascade " << i << " center.y";
        EXPECT_NEAR(clipmap.center.z, 0.0f, 1.0f) << "Cascade " << i << " center.z";
    }

    // Move camera far enough to trigger a re-center on cascade 0.
    // Cascade 0 voxel size = (2500 * 2) / 64 = ~78.125 cm.
    // Camera needs to move more than half a voxel (~39 cm) to trigger dirty.
    gdf.Update(uz::Vec3{100.0f, 0.0f, 0.0f});

    // Cascade 0 center should have shifted to track the camera
    const auto& cascade0 = gdf.GetClipmap(0);
    EXPECT_GT(cascade0.center.x, 0.0f)
        << "Cascade 0 should have shifted in X after camera moved 100cm";

    // Verify voxel sizes are correct for each cascade
    for (uint32_t i = 0; i < uz::GlobalDistanceField::kNumClipmaps; ++i) {
        const float expected = (uz::GlobalDistanceField::kClipmapExtents[i] * 2.0f)
                             / static_cast<float>(uz::GlobalDistanceField::kResolution);
        EXPECT_NEAR(gdf.GetVoxelSize(i), expected, 0.01f)
            << "Cascade " << i << " voxel size mismatch";
    }

    gdf.DestroyResources();
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7.3e: Radiance Cache Grid Geometry
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies that RadianceCache Update() correctly computes grid origin and
/// that the geometry is consistent with the configured extent.
TEST_F(GIGpuTest, RadianceCacheGridGeometry) {
    uz::RadianceCache rc;
    ASSERT_TRUE(rc.CreateResources(m_device.get(), 5000.0f));

    // Update with camera at origin
    rc.Update(uz::Vec3{0.0f, 0.0f, 0.0f});

    // Grid center should be at origin
    const uz::Vec3 center = rc.GetGridCenter();
    EXPECT_NEAR(center.x, 0.0f, 1.0f);
    EXPECT_NEAR(center.y, 0.0f, 1.0f);
    EXPECT_NEAR(center.z, 0.0f, 1.0f);

    // Grid extent should match what was passed to CreateResources
    EXPECT_NEAR(rc.GetGridExtent(), 5000.0f, 0.01f);

    // Grid origin should be center - extent = -5000 on each axis
    const uz::Vec3 origin = rc.GetGridOrigin();
    EXPECT_NEAR(origin.x, -5000.0f, 1.0f);
    EXPECT_NEAR(origin.y, -5000.0f, 1.0f);
    EXPECT_NEAR(origin.z, -5000.0f, 1.0f);

    // Move camera and verify grid tracks
    rc.Update(uz::Vec3{1000.0f, 500.0f, -200.0f});
    const uz::Vec3 newCenter = rc.GetGridCenter();
    // Center should have shifted toward the camera (exact snapping depends on
    // the grid quantization, but it should be closer to the new camera pos)
    EXPECT_GT(newCenter.x, -1.0f)
        << "Grid center should have shifted toward camera in X";

    rc.DestroyResources();
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7.3f: Large Structured Buffer for Radiance Cache SH Data
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a structured buffer sized exactly like the RadianceCache SH buffer
/// (4096 probes * 7 float4s * 16 bytes = 458752 bytes) and verifies the
/// allocation succeeds. This tests that the backend can handle the allocation
/// size required by the radiance cache.
TEST_F(GIGpuTest, StructuredBufferSHProbeSize) {
    constexpr uint32_t kProbes        = uz::RadianceCache::kTotalProbes;       // 4096
    constexpr uint32_t kFloat4PerProbe = uz::RadianceCache::kSHFloat4PerProbe; // 7
    constexpr uint32_t kFloat4Size    = 16; // sizeof(float4)
    constexpr uint64_t kBufferSize    = static_cast<uint64_t>(kProbes)
                                      * kFloat4PerProbe * kFloat4Size;         // 458752

    uz::RHIBufferDesc desc{};
    desc.sizeBytes = kBufferSize;
    desc.stride    = kFloat4Size;
    desc.usage     = uz::RHIBufferUsage::UnorderedAccess;
    desc.memory    = uz::RHIMemoryType::Default;
    desc.debugName = "GI_TestSHProbeBuffer";

    uz::RHIBufferHandle buffer = m_device->CreateBuffer(desc);
    ASSERT_TRUE(buffer.IsValid())
        << "Failed to create SH probe structured buffer ("
        << kBufferSize << " bytes)";

    m_device->DestroyBuffer(buffer);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7.3g: GDF Composite Constants Struct Layout
// ─────────────────────────────────────────────────────────────────────────────

/// Validates that GDF shader constant structs have the expected sizes and
/// alignments. These structs are passed directly to GPU root constants and
/// structured buffers — size mismatches cause silent data corruption.
TEST_F(GIGpuTest, GDFShaderConstantStructLayout) {
    // GdfMeshSdfGPU must be exactly 96 bytes (GPU-friendly, fits 6 float4 rows)
    static_assert(sizeof(uz::GdfMeshSdfGPU) == 96,
        "GdfMeshSdfGPU must be 96 bytes for StructuredBuffer packing");

    // GdfCompositeConstants must be exactly 32 bytes (8 uint32 root constants)
    static_assert(sizeof(uz::GdfCompositeConstants) == 32,
        "GdfCompositeConstants must be 32 bytes (8 root constants)");

    // RadianceCacheTraceConstants must be exactly 192 bytes after the
    // phase16 VFX→DDGI emissive injection extension (4 trailing uint/float
    // fields = 16 bytes appended to the prior 176-byte struct).
    static_assert(sizeof(uz::RadianceCacheTraceConstants) == 192,
        "RadianceCacheTraceConstants must be 192 bytes");

    // Verify field offsets for GdfCompositeConstants (matches root constant layout)
    uz::GdfCompositeConstants cc{};
    cc.outputUav      = 42;
    cc.meshCount      = 7;
    cc.clipmapIndex   = 2;
    cc.resolution     = 64;
    cc.clipmapCenterX = 100.0f;
    cc.clipmapCenterY = 200.0f;
    cc.clipmapCenterZ = 300.0f;
    cc.clipmapExtent  = 2500.0f;

    // Interpret as uint32 array — this is how SetRootConstants reads it
    const auto* asUints = reinterpret_cast<const uint32_t*>(&cc);
    EXPECT_EQ(asUints[0], 42u);   // outputUav
    EXPECT_EQ(asUints[1], 7u);    // meshCount
    EXPECT_EQ(asUints[2], 2u);    // clipmapIndex
    EXPECT_EQ(asUints[3], 64u);   // resolution

    // Float fields: verify they appear at the expected uint32 offsets
    float centerX = 0.0f;
    std::memcpy(&centerX, &asUints[4], sizeof(float));
    EXPECT_NEAR(centerX, 100.0f, 0.001f);

    float extent = 0.0f;
    std::memcpy(&extent, &asUints[7], sizeof(float));
    EXPECT_NEAR(extent, 2500.0f, 0.001f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7.3h: Buffer Upload → Buffer Copy → Readback (no texture involved)
// ─────────────────────────────────────────────────────────────────────────────

/// Validates the buffer-to-buffer copy + readback path, which is used by the
/// GI system for uploading mesh SDF metadata and reading back debug data.
TEST_F(GIGpuTest, BufferUploadCopyReadback) {
    constexpr uint32_t kNumElements = 16;
    constexpr uint32_t kElementSize = sizeof(float);
    constexpr uint64_t kBufferSize  = kNumElements * kElementSize; // 64 bytes

    // Source data: 16 float values
    float srcData[kNumElements];
    for (uint32_t i = 0; i < kNumElements; ++i) {
        srcData[i] = static_cast<float>(i) * 0.1f - 0.8f; // -0.8, -0.7, ..., 0.7
    }

    // Upload buffer
    uz::RHIBufferDesc uploadDesc{};
    uploadDesc.sizeBytes = kBufferSize;
    uploadDesc.usage     = uz::RHIBufferUsage::CopySrc;
    uploadDesc.memory    = uz::RHIMemoryType::Upload;
    uploadDesc.debugName = "GI_TestBufUpload";

    uz::RHIBufferHandle uploadBuf = m_device->CreateBuffer(uploadDesc);
    ASSERT_TRUE(uploadBuf.IsValid());

    // GPU-side buffer (intermediate)
    uz::RHIBufferDesc gpuDesc{};
    gpuDesc.sizeBytes = kBufferSize;
    gpuDesc.usage     = uz::RHIBufferUsage::CopySrc | uz::RHIBufferUsage::CopyDst;
    gpuDesc.memory    = uz::RHIMemoryType::Default;
    gpuDesc.debugName = "GI_TestBufGPU";

    uz::RHIBufferHandle gpuBuf = m_device->CreateBuffer(gpuDesc);
    ASSERT_TRUE(gpuBuf.IsValid());

    // Readback buffer
    uz::RHIBufferDesc readbackDesc{};
    readbackDesc.sizeBytes = kBufferSize;
    readbackDesc.usage     = uz::RHIBufferUsage::CopyDst;
    readbackDesc.memory    = uz::RHIMemoryType::Readback;
    readbackDesc.debugName = "GI_TestBufReadback";

    uz::RHIBufferHandle readbackBuf = m_device->CreateBuffer(readbackDesc);
    ASSERT_TRUE(readbackBuf.IsValid());

    // Map and fill upload buffer
    void* mapped = m_device->MapBuffer(uploadBuf);
    ASSERT_NE(mapped, nullptr);
    std::memcpy(mapped, srcData, kBufferSize);
    m_device->UnmapBuffer(uploadBuf);

    // Execute: upload → barrier → readback
    ExecuteGPUWork([&](uz::IRHICommandList* cmd) {
        cmd->CopyBuffer(uploadBuf, 0, gpuBuf, 0, kBufferSize);

        // Transition gpuBuf from CopyDst to CopySrc before reading from it
        uz::RHIBarrierDesc barrier{};
        barrier.buffer      = gpuBuf;
        barrier.stateBefore = uz::RHIResourceState::CopyDst;
        barrier.stateAfter  = uz::RHIResourceState::CopySrc;
        cmd->ResourceBarrier(&barrier, 1);

        cmd->CopyBuffer(gpuBuf, 0, readbackBuf, 0, kBufferSize);
    });

    // Map readback and verify
    void* readbackMapped = m_device->MapBuffer(readbackBuf);
    ASSERT_NE(readbackMapped, nullptr);

    const auto* resultFloats = static_cast<const float*>(readbackMapped);
    for (uint32_t i = 0; i < kNumElements; ++i) {
        EXPECT_FLOAT_EQ(resultFloats[i], srcData[i])
            << "Mismatch at element " << i;
    }

    m_device->UnmapBuffer(readbackBuf);

    // Cleanup
    m_device->DestroyBuffer(readbackBuf);
    m_device->DestroyBuffer(gpuBuf);
    m_device->DestroyBuffer(uploadBuf);
}

} // namespace

#endif // _WIN32
