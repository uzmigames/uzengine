/// Unit tests for GPUParticleSort and its supporting types.
///
/// Covers:
///   - NextPowerOfTwo() — boundary values, already-power-of-two, large inputs
///   - kSortGroupSize constant — exact value
///   - VFXSortConstants struct — size, field offsets, field roundtrip
///   - GPUParticleSort default construction — IsValid(), capacity, paddedCount,
///     handle validity, GetPaddedCount()
///   - GPUParticleSort::Create() — paddedCount rounding, IsValid(), buffer count,
///     capacity correctness, failure path
///   - GPUParticleSort::Destroy() — state reset, handle invalidation, safe on
///     never-created instance, safe with null device, double-destroy idempotency
///
/// All tests are CPU-only — no GPU hardware is required.

#include <gtest/gtest.h>

#include "gpu_particle_sort.h"
#include "mock_rhi_device.h"

using namespace uz;
using uz::test::MockRHIDevice;
using uz::test::FailingMockRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// kSortGroupSize
// ─────────────────────────────────────────────────────────────────────────────

/// The thread group size constant must equal 256 to match the compute shader's
/// [numthreads(256, 1, 1)] declaration.
TEST(GPUParticleSort, kSortGroupSize_Is256) {
    EXPECT_EQ(kSortGroupSize, 256u)
        << "kSortGroupSize must be 256 to match particle_sort_cs.hlsl numthreads";
}

// ─────────────────────────────────────────────────────────────────────────────
// NextPowerOfTwo — boundary values
// ─────────────────────────────────────────────────────────────────────────────

/// Input 0 must return 1 (the defined edge case: no valid power-of-two below 1).
TEST(NextPowerOfTwo, Zero_ReturnsOne) {
    EXPECT_EQ(NextPowerOfTwo(0u), 1u)
        << "NextPowerOfTwo(0) must return 1 per the documented edge case";
}

/// Input 1 is already a power of two — must return 1.
TEST(NextPowerOfTwo, One_ReturnsOne) {
    EXPECT_EQ(NextPowerOfTwo(1u), 1u);
}

/// Input 2 is already a power of two — must return 2.
TEST(NextPowerOfTwo, Two_ReturnsTwo) {
    EXPECT_EQ(NextPowerOfTwo(2u), 2u);
}

/// Input 3 is not a power of two — must round up to 4.
TEST(NextPowerOfTwo, Three_ReturnsFour) {
    EXPECT_EQ(NextPowerOfTwo(3u), 4u);
}

/// Input 255 is one below a power-of-two boundary — must round up to 256.
TEST(NextPowerOfTwo, TwoFiftyFive_Returns256) {
    EXPECT_EQ(NextPowerOfTwo(255u), 256u);
}

/// Input 256 is already a power of two — must return 256.
TEST(NextPowerOfTwo, TwoFiftySix_Returns256) {
    EXPECT_EQ(NextPowerOfTwo(256u), 256u);
}

/// Input 257 is one above a power-of-two boundary — must round up to 512.
TEST(NextPowerOfTwo, TwoFiftySeven_Returns512) {
    EXPECT_EQ(NextPowerOfTwo(257u), 512u);
}

/// Input 50000 must round up to 65536 (2^16).
TEST(NextPowerOfTwo, FiftyThousand_Returns65536) {
    EXPECT_EQ(NextPowerOfTwo(50000u), 65536u);
}

/// Input 65536 is already a power of two — must return 65536.
TEST(NextPowerOfTwo, SixtyFiveThirtySix_Returns65536) {
    EXPECT_EQ(NextPowerOfTwo(65536u), 65536u);
}

