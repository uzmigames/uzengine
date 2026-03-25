/// Unit tests for GPUParticleBuffer and ParticleAttributeSet.
///
/// Covers:
///   - ParticleAttributeSet::IsValid() — all valid vs any invalid
///   - GPUParticleBuffer::Create() — success, correct buffer counts
///   - GPUParticleBuffer::Destroy() — all handles reset, safe double-destroy
///   - GPUParticleBuffer::Swap() — readIndex/writeIndex XOR toggle
///   - GPUParticleBuffer::ReadSet()/WriteSet() — correct slot selection
///   - GPUParticleBuffer::IsValid() — before/after Create/Destroy
///
/// All tests are CPU-only — no GPU hardware is required.

#include <gtest/gtest.h>

#include "gpu_particle_buffer.h"
#include "mock_rhi_device.h"

using namespace uz;
using uz::test::MockRHIDevice;
using uz::test::FailingMockRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// ParticleAttributeSet::IsValid
// ─────────────────────────────────────────────────────────────────────────────

/// A default-constructed ParticleAttributeSet has all invalid handles, so
/// IsValid() must return false.
TEST(ParticleAttributeSet, DefaultConstruct_IsInvalid) {
    ParticleAttributeSet s;
    EXPECT_FALSE(s.IsValid())
        << "Default ParticleAttributeSet must be invalid (all handles at generation=0)";
}

/// IsValid() returns true only when every one of the seven attribute buffers
/// carries a non-zero generation.
TEST(ParticleAttributeSet, AllSevenHandlesValid_ReturnsTrue) {
    ParticleAttributeSet s;
    s.positions  = {1, 1};
    s.velocities = {2, 1};
    s.ages       = {3, 1};
    s.lifespans  = {4, 1};
    s.sizes      = {5, 1};
    s.colors     = {6, 1};
    s.rotations  = {7, 1};

    EXPECT_TRUE(s.IsValid())
        << "ParticleAttributeSet must be valid when all seven handles have generation=1";
}

/// If exactly one handle is invalid, IsValid() must return false.
TEST(ParticleAttributeSet, MissingPositions_IsInvalid) {
    ParticleAttributeSet s;
    // All valid except positions.
    s.velocities = {2, 1};
    s.ages       = {3, 1};
    s.lifespans  = {4, 1};
    s.sizes      = {5, 1};
    s.colors     = {6, 1};
    s.rotations  = {7, 1};

    EXPECT_FALSE(s.IsValid())
        << "Missing positions handle must make IsValid() return false";
}

TEST(ParticleAttributeSet, MissingVelocities_IsInvalid) {
    ParticleAttributeSet s;
    s.positions  = {1, 1};
    s.ages       = {3, 1};
    s.lifespans  = {4, 1};
    s.sizes      = {5, 1};
    s.colors     = {6, 1};
    s.rotations  = {7, 1};

    EXPECT_FALSE(s.IsValid());
}

TEST(ParticleAttributeSet, MissingAges_IsInvalid) {
    ParticleAttributeSet s;
    s.positions  = {1, 1};
    s.velocities = {2, 1};
    s.lifespans  = {4, 1};
    s.sizes      = {5, 1};
    s.colors     = {6, 1};
    s.rotations  = {7, 1};

    EXPECT_FALSE(s.IsValid());
}

TEST(ParticleAttributeSet, MissingLifespans_IsInvalid) {
    ParticleAttributeSet s;
    s.positions  = {1, 1};
    s.velocities = {2, 1};
    s.ages       = {3, 1};
    s.sizes      = {5, 1};
    s.colors     = {6, 1};
    s.rotations  = {7, 1};

    EXPECT_FALSE(s.IsValid());
}

TEST(ParticleAttributeSet, MissingSizes_IsInvalid) {
    ParticleAttributeSet s;
    s.positions  = {1, 1};
    s.velocities = {2, 1};
    s.ages       = {3, 1};
    s.lifespans  = {4, 1};
    s.colors     = {6, 1};
    s.rotations  = {7, 1};

    EXPECT_FALSE(s.IsValid());
}

TEST(ParticleAttributeSet, MissingColors_IsInvalid) {
    ParticleAttributeSet s;
    s.positions  = {1, 1};
    s.velocities = {2, 1};
    s.ages       = {3, 1};
    s.lifespans  = {4, 1};
    s.sizes      = {5, 1};
    s.rotations  = {7, 1};

    EXPECT_FALSE(s.IsValid());
}