/// Input 1000000 must round up to 1048576 (2^20).
TEST(NextPowerOfTwo, OneMillion_Returns1048576) {
    EXPECT_EQ(NextPowerOfTwo(1000000u), 1048576u);
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXSortConstants — layout
// ─────────────────────────────────────────────────────────────────────────────

/// The struct must be exactly 80 bytes so that a memcpy produces the correct
/// GPU cbuffer layout expected by particle_sort_cs.hlsl.
TEST(VFXSortConstants, SizeIs80Bytes) {
    static_assert(sizeof(VFXSortConstants) == 80,
                  "VFXSortConstants size mismatch — GPU cbuffer will break");
    EXPECT_EQ(sizeof(VFXSortConstants), 80u);
}

/// particleCount is the first field at byte 0.
TEST(VFXSortConstants, ParticleCount_OffsetIsZero) {
    EXPECT_EQ(offsetof(VFXSortConstants, particleCount), 0u)
        << "particleCount must be at byte 0 (Row 0, first field)";
}

/// cameraPosX starts Row 1 at byte 16.
TEST(VFXSortConstants, CameraPosX_OffsetIs16) {
    EXPECT_EQ(offsetof(VFXSortConstants, cameraPosX), 16u)
        << "cameraPosX must be at byte 16 (Row 1 start)";
}

/// cameraForwardX starts Row 2 at byte 32.
TEST(VFXSortConstants, CameraForwardX_OffsetIs32) {
    EXPECT_EQ(offsetof(VFXSortConstants, cameraForwardX), 32u)
        << "cameraForwardX must be at byte 32 (Row 2 start)";
}

/// positionsSrvIndex starts Row 3 at byte 48.
TEST(VFXSortConstants, PositionsSrvIndex_OffsetIs48) {
    EXPECT_EQ(offsetof(VFXSortConstants, positionsSrvIndex), 48u)
        << "positionsSrvIndex must be at byte 48 (Row 3 start)";
}

/// bitonicBlockSize starts Row 4 at byte 64.
TEST(VFXSortConstants, BitonicBlockSize_OffsetIs64) {
    EXPECT_EQ(offsetof(VFXSortConstants, bitonicBlockSize), 64u)
        << "bitonicBlockSize must be at byte 64 (Row 4 start)";
}

/// All fields must be writable and readable back with the exact values written.
TEST(VFXSortConstants, AllFields_Roundtrip) {
    VFXSortConstants cb{};
    cb.particleCount       = 512u;
    cb.sortPhase           = 1u;
    cb.paddedCount         = 512u;
    cb._pad0               = 0u;
    cb.cameraPosX          = 1.0f;
    cb.cameraPosY          = 2.0f;
    cb.cameraPosZ          = 3.0f;
    cb._pad1               = 0.0f;
    cb.cameraForwardX      = 0.0f;
    cb.cameraForwardY      = 0.0f;
    cb.cameraForwardZ      = 1.0f;
    cb._pad2               = 0.0f;
    cb.positionsSrvIndex   = 7u;
    cb.sortKeysUavIndex    = 8u;
    cb.sortIndicesUavIndex = 9u;
    cb._pad3               = 0u;
    cb.bitonicBlockSize    = 512u;
    cb.bitonicSubBlockSize = 256u;
    cb._pad4               = 0u;
    cb._pad5               = 0u;

    EXPECT_EQ(cb.particleCount,       512u);
    EXPECT_EQ(cb.sortPhase,           1u);
    EXPECT_EQ(cb.paddedCount,         512u);
    EXPECT_FLOAT_EQ(cb.cameraPosX,    1.0f);
    EXPECT_FLOAT_EQ(cb.cameraPosY,    2.0f);
    EXPECT_FLOAT_EQ(cb.cameraPosZ,    3.0f);
    EXPECT_FLOAT_EQ(cb.cameraForwardX, 0.0f);
    EXPECT_FLOAT_EQ(cb.cameraForwardY, 0.0f);
    EXPECT_FLOAT_EQ(cb.cameraForwardZ, 1.0f);
    EXPECT_EQ(cb.positionsSrvIndex,   7u);
    EXPECT_EQ(cb.sortKeysUavIndex,    8u);
    EXPECT_EQ(cb.sortIndicesUavIndex, 9u);
    EXPECT_EQ(cb.bitonicBlockSize,    512u);
    EXPECT_EQ(cb.bitonicSubBlockSize, 256u);
}

// ─────────────────────────────────────────────────────────────────────────────
// GPUParticleSort — default construction
// ─────────────────────────────────────────────────────────────────────────────

/// A default-constructed GPUParticleSort has never had buffers allocated,
/// so IsValid() must return false.
TEST(GPUParticleSort, DefaultConstruct_IsInvalid) {
    GPUParticleSort sort;
    EXPECT_FALSE(sort.IsValid())
        << "Default GPUParticleSort must be invalid before Create()";
}

/// Default capacity must be zero.
TEST(GPUParticleSort, DefaultConstruct_CapacityIsZero) {
    GPUParticleSort sort;
    EXPECT_EQ(sort.capacity, 0u);
}

/// Default paddedCount must be zero.
TEST(GPUParticleSort, DefaultConstruct_PaddedCountIsZero) {
    GPUParticleSort sort;
    EXPECT_EQ(sort.paddedCount, 0u);
}

/// GetPaddedCount() on a default instance must return 0.
TEST(GPUParticleSort, DefaultConstruct_GetPaddedCountReturnsZero) {
    GPUParticleSort sort;
    EXPECT_EQ(sort.GetPaddedCount(), 0u);
}

/// The sortKeys handle on a default instance must be invalid (generation == 0).
TEST(GPUParticleSort, DefaultConstruct_SortKeysHandleInvalid) {
    GPUParticleSort sort;
    EXPECT_FALSE(sort.sortKeys.IsValid())
        << "sortKeys must be invalid before Create()";
}

/// The sortIndices handle on a default instance must be invalid.
TEST(GPUParticleSort, DefaultConstruct_SortIndicesHandleInvalid) {
    GPUParticleSort sort;
    EXPECT_FALSE(sort.sortIndices.IsValid())
        << "sortIndices must be invalid before Create()";
}

// ─────────────────────────────────────────────────────────────────────────────
// GPUParticleSort::Create — success path
// ─────────────────────────────────────────────────────────────────────────────

/// Create() must return true when called with a valid device and capacity > 0.
TEST(GPUParticleSort, Create_ValidArgs_ReturnsTrue) {
    MockRHIDevice device;
    GPUParticleSort sort;

    const bool ok = sort.Create(&device, 100u);

    EXPECT_TRUE(ok) << "Create() must return true on success";

    sort.Destroy(&device);
}

/// After successful Create(), IsValid() must return true.
TEST(GPUParticleSort, Create_Success_IsValidTrue) {
    MockRHIDevice device;
    GPUParticleSort sort;

    sort.Create(&device, 100u);

    EXPECT_TRUE(sort.IsValid());

    sort.Destroy(&device);
}

/// Create() with capacity 100 (not a power of two) must set paddedCount to 128.
TEST(GPUParticleSort, Create_Capacity100_PaddedCountIs128) {
    MockRHIDevice device;
    GPUParticleSort sort;

    sort.Create(&device, 100u);

    EXPECT_EQ(sort.paddedCount, 128u)
        << "paddedCount must be NextPowerOfTwo(100) = 128";

    sort.Destroy(&device);
}

/// Create() with an exact power-of-two capacity must set paddedCount to the
/// same value (no rounding up).
TEST(GPUParticleSort, Create_Capacity256_PaddedCountIs256) {
    MockRHIDevice device;
    GPUParticleSort sort;

    sort.Create(&device, 256u);

    EXPECT_EQ(sort.paddedCount, 256u)
        << "paddedCount must be NextPowerOfTwo(256) = 256 (already a power of two)";

    sort.Destroy(&device);
}

/// After Create(), the stored capacity must equal what was requested.
TEST(GPUParticleSort, Create_CapacityStored) {
    MockRHIDevice device;
    GPUParticleSort sort;

    sort.Create(&device, 100u);

    EXPECT_EQ(sort.capacity, 100u)
        << "capacity must equal the requested particleCapacity argument";

    sort.Destroy(&device);
}

/// Create() must allocate exactly 2 GPU buffers (sortKeys + sortIndices).
TEST(GPUParticleSort, Create_AllocatesExactlyTwoBuffers) {
    MockRHIDevice device;
    GPUParticleSort sort;

    sort.Create(&device, 512u);

    EXPECT_EQ(device.buffersCreated.load(), 2u)
        << "Create() must allocate exactly 2 buffers: sortKeys and sortIndices";

    sort.Destroy(&device);
}

/// After Create(), both buffer handles must be valid.
TEST(GPUParticleSort, Create_BothHandlesValid) {
    MockRHIDevice device;
    GPUParticleSort sort;

    sort.Create(&device, 64u);

    EXPECT_TRUE(sort.sortKeys.IsValid())    << "sortKeys must be valid after Create()";
    EXPECT_TRUE(sort.sortIndices.IsValid()) << "sortIndices must be valid after Create()";

    sort.Destroy(&device);
}

/// GetPaddedCount() must return paddedCount after a successful Create().
TEST(GPUParticleSort, Create_GetPaddedCountMatchesPaddedCount) {
    MockRHIDevice device;
    GPUParticleSort sort;

    sort.Create(&device, 200u);

    EXPECT_EQ(sort.GetPaddedCount(), sort.paddedCount)
        << "GetPaddedCount() must equal paddedCount after Create()";

    sort.Destroy(&device);
}

// ─────────────────────────────────────────────────────────────────────────────
// GPUParticleSort::Create — failure path
// ─────────────────────────────────────────────────────────────────────────────

/// When the device cannot allocate buffers, Create() must return false.
TEST(GPUParticleSort, Create_DeviceFails_ReturnsFalse) {
    FailingMockRHIDevice failDev;
    GPUParticleSort sort;

    const bool ok = sort.Create(&failDev, 128u);

    EXPECT_FALSE(ok) << "Create() must return false when device cannot allocate buffers";
}

/// After a failed Create(), the struct must remain in its pre-Create() state:
/// IsValid() == false, capacity == 0, paddedCount == 0.
TEST(GPUParticleSort, Create_Failure_RemainsInvalid) {
    FailingMockRHIDevice failDev;
    GPUParticleSort sort;

    sort.Create(&failDev, 128u);

    EXPECT_FALSE(sort.IsValid())       << "GPUParticleSort must be invalid after failed Create()";
    EXPECT_EQ(sort.capacity,    0u)    << "capacity must be 0 after failed Create()";
    EXPECT_EQ(sort.paddedCount, 0u)    << "paddedCount must be 0 after failed Create()";
    EXPECT_FALSE(sort.sortKeys.IsValid())    << "sortKeys must be invalid after failed Create()";
    EXPECT_FALSE(sort.sortIndices.IsValid()) << "sortIndices must be invalid after failed Create()";
}

// ─────────────────────────────────────────────────────────────────────────────
// GPUParticleSort::Destroy
// ─────────────────────────────────────────────────────────────────────────────

/// Destroy() must release all buffers allocated by Create().
TEST(GPUParticleSort, Destroy_ReleasesAllBuffers) {
    MockRHIDevice device;
    GPUParticleSort sort;
    sort.Create(&device, 512u);

    sort.Destroy(&device);

    EXPECT_EQ(device.buffersDestroyed.load(), device.buffersCreated.load())
        << "Destroy() must release exactly as many buffers as Create() allocated";
}

/// After Destroy(), IsValid() must return false.
TEST(GPUParticleSort, Destroy_IsInvalidAfterwards) {
    MockRHIDevice device;
    GPUParticleSort sort;
    sort.Create(&device, 256u);

    sort.Destroy(&device);

    EXPECT_FALSE(sort.IsValid());
}

/// After Destroy(), capacity must be reset to 0.
TEST(GPUParticleSort, Destroy_CapacityResetToZero) {
    MockRHIDevice device;
    GPUParticleSort sort;
    sort.Create(&device, 1024u);

    sort.Destroy(&device);

    EXPECT_EQ(sort.capacity, 0u)
        << "capacity must be 0 after Destroy()";
}

/// After Destroy(), paddedCount must be reset to 0.
TEST(GPUParticleSort, Destroy_PaddedCountResetToZero) {
    MockRHIDevice device;
    GPUParticleSort sort;
    sort.Create(&device, 300u);

    sort.Destroy(&device);

    EXPECT_EQ(sort.paddedCount, 0u)
        << "paddedCount must be 0 after Destroy()";
}

/// After Destroy(), both buffer handles must be invalid.
TEST(GPUParticleSort, Destroy_AllHandlesInvalid) {
    MockRHIDevice device;
    GPUParticleSort sort;
    sort.Create(&device, 128u);

    sort.Destroy(&device);

    EXPECT_FALSE(sort.sortKeys.IsValid())
        << "sortKeys must be invalid after Destroy()";
    EXPECT_FALSE(sort.sortIndices.IsValid())
        << "sortIndices must be invalid after Destroy()";
}

/// Calling Destroy() twice must not crash and must not call DestroyBuffer a
/// second time (all handles are already invalid after the first Destroy()).
TEST(GPUParticleSort, Destroy_CalledTwice_SafeNoOp) {
    MockRHIDevice device;
    GPUParticleSort sort;
    sort.Create(&device, 64u);

    sort.Destroy(&device);
    const uint32_t destroysAfterFirst = device.buffersDestroyed.load();

    // Second Destroy() — handles are already invalid, so no DestroyBuffer calls
    // should be issued.
    sort.Destroy(&device);

    EXPECT_EQ(device.buffersDestroyed.load(), destroysAfterFirst)
        << "Second Destroy() must not issue additional DestroyBuffer calls";
    EXPECT_FALSE(sort.IsValid());
}

/// Destroy() on a never-created instance with a null device must be a no-op
/// and must not crash.
TEST(GPUParticleSort, Destroy_NullDevice_IsNoOp) {
    GPUParticleSort sort;

    // Must not crash even though device is null and buffers were never created.
    sort.Destroy(nullptr);

    EXPECT_FALSE(sort.IsValid());
    EXPECT_EQ(sort.capacity,    0u);
    EXPECT_EQ(sort.paddedCount, 0u);
}

/// Destroy() on a never-created instance with a valid device must be a no-op:
/// no DestroyBuffer calls should be issued because both handles are invalid.
TEST(GPUParticleSort, Destroy_UninitializedWithDevice_NoDestroyBufferCalls) {
    MockRHIDevice device;
    GPUParticleSort sort;

    sort.Destroy(&device);

    EXPECT_EQ(device.buffersDestroyed.load(), 0u)
        << "Destroy() on a never-created instance must not call DestroyBuffer";
}