TEST(ParticleAttributeSet, MissingRotations_IsInvalid) {
    ParticleAttributeSet s;
    s.positions  = {1, 1};
    s.velocities = {2, 1};
    s.ages       = {3, 1};
    s.lifespans  = {4, 1};
    s.sizes      = {5, 1};
    s.colors     = {6, 1};

    EXPECT_FALSE(s.IsValid());
}

// ─────────────────────────────────────────────────────────────────────────────
// GPUParticleBuffer — default state
// ─────────────────────────────────────────────────────────────────────────────

/// Before Create() is called, IsValid() must return false.
TEST(GPUParticleBuffer, DefaultConstruct_IsInvalid) {
    GPUParticleBuffer buf;
    EXPECT_FALSE(buf.IsValid())
        << "Default GPUParticleBuffer must be invalid before Create()";
}

/// Default capacity must be zero.
TEST(GPUParticleBuffer, DefaultConstruct_CapacityIsZero) {
    GPUParticleBuffer buf;
    EXPECT_EQ(buf.capacity, 0u);
}

/// Default readIndex/writeIndex must be 0/1 (ping-pong initial state).
TEST(GPUParticleBuffer, DefaultConstruct_IndicesAreZeroAndOne) {
    GPUParticleBuffer buf;
    EXPECT_EQ(buf.readIndex, 0u);
    EXPECT_EQ(buf.writeIndex, 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// GPUParticleBuffer::Create — success path
// ─────────────────────────────────────────────────────────────────────────────

/// Create() with a valid device and positive capacity returns true.
TEST(GPUParticleBuffer, Create_ValidArgs_ReturnsTrue) {
    MockRHIDevice device;
    GPUParticleBuffer buf;

    const bool ok = buf.Create(&device, 1000u);

    EXPECT_TRUE(ok) << "Create() must return true on success";

    buf.Destroy(&device);
}

/// After successful Create(), IsValid() returns true.
TEST(GPUParticleBuffer, Create_Success_IsValidTrue) {
    MockRHIDevice device;
    GPUParticleBuffer buf;

    buf.Create(&device, 1000u);

    EXPECT_TRUE(buf.IsValid());

    buf.Destroy(&device);
}

/// After Create(), capacity matches what was requested.
TEST(GPUParticleBuffer, Create_Success_CapacityMatchesRequested) {
    MockRHIDevice device;
    GPUParticleBuffer buf;

    buf.Create(&device, 4096u);

    EXPECT_EQ(buf.capacity, 4096u);

    buf.Destroy(&device);
}

/// Create() must allocate exactly the expected number of GPU buffers.
///
/// Per attribute set: 7 attribute buffers.
/// Two sets: 14 attribute buffers.
/// Shared: aliveCount, freeList, freeListHead, idToIndex, indirectDrawArgs,
///          indirectDispatchArgs = 6 shared buffers.
/// Total: 14 + 6 = 20 CreateBuffer calls.
TEST(GPUParticleBuffer, Create_AllocatesCorrectBufferCount) {
    MockRHIDevice device;
    GPUParticleBuffer buf;

    buf.Create(&device, 100u);

    EXPECT_EQ(device.buffersCreated.load(), 20u)
        << "Create() must allocate exactly 20 GPU buffers (14 attribute + 6 shared)";

    buf.Destroy(&device);
}

/// After Create(), both attribute sets must be valid (all 7 handles in each).
TEST(GPUParticleBuffer, Create_BothAttributeSetsValid) {
    MockRHIDevice device;
    GPUParticleBuffer buf;

    buf.Create(&device, 500u);

    EXPECT_TRUE(buf.sets[0].IsValid())
        << "Attribute set A must be valid after Create()";
    EXPECT_TRUE(buf.sets[1].IsValid())
        << "Attribute set B must be valid after Create()";

    buf.Destroy(&device);
}

/// All shared handles must be valid after Create().
TEST(GPUParticleBuffer, Create_SharedBuffersValid) {
    MockRHIDevice device;
    GPUParticleBuffer buf;

    buf.Create(&device, 500u);

    EXPECT_TRUE(buf.aliveCount.IsValid())          << "aliveCount must be valid";
    EXPECT_TRUE(buf.freeList.IsValid())             << "freeList must be valid";
    EXPECT_TRUE(buf.freeListHead.IsValid())         << "freeListHead must be valid";
    EXPECT_TRUE(buf.idToIndex.IsValid())            << "idToIndex must be valid";
    EXPECT_TRUE(buf.indirectDrawArgs.IsValid())     << "indirectDrawArgs must be valid";
    EXPECT_TRUE(buf.indirectDispatchArgs.IsValid()) << "indirectDispatchArgs must be valid";

    buf.Destroy(&device);
}

/// Create() with capacity=1 (minimum valid) must succeed.
TEST(GPUParticleBuffer, Create_CapacityOne_Succeeds) {
    MockRHIDevice device;
    GPUParticleBuffer buf;

    const bool ok = buf.Create(&device, 1u);

    EXPECT_TRUE(ok);
    EXPECT_EQ(buf.capacity, 1u);

    buf.Destroy(&device);
}

// ─────────────────────────────────────────────────────────────────────────────
// GPUParticleBuffer::Create — failure path
// ─────────────────────────────────────────────────────────────────────────────

/// When the device fails to allocate buffers, Create() returns false.
TEST(GPUParticleBuffer, Create_DeviceFailsBufferAlloc_ReturnsFalse) {
    FailingMockRHIDevice failDev;
    GPUParticleBuffer buf;

    const bool ok = buf.Create(&failDev, 1000u);

    EXPECT_FALSE(ok) << "Create() must return false when device cannot allocate buffers";
}

/// After a failed Create(), the buffer must remain invalid.
TEST(GPUParticleBuffer, Create_Failure_BufferRemainsInvalid) {
    FailingMockRHIDevice failDev;
    GPUParticleBuffer buf;

    buf.Create(&failDev, 1000u);

    EXPECT_FALSE(buf.IsValid())
        << "Buffer must remain invalid after a failed Create()";
    EXPECT_EQ(buf.capacity, 0u)
        << "Capacity must be reset to 0 after a failed Create()";
}

// ─────────────────────────────────────────────────────────────────────────────
// GPUParticleBuffer::Destroy
// ─────────────────────────────────────────────────────────────────────────────

/// Destroy() releases all 20 GPU buffers allocated by Create().
TEST(GPUParticleBuffer, Destroy_ReleasesAllBuffers) {
    MockRHIDevice device;
    GPUParticleBuffer buf;
    buf.Create(&device, 1000u);

    buf.Destroy(&device);

    EXPECT_EQ(device.buffersDestroyed.load(), device.buffersCreated.load())
        << "Destroy() must release exactly as many buffers as Create() allocated";
}

/// After Destroy(), IsValid() returns false.
TEST(GPUParticleBuffer, Destroy_IsInvalidAfterwards) {
    MockRHIDevice device;
    GPUParticleBuffer buf;
    buf.Create(&device, 1000u);

    buf.Destroy(&device);

    EXPECT_FALSE(buf.IsValid());
}

/// After Destroy(), all attribute handles must be invalid.
TEST(GPUParticleBuffer, Destroy_AllHandlesInvalid) {
    MockRHIDevice device;
    GPUParticleBuffer buf;
    buf.Create(&device, 1000u);

    buf.Destroy(&device);

    EXPECT_FALSE(buf.sets[0].IsValid())             << "sets[0] must be invalid after Destroy";
    EXPECT_FALSE(buf.sets[1].IsValid())             << "sets[1] must be invalid after Destroy";
    EXPECT_FALSE(buf.aliveCount.IsValid())          << "aliveCount must be invalid after Destroy";
    EXPECT_FALSE(buf.freeList.IsValid())            << "freeList must be invalid after Destroy";
    EXPECT_FALSE(buf.freeListHead.IsValid())        << "freeListHead must be invalid after Destroy";
    EXPECT_FALSE(buf.idToIndex.IsValid())           << "idToIndex must be invalid after Destroy";
    EXPECT_FALSE(buf.indirectDrawArgs.IsValid())    << "indirectDrawArgs must be invalid after Destroy";
    EXPECT_FALSE(buf.indirectDispatchArgs.IsValid()) << "indirectDispatchArgs must be invalid after Destroy";
}

/// After Destroy(), capacity must be reset to zero.
TEST(GPUParticleBuffer, Destroy_CapacityResetToZero) {
    MockRHIDevice device;
    GPUParticleBuffer buf;
    buf.Create(&device, 2048u);

    buf.Destroy(&device);

    EXPECT_EQ(buf.capacity, 0u)
        << "Capacity must be reset to 0 after Destroy()";
}

/// Calling Destroy() twice (double-destroy) must not crash.
/// The second call should be a safe no-op.
TEST(GPUParticleBuffer, Destroy_CalledTwice_NocrashNoExtraDestroys) {
    MockRHIDevice device;
    GPUParticleBuffer buf;
    buf.Create(&device, 512u);

    buf.Destroy(&device);
    const uint32_t destroyCountAfterFirst = device.buffersDestroyed.load();

    // Second Destroy() must not call DestroyBuffer again (handles are already invalid).
    buf.Destroy(&device);

    EXPECT_EQ(device.buffersDestroyed.load(), destroyCountAfterFirst)
        << "Second Destroy() must not call DestroyBuffer (all handles already invalid)";
}

/// Destroy() on a never-created buffer (null device) must be a no-op.
TEST(GPUParticleBuffer, Destroy_NullDevice_IsNoOp) {
    GPUParticleBuffer buf;
    // Should not crash with null device and never-allocated buffer.
    buf.Destroy(nullptr);
    EXPECT_FALSE(buf.IsValid());
}

// ─────────────────────────────────────────────────────────────────────────────
// GPUParticleBuffer::Swap
// ─────────────────────────────────────────────────────────────────────────────

/// After one Swap(), readIndex should be 1 and writeIndex should be 0.
TEST(GPUParticleBuffer, Swap_OnceThenReadIs1WriteIs0) {
    GPUParticleBuffer buf;

    buf.Swap();

    EXPECT_EQ(buf.readIndex,  1u) << "After first Swap(), readIndex must be 1";
    EXPECT_EQ(buf.writeIndex, 0u) << "After first Swap(), writeIndex must be 0";
}

/// Two consecutive Swaps must restore the original indices.
TEST(GPUParticleBuffer, Swap_TwiceRestoresOriginalIndices) {
    GPUParticleBuffer buf;

    buf.Swap();
    buf.Swap();

    EXPECT_EQ(buf.readIndex,  0u) << "After two Swaps, readIndex must return to 0";
    EXPECT_EQ(buf.writeIndex, 1u) << "After two Swaps, writeIndex must return to 1";
}

/// After an odd number of swaps readIndex is 1; after even number it is 0.
TEST(GPUParticleBuffer, Swap_NineSwaps_OddReadIndex) {
    GPUParticleBuffer buf;

    for (int i = 0; i < 9; ++i) buf.Swap();

    EXPECT_EQ(buf.readIndex,  1u);
    EXPECT_EQ(buf.writeIndex, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// GPUParticleBuffer::ReadSet / WriteSet
// ─────────────────────────────────────────────────────────────────────────────

/// Initially ReadSet() references sets[0] and WriteSet() references sets[1].
TEST(GPUParticleBuffer, ReadSet_InitiallySetZero) {
    GPUParticleBuffer buf;
    EXPECT_EQ(&buf.ReadSet(), &buf.sets[0])
        << "ReadSet() must return sets[0] before any Swap()";
}

TEST(GPUParticleBuffer, WriteSet_InitiallySetOne) {
    GPUParticleBuffer buf;
    EXPECT_EQ(&buf.WriteSet(), &buf.sets[1])
        << "WriteSet() must return sets[1] before any Swap()";
}

/// After one Swap(), ReadSet references sets[1] and WriteSet references sets[0].
TEST(GPUParticleBuffer, ReadSet_AfterOneSwap_ReturnsSetOne) {
    GPUParticleBuffer buf;
    buf.Swap();

    EXPECT_EQ(&buf.ReadSet(), &buf.sets[1])
        << "ReadSet() must return sets[1] after one Swap()";
    EXPECT_EQ(&buf.WriteSet(), &buf.sets[0])
        << "WriteSet() must return sets[0] after one Swap()";
}

/// ReadSet and WriteSet never refer to the same slot simultaneously.
TEST(GPUParticleBuffer, ReadSetAndWriteSet_NeverSameSlot) {
    GPUParticleBuffer buf;

    for (int i = 0; i < 4; ++i) {
        EXPECT_NE(&buf.ReadSet(), &buf.WriteSet())
            << "ReadSet() and WriteSet() must never return the same slot (swap " << i << ")";
        buf.Swap();
    }
}

/// Const ReadSet overload returns the same address as the non-const overload.
TEST(GPUParticleBuffer, WriteSet_ConstOverload_SameAddress) {
    GPUParticleBuffer buf;
    const GPUParticleBuffer& constBuf = buf;

    EXPECT_EQ(&constBuf.WriteSet(), &buf.WriteSet());
}
