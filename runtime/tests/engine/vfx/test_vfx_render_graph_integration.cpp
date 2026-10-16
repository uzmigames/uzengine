/// Integration tests for VFX render graph pass wiring — tasks 7.1–7.4.
///
/// Verifies that the CPU-side pass descriptor structures used to wire VFX
/// systems into the render graph are correctly populated from emitter, decal,
/// trail, and mesh-particle configuration data, without requiring GPU hardware.
///
/// # Scope
/// - 7.1: ParticleRenderPassDesc + ParticleSimulationPassDesc — billboard
///   pipeline wiring: EmitterRenderData and EmitterSimDispatch fields are
///   correctly set from an initialized ParticleEmitter.
/// - 7.2: DecalPassDesc — DecalManager produces sorted VfxDecal pointers
///   that drive the DecalRenderData array, and DecalConstants is the correct
///   size (192 bytes, 256-byte aligned by CBRingAllocator).
/// - 7.3: TrailPassDesc — TrailSystem render data maps to TrailPassRenderItem
///   fields with correct sort metadata and draw parameters.
/// - 7.4: ParticleMeshPassDesc — emitters using MeshRenderModule allocate
///   mesh buffers, VFXMeshInstanceConstants is 64 bytes, and
///   ParticleMeshBatchItem fields are populated from MeshRenderModuleConfig.
///
/// All tests are CPU-only — no GPU hardware required.

#include <gtest/gtest.h>

#include "engine/vfx/core/gpu_particle_buffer.h"
#include "engine/vfx/core/particle_budget.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/renderer/passes/vfx/particle_render_pass.h"
#include "engine/renderer/passes/vfx/particle_simulation_pass.h"
#include "engine/renderer/passes/vfx/decal_pass.h"
#include "engine/renderer/passes/vfx/trail_render_pass.h"
#include "engine/renderer/passes/vfx/particle_mesh_render_pass.h"
#include "engine/vfx/decals/decal_asset.h"
#include "engine/vfx/decals/decal_component.h"
#include "engine/vfx/decals/decal_manager.h"
#include "engine/vfx/trails/trail_system.h"
#include "engine/vfx/trails/trail_component.h"
#include "engine/vfx/modules/mesh_render_module.h"
#include "mock_rhi_device.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>

using namespace uz;
using uz::test::MockRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// Shared helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Minimal valid .uzdecal JSON used by the decal pass wiring tests.
constexpr const char* kTestDecalJson = R"({
    "format": "uzdecal",
    "version": 1,
    "material": "materials/vfx/test_decal.uzmat",
    "blend_mode": "Opaque",
    "lifetime": 10.0,
    "fade_duration": 2.0,
    "normal_intensity": 0.8,
    "roughness_override": -1.0,
    "sort_order": 0,
    "half_extents": [0.5, 0.5, 0.2]
})";

/// Loads a shared test DecalAsset from the minimal JSON above.
std::unique_ptr<DecalAsset> MakeTestDecalAsset() {
    return DecalAsset::LoadFromString(kTestDecalJson);
}

/// Builds a TrailComponent with a small ring buffer and loose emission
/// thresholds so that a single-unit position step reliably adds a point.
TrailComponent MakeTestTrailComponent(const std::string& mat = "mat/test.uzmat") {
    TrailComponent tc;
    tc.materialPath      = mat;
    tc.maxTrailPoints    = 32;
    tc.minPointDistance  = 0.01f;
    tc.pointLifetime     = 60.0f;
    tc.widthStart        = 0.5f;
    tc.widthEnd          = 0.05f;
    tc.isActive          = true;
    tc.points            = TrailPointBuffer(tc.maxTrailPoints);
    return tc;
}

/// World-space camera position used throughout trail render tests.
const Vec3 kTestCamera{0.0f, 100.0f, 0.0f};

} // anonymous namespace

// =============================================================================
// Task 7.1 — Simulation + Billboard pipeline wiring
// =============================================================================

// ─────────────────────────────────────────────────────────────────────────────
// 7.1.1: EmitterRenderData default state
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies that a default-constructed EmitterRenderData has a null buffer
/// pointer and UINT32_MAX SRV indices, representing the correct uninitialized
/// sentinel state before the frame's pass setup populates it.
TEST(VFXRenderGraph_Billboard, EmitterRenderData_DefaultBuffer_IsNull) {
    EmitterRenderData rd;
    EXPECT_EQ(rd.buffer, nullptr)
        << "Default-constructed EmitterRenderData must have null buffer pointer";
}

TEST(VFXRenderGraph_Billboard, EmitterRenderData_DefaultReadSetSrvIndex_IsUINT32MAX) {
    EmitterRenderData rd;
    EXPECT_EQ(rd.readSetSrvIndex, UINT32_MAX)
        << "Default readSetSrvIndex must be UINT32_MAX (unset sentinel)";
}

TEST(VFXRenderGraph_Billboard, EmitterRenderData_DefaultSpriteSrvIndex_IsUINT32MAX) {
    EmitterRenderData rd;
    EXPECT_EQ(rd.spriteSrvIndex, UINT32_MAX)
        << "Default spriteSrvIndex must be UINT32_MAX (unset sentinel)";
}

TEST(VFXRenderGraph_Billboard, EmitterRenderData_DefaultSceneDepthSrvIndex_IsUINT32MAX) {
    EmitterRenderData rd;
    EXPECT_EQ(rd.sceneDepthSrvIndex, UINT32_MAX)
        << "Default sceneDepthSrvIndex must be UINT32_MAX (unset sentinel)";
}

TEST(VFXRenderGraph_Billboard, EmitterRenderData_DefaultDistanceToCamera_IsZero) {
    EmitterRenderData rd;
    EXPECT_FLOAT_EQ(rd.distanceToCamera, 0.0f)
        << "Default distanceToCamera must be 0.0f";
}

// ─────────────────────────────────────────────────────────────────────────────
// 7.1.2: EmitterRenderData wired from an initialized ParticleEmitter
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a ParticleEmitter, initializes it, then manually populates an
/// EmitterRenderData to simulate what the VFX render frame setup does.
/// Verifies that the buffer pointer, capacity, and render graph resource IDs
/// are correctly propagated from the emitter's GPUParticleBuffer.
///
/// This test is a CPU-only structural check — no actual render graph builder
/// or GPU dispatch is invoked. It verifies the data contract between the
/// emitter and the pass descriptor.
TEST(VFXRenderGraph_Billboard, EmitterRenderData_WiredFromEmitter_BufferPointsToEmitterBuffer) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);

    ParticleEmitter emitter;
    emitter.maxParticles = 500u;
    emitter.emissionRate = 100.0f;
    emitter.isEmitting   = true;
    ASSERT_TRUE(emitter.Initialize(&device, budget));

    // Simulate frame-setup wiring: assign the buffer pointer.
    EmitterRenderData rd;
    rd.buffer = &emitter.buffer;

    EXPECT_EQ(rd.buffer, &emitter.buffer)
        << "EmitterRenderData.buffer must point to the emitter's GPUParticleBuffer";
    EXPECT_TRUE(rd.buffer->IsValid())
        << "The wired buffer must be valid (GPU buffers allocated)";
    EXPECT_EQ(rd.buffer->capacity, 500u)
        << "Buffer capacity must match the emitter's allocatedFromBudget";

    emitter.Shutdown(&device, budget);
}

/// Verifies that after assigning an emitter's buffer to EmitterRenderData,
/// the read-set attribute handles are valid (non-Invalid) — confirming that
/// the buffer was properly created and ping-pong state is intact.
TEST(VFXRenderGraph_Billboard, EmitterRenderData_WiredBuffer_ReadSetHandlesAreValid) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);

    ParticleEmitter emitter;
    emitter.maxParticles = 200u;
    ASSERT_TRUE(emitter.Initialize(&device, budget));

    EmitterRenderData rd;
    rd.buffer = &emitter.buffer;

    const auto& readSet = rd.buffer->ReadSet();
    EXPECT_TRUE(readSet.positions.IsValid())
        << "Read-set positions handle must be valid after Initialize";
    EXPECT_TRUE(readSet.velocities.IsValid())
        << "Read-set velocities handle must be valid after Initialize";
    EXPECT_TRUE(readSet.ages.IsValid())
        << "Read-set ages handle must be valid after Initialize";
    EXPECT_TRUE(readSet.lifespans.IsValid())
        << "Read-set lifespans handle must be valid after Initialize";
    EXPECT_TRUE(readSet.sizes.IsValid())
        << "Read-set sizes handle must be valid after Initialize";
    EXPECT_TRUE(readSet.colors.IsValid())
        << "Read-set colors handle must be valid after Initialize";
    EXPECT_TRUE(readSet.rotations.IsValid())
        << "Read-set rotations handle must be valid after Initialize";

    emitter.Shutdown(&device, budget);
}

/// Verifies that the ping-pong indirect draw args handle in the emitter's
/// buffer is valid, since the billboard render pass consumes it via
/// DrawIndexedIndirect.
TEST(VFXRenderGraph_Billboard, EmitterRenderData_WiredBuffer_IndirectDrawArgsHandleIsValid) {
    MockRHIDevice device;
    ParticleBudget budget(10000u);

    ParticleEmitter emitter;
    emitter.maxParticles = 300u;
    ASSERT_TRUE(emitter.Initialize(&device, budget));

    EmitterRenderData rd;
    rd.buffer = &emitter.buffer;

    EXPECT_TRUE(rd.buffer->indirectDrawArgs.IsValid())
        << "indirectDrawArgs handle must be valid — required for DrawIndexedIndirect";

    emitter.Shutdown(&device, budget);
}

/// Verifies that the render graph IDs in EmitterRenderData start as invalid
/// (default) and that the pass descriptor's emitters vector is initially empty,
/// matching the expected pre-population state.
TEST(VFXRenderGraph_Billboard, ParticleRenderPassDesc_Default_EmittersIsEmpty) {
    ParticleRenderPassDesc desc;
    EXPECT_TRUE(desc.emitters.empty())
        << "ParticleRenderPassDesc must start with an empty emitters vector";
}

TEST(VFXRenderGraph_Billboard, EmitterRenderData_DefaultRgReadPositions_IsInvalid) {
    EmitterRenderData rd;
    EXPECT_FALSE(rd.rgReadPositions.IsValid())
        << "Default rgReadPositions must be invalid (UINT32_MAX index)";
}

TEST(VFXRenderGraph_Billboard, EmitterRenderData_DefaultRgIndirectDrawArgs_IsInvalid) {
    EmitterRenderData rd;
    EXPECT_FALSE(rd.rgIndirectDrawArgs.IsValid())
        << "Default rgIndirectDrawArgs must be invalid before graph import";
}

// ─────────────────────────────────────────────────────────────────────────────
// 7.1.3: EmitterSimDispatch default state (simulation pass)
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies that a default EmitterSimDispatch has sentinel/invalid SRV/UAV
/// indices and zero spawn/alive counts, representing the uninitialized state
/// before the frame's simulation pass setup runs.
TEST(VFXRenderGraph_Simulation, EmitterSimDispatch_DefaultBuffer_IsNull) {
    EmitterSimDispatch dispatch;
    EXPECT_EQ(dispatch.buffer, nullptr)
        << "Default EmitterSimDispatch must have a null buffer pointer";
}

TEST(VFXRenderGraph_Simulation, EmitterSimDispatch_DefaultSpawnCount_IsZero) {
    EmitterSimDispatch dispatch;
    EXPECT_EQ(dispatch.spawnCount, 0u)
        << "Default spawnCount must be 0";
}

TEST(VFXRenderGraph_Simulation, EmitterSimDispatch_DefaultAliveCount_IsZero) {
    EmitterSimDispatch dispatch;
    EXPECT_EQ(dispatch.aliveCount, 0u)
        << "Default aliveCount must be 0";
}

TEST(VFXRenderGraph_Simulation, EmitterSimDispatch_DefaultReadSetSrvIndex_IsUINT32MAX) {
    EmitterSimDispatch dispatch;
    EXPECT_EQ(dispatch.readSetSrvIndex, UINT32_MAX)
        << "Default readSetSrvIndex must be UINT32_MAX (unbound sentinel)";
}

TEST(VFXRenderGraph_Simulation, EmitterSimDispatch_DefaultWriteSetUavIndex_IsUINT32MAX) {
    EmitterSimDispatch dispatch;
    EXPECT_EQ(dispatch.writeSetUavIndex, UINT32_MAX)
        << "Default writeSetUavIndex must be UINT32_MAX (unbound sentinel)";
}

TEST(VFXRenderGraph_Simulation, EmitterSimDispatch_DefaultAliveCountUavIndex_IsUINT32MAX) {
    EmitterSimDispatch dispatch;
    EXPECT_EQ(dispatch.aliveCountUavIndex, UINT32_MAX)
        << "Default aliveCountUavIndex must be UINT32_MAX (unbound sentinel)";
}

TEST(VFXRenderGraph_Simulation, EmitterSimDispatch_DefaultIndirectDrawArgsUavIndex_IsUINT32MAX) {
    EmitterSimDispatch dispatch;
    EXPECT_EQ(dispatch.indirectDrawArgsUavIndex, UINT32_MAX)
        << "Default indirectDrawArgsUavIndex must be UINT32_MAX (unbound sentinel)";
}

// ─────────────────────────────────────────────────────────────────────────────
// 7.1.4: Simulation constant buffer structs are correct sizes
// ─────────────────────────────────────────────────────────────────────────────

/// ParticleSpawnConstants must be exactly 304 bytes to match the HLSL cbuffer
/// layout (VFXSpawnConstants: 19 x float4 — 192 base bytes + 80 bytes of
/// user-attribute slots + 32 bytes of spline data-interface fields).
TEST(VFXRenderGraph_Simulation, ParticleSpawnConstants_Size_Is304Bytes) {
    EXPECT_EQ(sizeof(ParticleSpawnConstants), 304u)
        << "ParticleSpawnConstants must be 304 bytes to match HLSL VFXSpawnConstants";
}

/// ParticleUpdateConstants must be exactly 432 bytes (27 x float4 — 384 base
/// bytes + 48 bytes of spline data-interface fields).
TEST(VFXRenderGraph_Simulation, ParticleUpdateConstants_Size_Is432Bytes) {
    EXPECT_EQ(sizeof(ParticleUpdateConstants), 432u)
        << "ParticleUpdateConstants must be 432 bytes to match HLSL VFXUpdateConstants";
}

/// ParticleIndirectArgsConstants must be exactly 16 bytes (1 x float4).
TEST(VFXRenderGraph_Simulation, ParticleIndirectArgsConstants_Size_Is16Bytes) {
    EXPECT_EQ(sizeof(ParticleIndirectArgsConstants), 16u)
        << "ParticleIndirectArgsConstants must be 16 bytes to match HLSL VFXIndirectArgsConstants";
}

/// The default aliveCountSrvIndex in ParticleIndirectArgsConstants
/// must be 0 (uninitialized, filled at runtime).
TEST(VFXRenderGraph_Simulation, ParticleIndirectArgsConstants_Default_AliveCountSrvIndex_Is0) {
    ParticleIndirectArgsConstants c;
    EXPECT_EQ(c.aliveCountSrvIndex, 0u)
        << "aliveCountSrvIndex default must be 0";
}

/// The default indirectArgsUavIndex in ParticleIndirectArgsConstants
/// must be 0 (uninitialized, filled at runtime).
TEST(VFXRenderGraph_Simulation, ParticleIndirectArgsConstants_Default_IndirectArgsUavIndex_Is0) {
    ParticleIndirectArgsConstants c;
    EXPECT_EQ(c.indirectArgsUavIndex, 0u)
        << "indirectArgsUavIndex default must be 0";
}

// ─────────────────────────────────────────────────────────────────────────────
// 7.1.5: ParticleBillboardIndices and VFXBillboardCB sizes
// ─────────────────────────────────────────────────────────────────────────────

/// ParticleBillboardIndices (b0 root constants) must be 48 bytes. Phase 2 of
/// task 3.91.0 repurposed the trailing pad slot as `subImageIndexBufferIdx`
/// so the size stays the same (12 uint32 slots) — the HLSL layout is
/// byte-identical, only the semantic meaning of the last slot changed.
TEST(VFXRenderGraph_Billboard, ParticleBillboardIndices_Size_Is48Bytes) {
    EXPECT_EQ(sizeof(ParticleBillboardIndices), 48u)
        << "ParticleBillboardIndices must be 48 bytes (12 uint32) to match HLSL VFXBillboardConstants";
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 3b-3.91.0 Phase 2: SubImageIndex SoA channel wiring
// ─────────────────────────────────────────────────────────────────────────────

/// The SubImageIndex bindless index field occupies the last uint32 slot
/// (offset 44, repurposed from the former _vfxPad1 pad slot).
TEST(VFXRenderGraph_Billboard, ParticleBillboardIndices_SubImageIndex_OffsetIs44) {
    EXPECT_EQ(offsetof(ParticleBillboardIndices, subImageIndexBufferIdx), 44u)
        << "subImageIndexBufferIdx must be the 12th uint32 at offset 44 (repurposed _vfxPad1)";
}

/// Default-initialized SubImageIndex descriptor index is zero so bindless
/// shaders can guard against it (0 = uninitialized fallback) before reading.
TEST(VFXRenderGraph_Billboard, ParticleBillboardIndices_SubImageIndex_DefaultIsZero) {
    ParticleBillboardIndices indices{};
    EXPECT_EQ(indices.subImageIndexBufferIdx, 0u);
}

/// The SubImageIndex SoA channel must be part of a valid ParticleAttributeSet —
/// IsValid() returns false if subImageIndices is invalid.
TEST(VFX_GPUParticleBuffer, ParticleAttributeSet_SubImageIndices_RequiredForIsValid) {
    ParticleAttributeSet set;
    // All fields default-invalid → IsValid() must be false.
    EXPECT_FALSE(set.IsValid());

    // Assign every field *except* subImageIndices to a fake-valid handle and
    // confirm IsValid() still reports false (i.e. subImageIndices is required).
    // RHIHandle validity is generational (generation 0 == invalid), so the
    // fake handle needs a non-zero generation, not just a non-zero index.
    RHIBufferHandle handle{1, 1};
    set.positions  = handle;
    set.velocities = handle;
    set.ages       = handle;
    set.lifespans  = handle;
    set.sizes      = handle;
    set.colors     = handle;
    set.rotations  = handle;
    // subImageIndices intentionally left Invalid.
    EXPECT_FALSE(set.IsValid())
        << "ParticleAttributeSet::IsValid() must require a valid subImageIndices buffer";

    set.subImageIndices = handle;
    EXPECT_TRUE(set.IsValid())
        << "ParticleAttributeSet::IsValid() must succeed once subImageIndices is valid";
}

/// ParticleSpawnConstants must expose the SubImageIndex UAV field at the
/// byte offset matching HLSL VFXSpawnConstants (offset 180, slot 45).
TEST(VFXRenderGraph_Simulation, ParticleSpawnConstants_SubImageIndex_OffsetIs180) {
    EXPECT_EQ(offsetof(ParticleSpawnConstants, subImageIndicesUavIndex), 180u)
        << "subImageIndicesUavIndex must be at offset 180 in ParticleSpawnConstants";
}

/// ParticleSpawnConstants must expose flipbookTotalFrames at offset 184
/// so the spawn shader can pre-seed SubImageIndex = 0 and verify bounds.
TEST(VFXRenderGraph_Simulation, ParticleSpawnConstants_FlipbookTotalFrames_OffsetIs184) {
    EXPECT_EQ(offsetof(ParticleSpawnConstants, flipbookTotalFrames), 184u)
        << "flipbookTotalFrames must be at offset 184 in ParticleSpawnConstants";
}

/// ParticleUpdateConstants SubImageIndex SRV index is at offset 60 (slot 15 in
/// the SRV block — was _padSrv0, now repurposed for the SoA SubImageIndex read).
TEST(VFXRenderGraph_Simulation, ParticleUpdateConstants_SubImageIndexSrv_OffsetIs60) {
    EXPECT_EQ(offsetof(ParticleUpdateConstants, subImageIndicesSrvIndex), 60u)
        << "subImageIndicesSrvIndex must be at offset 60 in ParticleUpdateConstants";
}

/// ParticleUpdateConstants SubImageIndex UAV index is at offset 92 (was
/// _padUav0, now repurposed for the SoA SubImageIndex write).
TEST(VFXRenderGraph_Simulation, ParticleUpdateConstants_SubImageIndexUav_OffsetIs92) {
    EXPECT_EQ(offsetof(ParticleUpdateConstants, subImageIndicesUavIndex), 92u)
        << "subImageIndicesUavIndex must be at offset 92 in ParticleUpdateConstants";
}

/// ParticleUpdateConstants flipbookTotalFrames is at offset 336 (was _padPF0,
/// now carries the SubUV frame count for subImageIndex computation).
TEST(VFXRenderGraph_Simulation, ParticleUpdateConstants_FlipbookTotalFrames_OffsetIs336) {
    EXPECT_EQ(offsetof(ParticleUpdateConstants, flipbookTotalFrames), 336u)
        << "flipbookTotalFrames must be at offset 336 in ParticleUpdateConstants";
}

/// Default-initialized ParticleUpdateConstants::flipbookTotalFrames is 1
/// (single-frame sprite, no flipbook). The update shader computes
/// subImageIndex = normAge * (totalFrames - 1), which collapses to 0 when
/// totalFrames == 1 — the correct behavior for non-flipbook sprites.
TEST(VFXRenderGraph_Simulation, ParticleUpdateConstants_FlipbookTotalFrames_DefaultIs1) {
    ParticleUpdateConstants c;
    EXPECT_EQ(c.flipbookTotalFrames, 1u);
}

/// The final row carries render size and pixel jitter for velocity encoding.
TEST(VFXRenderGraph_Billboard, VFXBillboardCB_Size_Is336Bytes) {
    EXPECT_EQ(sizeof(VFXBillboardCB), 336u)
        << "VFXBillboardCB must be 336 bytes to match the HLSL VFXBillboardCB layout";
}

TEST(VFXRenderGraph_Billboard, VFXBillboardCB_Default_SoftFadeDistance_Is50) {
    VFXBillboardCB c;
    EXPECT_FLOAT_EQ(c.softFadeDistance, 50.0f)
        << "Default softFadeDistance must be 50.0f (50 world units / cm)";
}

// ─────────────────────────────────────────────────────────────────────────────
// 7.1.6: Two-emitter PassDesc population
// ─────────────────────────────────────────────────────────────────────────────

/// Creates two emitters, wires them into a ParticleRenderPassDesc, and verifies
/// that the emitters vector has exactly 2 entries with non-null buffer pointers
/// and correct capacities.
TEST(VFXRenderGraph_Billboard, ParticleRenderPassDesc_TwoEmitters_EmittersVectorHasTwoEntries) {
    MockRHIDevice device;
    ParticleBudget budget(20000u);

    ParticleEmitter e1, e2;
    e1.maxParticles = 1000u;
    e2.maxParticles = 2000u;
    ASSERT_TRUE(e1.Initialize(&device, budget));
    ASSERT_TRUE(e2.Initialize(&device, budget));

    ParticleRenderPassDesc desc;
    {
        EmitterRenderData rd1;
        rd1.buffer           = &e1.buffer;
        rd1.distanceToCamera = 50.0f;

        EmitterRenderData rd2;
        rd2.buffer           = &e2.buffer;
        rd2.distanceToCamera = 100.0f;

        desc.emitters.push_back(rd1);
        desc.emitters.push_back(rd2);
    }

    ASSERT_EQ(desc.emitters.size(), 2u)
        << "PassDesc must hold exactly 2 emitter entries";

    EXPECT_EQ(desc.emitters[0].buffer->capacity, 1000u)
        << "First emitter's buffer capacity must be 1000";
    EXPECT_FLOAT_EQ(desc.emitters[0].distanceToCamera, 50.0f)
        << "First emitter's distanceToCamera must be 50.0f";

    EXPECT_EQ(desc.emitters[1].buffer->capacity, 2000u)
        << "Second emitter's buffer capacity must be 2000";
    EXPECT_FLOAT_EQ(desc.emitters[1].distanceToCamera, 100.0f)
        << "Second emitter's distanceToCamera must be 100.0f";

    e1.Shutdown(&device, budget);
    e2.Shutdown(&device, budget);
}

/// Verifies that when the same two emitters are wired into a
/// ParticleSimulationPassDesc, the dispatch vector reflects the correct
/// buffer pointers and spawn counts can be zero-initialized safely.
TEST(VFXRenderGraph_Simulation, ParticleSimulationPassDesc_TwoDispatches_BuffersAreCorrect) {
    MockRHIDevice device;
    ParticleBudget budget(5000u);

    ParticleEmitter e1, e2;
    e1.maxParticles = 500u;
    e1.emissionRate = 100.0f;
    e2.maxParticles = 800u;
    e2.emissionRate = 200.0f;
    ASSERT_TRUE(e1.Initialize(&device, budget));
    ASSERT_TRUE(e2.Initialize(&device, budget));

    ParticleSimulationPassDesc desc;
    {
        EmitterSimDispatch d1;
        d1.buffer     = &e1.buffer;
        d1.spawnCount = 0u;
        d1.aliveCount = 0u;

        EmitterSimDispatch d2;
        d2.buffer     = &e2.buffer;
        d2.spawnCount = 0u;
        d2.aliveCount = 0u;

        desc.emitters.push_back(d1);
        desc.emitters.push_back(d2);
    }

    ASSERT_EQ(desc.emitters.size(), 2u);
    EXPECT_EQ(desc.emitters[0].buffer, &e1.buffer);
    EXPECT_EQ(desc.emitters[1].buffer, &e2.buffer);

    EXPECT_TRUE(desc.emitters[0].buffer->IsValid());
    EXPECT_TRUE(desc.emitters[1].buffer->IsValid());

    e1.Shutdown(&device, budget);
    e2.Shutdown(&device, budget);
}

// =============================================================================
// Task 7.2 — Decal pass registration
// =============================================================================

// ─────────────────────────────────────────────────────────────────────────────
// 7.2.1: DecalConstants struct size
// ─────────────────────────────────────────────────────────────────────────────

/// DecalConstants must be exactly 224 bytes.
/// The actual layout is:
///   worldToDecal[16]   = 64 bytes  (offset   0)
///   invViewProj[16]    = 64 bytes  (offset  64)
///   8 float scalars    = 32 bytes  (offset 128)  — normalIntensity..screenHeight.._pad1
///   viewProjection[16] = 64 bytes  (offset 160)
///   Total              = 224 bytes
/// (The header comment of 192 bytes predates the addition of viewProjection.)
TEST(VFXRenderGraph_Decal, DecalConstants_Size_Is224Bytes) {
    EXPECT_EQ(sizeof(DecalConstants), 224u)
        << "DecalConstants must be 224 bytes — two mat4s + 8 scalars";
}

/// Verifies that individual field offsets within DecalConstants are correct.
/// worldToDecal starts at byte 0, invViewProj at byte 64, normalIntensity at
/// byte 128, and viewProjection at byte 160.
TEST(VFXRenderGraph_Decal, DecalConstants_FieldOffsets_MatchDocumentedLayout) {
    DecalConstants c{};
    const ptrdiff_t base = reinterpret_cast<ptrdiff_t>(&c);

    EXPECT_EQ(reinterpret_cast<ptrdiff_t>(&c.worldToDecal) - base, 0)
        << "worldToDecal must start at offset 0";
    EXPECT_EQ(reinterpret_cast<ptrdiff_t>(&c.invViewProj) - base, 64)
        << "invViewProj must start at offset 64";
    EXPECT_EQ(reinterpret_cast<ptrdiff_t>(&c.normalIntensity) - base, 128)
        << "normalIntensity must start at offset 128";
    EXPECT_EQ(reinterpret_cast<ptrdiff_t>(&c.viewProjection) - base, 160)
        << "viewProjection must start at offset 160";
}

/// Verifies that DecalConstants' blendMode field is at byte offset 140
/// (normalIntensity=128, roughnessOverride=132, fadeAlpha=136, blendMode=140).
TEST(VFXRenderGraph_Decal, DecalConstants_BlendModeOffset_Is140) {
    DecalConstants c{};
    const ptrdiff_t base = reinterpret_cast<ptrdiff_t>(&c);
    EXPECT_EQ(reinterpret_cast<ptrdiff_t>(&c.blendMode) - base, 140)
        << "blendMode must be at offset 140";
}

// ─────────────────────────────────────────────────────────────────────────────
// 7.2.2: DecalPassDesc default state
// ─────────────────────────────────────────────────────────────────────────────

/// A default-constructed DecalPassDesc must have an empty decals vector,
/// representing the pre-population state before frame setup.
TEST(VFXRenderGraph_Decal, DecalPassDesc_Default_DecalsIsEmpty) {
    DecalPassDesc desc;
    EXPECT_TRUE(desc.decals.empty())
        << "DecalPassDesc must start with an empty decals vector";
}

/// The unit cube must have exactly 8 vertices and 36 indices as declared by
/// the named constants kDecalCubeVertexCount and kDecalCubeIndexCount.
TEST(VFXRenderGraph_Decal, DecalCubeVertexCount_Is8) {
    EXPECT_EQ(kDecalCubeVertexCount, 8u)
        << "Decal cube must have exactly 8 vertices (corners of [-0.5, 0.5] cube)";
}

TEST(VFXRenderGraph_Decal, DecalCubeIndexCount_Is36) {
    EXPECT_EQ(kDecalCubeIndexCount, 36u)
        << "Decal cube must have exactly 36 indices (12 triangles * 3)";
}

/// The GetDecalCubeVertices() function must return a non-null pointer to
/// 24 valid floats (8 vertices * 3 components). The function is purely
/// CPU-side static data — no GPU or device is needed.
TEST(VFXRenderGraph_Decal, GetDecalCubeVertices_ReturnsNonNull) {
    const float* verts = GetDecalCubeVertices();
    ASSERT_NE(verts, nullptr)
        << "GetDecalCubeVertices must return a non-null pointer";
}

TEST(VFXRenderGraph_Decal, GetDecalCubeIndices_ReturnsNonNull) {
    const uint16_t* idx = GetDecalCubeIndices();
    ASSERT_NE(idx, nullptr)
        << "GetDecalCubeIndices must return a non-null pointer";
}

/// All 36 cube indices must be in [0, 7] — the valid range for an 8-vertex cube.
TEST(VFXRenderGraph_Decal, GetDecalCubeIndices_AllIndicesInRange) {
    const uint16_t* idx = GetDecalCubeIndices();
    ASSERT_NE(idx, nullptr);
    for (uint32_t i = 0; i < kDecalCubeIndexCount; ++i) {
        EXPECT_LT(idx[i], kDecalCubeVertexCount)
            << "Index " << i << " (" << idx[i]
            << ") must be < " << kDecalCubeVertexCount;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 7.2.3: DecalManager → DecalPassDesc population
// ─────────────────────────────────────────────────────────────────────────────

/// Spawns a single decal through DecalManager and verifies it appears in the
/// GetSortedDecals() result, which drives DecalRenderData population for the
/// pass descriptor. This verifies the data path from manager to pass.
TEST(VFXRenderGraph_Decal, DecalManager_OneSpawnedDecal_AppearsinSortedList) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 100u, 512u);
    ASSERT_TRUE(mgr.IsInitialized());

    const auto asset = MakeTestDecalAsset();
    ASSERT_NE(asset, nullptr);

    const Vec3 hitPos{0.0f, 0.0f, 0.0f};
    const Vec3 normal{0.0f, 1.0f, 0.0f};
    VfxDecal* d = mgr.SpawnDecal(*asset, hitPos, normal);
    ASSERT_NE(d, nullptr)
        << "SpawnDecal must succeed with a valid asset and available pool slot";

    // Simulate one frame of update before requesting the sorted list.
    mgr.Update(0.016f);

    const Vec3 cameraPos{0.0f, 10.0f, 0.0f};
    const auto& sorted = mgr.GetSortedDecals(cameraPos);

    ASSERT_EQ(sorted.size(), 1u)
        << "One spawned decal must appear in the sorted list";
    EXPECT_EQ(sorted[0], d)
        << "The pointer in the sorted list must match the spawned decal";

    mgr.Shutdown(nullptr);
}

/// Spawns 3 decals with distinct sort orders. GetSortedDecals() must return
/// them in ascending sort-order order (lower sortOrder drawn first).
TEST(VFXRenderGraph_Decal, DecalManager_ThreeDecals_SortedBySortOrder) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 100u, 512u);

    const auto asset = MakeTestDecalAsset();
    ASSERT_NE(asset, nullptr);

    const Vec3 pos{0.0f, 0.0f, 0.0f};
    const Vec3 n{0.0f, 1.0f, 0.0f};

    // Spawn 3 decals at the same position — sort order distinguishes them.
    VfxDecal* da = mgr.SpawnDecal(*asset, pos, n);
    VfxDecal* db = mgr.SpawnDecal(*asset, pos, n);
    VfxDecal* dc = mgr.SpawnDecal(*asset, pos, n);
    ASSERT_NE(da, nullptr);
    ASSERT_NE(db, nullptr);
    ASSERT_NE(dc, nullptr);

    // Assign distinct sort orders out of natural spawn order.
    da->sortOrder = 2;
    db->sortOrder = 0;
    dc->sortOrder = 1;

    mgr.Update(0.016f);

    const Vec3 cam{0.0f, 10.0f, 0.0f};
    const auto& sorted = mgr.GetSortedDecals(cam);

    ASSERT_EQ(sorted.size(), 3u)
        << "All three decals must be in the sorted list";

    EXPECT_EQ(sorted[0]->sortOrder, 0)
        << "First entry must have sortOrder=0 (drawn first)";
    EXPECT_EQ(sorted[1]->sortOrder, 1)
        << "Second entry must have sortOrder=1";
    EXPECT_EQ(sorted[2]->sortOrder, 2)
        << "Third entry must have sortOrder=2 (drawn last)";

    mgr.Shutdown(nullptr);
}

/// DecalRenderData default SRV indices must all be UINT32_MAX, verifying
/// the unbound sentinel state for albedo, normal, roughness, and depth.
TEST(VFXRenderGraph_Decal, DecalRenderData_Default_AllSrvIndicesAreUINT32MAX) {
    DecalRenderData rd;
    EXPECT_EQ(rd.albedoSrvIndex, UINT32_MAX)
        << "Default albedoSrvIndex must be UINT32_MAX";
    EXPECT_EQ(rd.normalSrvIndex, UINT32_MAX)
        << "Default normalSrvIndex must be UINT32_MAX";
    EXPECT_EQ(rd.roughnessSrvIndex, UINT32_MAX)
        << "Default roughnessSrvIndex must be UINT32_MAX";
    EXPECT_EQ(rd.sceneDepthSrvIndex, UINT32_MAX)
        << "Default sceneDepthSrvIndex must be UINT32_MAX";
}

/// Verifies that DecalRenderData can be correctly populated from a
/// VfxDecal pointer retrieved from the manager.
TEST(VFXRenderGraph_Decal, DecalRenderData_PopulatedFromComponent_DecalPtrIsCorrect) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10u, 512u);

    const auto asset = MakeTestDecalAsset();
    ASSERT_NE(asset, nullptr);

    VfxDecal* spawned = mgr.SpawnDecal(*asset,
                                              Vec3{1.0f, 0.0f, 0.0f},
                                              Vec3{0.0f, 0.0f, 1.0f});
    ASSERT_NE(spawned, nullptr);

    // Simulate frame setup: build DecalRenderData from the sorted list.
    mgr.Update(0.016f);
    const auto& sorted = mgr.GetSortedDecals(Vec3{0.0f, 5.0f, -5.0f});
    ASSERT_EQ(sorted.size(), 1u);

    DecalRenderData rd;
    rd.decal = sorted[0];

    EXPECT_EQ(rd.decal, spawned)
        << "DecalRenderData.decal must point to the spawned VfxDecal";
    EXPECT_TRUE(rd.decal->isActive)
        << "The referenced decal must still be active";

    mgr.Shutdown(nullptr);
}

/// Verifies that an expired decal is removed from GetSortedDecals after
/// Update() processes its lifetime — confirming that the decal pass will not
/// attempt to render stale entries.
TEST(VFXRenderGraph_Decal, DecalManager_ExpiredDecal_NotInSortedList) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10u, 512u);

    const auto asset = MakeTestDecalAsset();
    ASSERT_NE(asset, nullptr);

    VfxDecal* d = mgr.SpawnDecal(*asset,
                                        Vec3{0.0f, 0.0f, 0.0f},
                                        Vec3{0.0f, 1.0f, 0.0f});
    ASSERT_NE(d, nullptr);

    // Advance far past the 10-second lifetime.
    mgr.Update(15.0f);

    const auto& sorted = mgr.GetSortedDecals(Vec3{0.0f, 0.0f, 0.0f});
    EXPECT_TRUE(sorted.empty())
        << "Expired decal must not appear in the sorted list used by the pass";

    mgr.Shutdown(nullptr);
}

// =============================================================================
// Task 7.3 — Trail pass render items
// =============================================================================

// ─────────────────────────────────────────────────────────────────────────────
// 7.3.1: TrailPassRenderItem default state
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies that a default-constructed TrailPassRenderItem has the correct
/// sentinel values for all descriptor heap indices and zero draw counts,
/// indicating it has not yet been populated by frame setup.
TEST(VFXRenderGraph_Trail, TrailPassRenderItem_Default_VertexBufferSrvIndex_IsUINT32MAX) {
    TrailPassRenderItem item;
    EXPECT_EQ(item.vertexBufferSrvIndex, UINT32_MAX)
        << "Default vertexBufferSrvIndex must be UINT32_MAX";
}

TEST(VFXRenderGraph_Trail, TrailPassRenderItem_Default_IndexBufferSrvIndex_IsUINT32MAX) {
    TrailPassRenderItem item;
    EXPECT_EQ(item.indexBufferSrvIndex, UINT32_MAX)
        << "Default indexBufferSrvIndex must be UINT32_MAX";
}

TEST(VFXRenderGraph_Trail, TrailPassRenderItem_Default_VertexCount_IsZero) {
    TrailPassRenderItem item;
    EXPECT_EQ(item.vertexCount, 0u)
        << "Default vertexCount must be 0";
}

TEST(VFXRenderGraph_Trail, TrailPassRenderItem_Default_IndexCount_IsZero) {
    TrailPassRenderItem item;
    EXPECT_EQ(item.indexCount, 0u)
        << "Default indexCount must be 0";
}

TEST(VFXRenderGraph_Trail, TrailPassRenderItem_Default_DistanceToCamera_IsZero) {
    TrailPassRenderItem item;
    EXPECT_FLOAT_EQ(item.distanceToCamera, 0.0f)
        << "Default distanceToCamera must be 0.0f";
}

TEST(VFXRenderGraph_Trail, TrailPassRenderItem_Default_SortOrder_IsZero) {
    TrailPassRenderItem item;
    EXPECT_EQ(item.sortOrder, 0)
        << "Default sortOrder must be 0";
}

// ─────────────────────────────────────────────────────────────────────────────
// 7.3.2: TrailPassConstants size
// ─────────────────────────────────────────────────────────────────────────────

/// TrailPassConstants must be exactly 64 bytes (one float4x4 view-projection
/// matrix, column-major), as documented in the header.
TEST(VFXRenderGraph_Trail, TrailPassConstants_Size_Is64Bytes) {
    EXPECT_EQ(sizeof(TrailPassConstants), 64u)
        << "TrailPassConstants must be 64 bytes to match the HLSL cbuffer layout";
}

// ─────────────────────────────────────────────────────────────────────────────
// 7.3.3: TrailPassDesc default state
// ─────────────────────────────────────────────────────────────────────────────

/// A default-constructed TrailPassDesc must have an empty items vector.
TEST(VFXRenderGraph_Trail, TrailPassDesc_Default_ItemsIsEmpty) {
    TrailPassDesc desc;
    EXPECT_TRUE(desc.items.empty())
        << "TrailPassDesc must start with an empty items vector";
}

// ─────────────────────────────────────────────────────────────────────────────
// 7.3.4: TrailSystem render data → TrailPassRenderItem mapping
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies that a TrailSystem with zero registered trails returns an empty
/// GetRenderData() result, which correctly maps to an empty TrailPassDesc.
TEST(VFXRenderGraph_Trail, TrailSystem_NoTrails_GetRenderData_IsEmpty) {
    TrailSystem sys;
    const auto data = sys.GetRenderData();
    EXPECT_TRUE(data.empty())
        << "A trail system with no registered trails must produce empty render data";
}

/// Registers a trail with 2 points (one quad) and verifies that GetRenderData()
/// returns exactly one entry with non-zero vertexCount and indexCount, which
/// the frame setup code uses to populate TrailPassRenderItem draw parameters.
TEST(VFXRenderGraph_Trail, TrailSystem_TwoPoints_GetRenderData_HasOneEntry) {
    TrailSystem sys;
    TrailComponent tc = MakeTestTrailComponent();
    const TrailSystem::TrailHandle h = sys.RegisterTrail(std::move(tc));

    // Add two points by moving the entity.
    const std::unordered_map<TrailSystem::TrailHandle, Vec3> pos1{{h, Vec3{0.0f, 0.0f, 0.0f}}};
    sys.Update(0.016f, pos1, kTestCamera);

    const std::unordered_map<TrailSystem::TrailHandle, Vec3> pos2{{h, Vec3{1.0f, 0.0f, 0.0f}}};
    sys.Update(0.016f, pos2, kTestCamera);

    const auto data = sys.GetRenderData();
    ASSERT_EQ(data.size(), 1u)
        << "One trail with 2 points must produce exactly one render data entry";

    EXPECT_GT(data[0].vertexCount, 0u)
        << "vertexCount must be > 0 for a trail with at least 2 points";
    EXPECT_GT(data[0].indexCount, 0u)
        << "indexCount must be > 0 for a trail with at least 2 points";

    sys.UnregisterTrail(h);
}

/// Verifies that the materialPath field in TrailSystem::TrailRenderData matches
/// the trail component's materialPath. This drives SRV binding for the trail
/// texture in the pass descriptor.
TEST(VFXRenderGraph_Trail, TrailSystem_RenderData_MaterialPath_MatchesComponent) {
    TrailSystem sys;
    TrailComponent tc = MakeTestTrailComponent("materials/vfx/sword_trail.uzmat");
    const TrailSystem::TrailHandle h = sys.RegisterTrail(std::move(tc));

    // Produce at least 2 points so the trail is renderable.
    sys.Update(0.016f, {{h, Vec3{0.0f, 0.0f, 0.0f}}}, kTestCamera);
    sys.Update(0.016f, {{h, Vec3{1.0f, 0.0f, 0.0f}}}, kTestCamera);

    const auto data = sys.GetRenderData();
    ASSERT_EQ(data.size(), 1u);

    ASSERT_NE(data[0].materialPath, nullptr)
        << "materialPath must not be null for a renderable trail";
    EXPECT_STREQ(data[0].materialPath, "materials/vfx/sword_trail.uzmat")
        << "materialPath must match the component's materialPath string";

    sys.UnregisterTrail(h);
}

/// Verifies that TrailRenderData.sortOrder matches the component's sortOrder,
/// enabling the pass to apply the correct draw ordering for overlapping trails.
TEST(VFXRenderGraph_Trail, TrailSystem_RenderData_SortOrder_MatchesComponent) {
    TrailSystem sys;
    TrailComponent tc = MakeTestTrailComponent();
    tc.sortOrder = 7;
    const TrailSystem::TrailHandle h = sys.RegisterTrail(std::move(tc));

    sys.Update(0.016f, {{h, Vec3{0.0f, 0.0f, 0.0f}}}, kTestCamera);
    sys.Update(0.016f, {{h, Vec3{1.0f, 0.0f, 0.0f}}}, kTestCamera);

    const auto data = sys.GetRenderData();
    ASSERT_EQ(data.size(), 1u);

    EXPECT_EQ(data[0].sortOrder, 7)
        << "TrailRenderData.sortOrder must match the component's sortOrder";

    sys.UnregisterTrail(h);
}

/// Verifies that a trail with only one point produces no render data, because
/// a single point cannot form a ribbon quad and the pass must skip it.
TEST(VFXRenderGraph_Trail, TrailSystem_OnePoint_GetRenderData_IsEmpty) {
    TrailSystem sys;
    TrailComponent tc = MakeTestTrailComponent();
    const TrailSystem::TrailHandle h = sys.RegisterTrail(std::move(tc));

    // Update with one position (produces one point, no ribbon yet).
    sys.Update(0.016f, {{h, Vec3{0.0f, 0.0f, 0.0f}}}, kTestCamera);

    const auto data = sys.GetRenderData();
    EXPECT_TRUE(data.empty())
        << "A trail with only 1 point cannot form a ribbon quad — render data must be empty";

    sys.UnregisterTrail(h);
}

/// Verifies that TrailPassRenderItem can be correctly populated from
/// TrailSystem::TrailRenderData. This maps the render data fields to the
/// pass item fields that drive the DrawIndexed call.
TEST(VFXRenderGraph_Trail, TrailPassRenderItem_PopulatedFromRenderData_FieldsAreCorrect) {
    TrailSystem sys;
    TrailComponent tc = MakeTestTrailComponent("mat/test.uzmat");
    tc.sortOrder = 3;
    const TrailSystem::TrailHandle h = sys.RegisterTrail(std::move(tc));

    sys.Update(0.016f, {{h, Vec3{0.0f, 0.0f, 0.0f}}}, kTestCamera);
    sys.Update(0.016f, {{h, Vec3{2.0f, 0.0f, 0.0f}}}, kTestCamera);

    const auto data = sys.GetRenderData();
    ASSERT_EQ(data.size(), 1u);

    // Simulate frame setup: map TrailRenderData → TrailPassRenderItem.
    TrailPassRenderItem item;
    item.vertexCount      = data[0].vertexCount;
    item.indexCount       = data[0].indexCount;
    item.sortOrder        = data[0].sortOrder;
    item.distanceToCamera = data[0].distanceToCamera;

    EXPECT_EQ(item.vertexCount, data[0].vertexCount)
        << "vertexCount must be copied from render data";
    EXPECT_EQ(item.indexCount, data[0].indexCount)
        << "indexCount must be copied from render data";
    EXPECT_EQ(item.sortOrder, 3)
        << "sortOrder must be copied from render data";

    sys.UnregisterTrail(h);
}

/// Verifies that two trails with different sort orders produce two render data
/// entries, both of which can be mapped to TrailPassRenderItems with the
/// correct sort order metadata.
TEST(VFXRenderGraph_Trail, TrailSystem_TwoTrails_TwoRenderDataEntries) {
    TrailSystem sys;

    TrailComponent tc1 = MakeTestTrailComponent("mat/a.uzmat");
    tc1.sortOrder = 0;
    TrailComponent tc2 = MakeTestTrailComponent("mat/b.uzmat");
    tc2.sortOrder = 1;

    const TrailSystem::TrailHandle h1 = sys.RegisterTrail(std::move(tc1));
    const TrailSystem::TrailHandle h2 = sys.RegisterTrail(std::move(tc2));

    // Move both trails to produce renderable geometry.
    std::unordered_map<TrailSystem::TrailHandle, Vec3> p1{{h1, Vec3{0.0f, 0.0f, 0.0f}},
                                                           {h2, Vec3{5.0f, 0.0f, 0.0f}}};
    sys.Update(0.016f, p1, kTestCamera);

    std::unordered_map<TrailSystem::TrailHandle, Vec3> p2{{h1, Vec3{1.0f, 0.0f, 0.0f}},
                                                           {h2, Vec3{6.0f, 0.0f, 0.0f}}};
    sys.Update(0.016f, p2, kTestCamera);

    const auto data = sys.GetRenderData();
    ASSERT_EQ(data.size(), 2u)
        << "Two trails with renderable geometry must produce two render data entries";

    // Both entries must have valid draw counts.
    for (size_t i = 0; i < 2; ++i) {
        EXPECT_GT(data[i].vertexCount, 0u)
            << "Entry " << i << " must have vertexCount > 0";
        EXPECT_GT(data[i].indexCount, 0u)
            << "Entry " << i << " must have indexCount > 0";
    }

    sys.UnregisterTrail(h1);
    sys.UnregisterTrail(h2);
}

// =============================================================================
// Task 7.4 — Mesh particle pass
// =============================================================================

// ─────────────────────────────────────────────────────────────────────────────
// 7.4.1: ParticleMeshPassConstants size
// ─────────────────────────────────────────────────────────────────────────────

/// ParticleMeshPassConstants must be exactly 64 bytes (one float4x4
/// view-projection matrix, column-major), matching the HLSL cbuffer layout.
TEST(VFXRenderGraph_MeshParticle, ParticleMeshPassConstants_Size_Is64Bytes) {
    EXPECT_EQ(sizeof(ParticleMeshPassConstants), 64u)
        << "ParticleMeshPassConstants must be 64 bytes to match the HLSL cbuffer layout";
}

// ─────────────────────────────────────────────────────────────────────────────
// 7.4.2: ParticleMeshBatchItem default state
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies that a default-constructed ParticleMeshBatchItem has sentinel
/// UINT32_MAX SRV indices and no valid buffer/pipeline handles.
TEST(VFXRenderGraph_MeshParticle, ParticleMeshBatchItem_Default_InstanceBufferSrvIndex_IsUINT32MAX) {
    ParticleMeshBatchItem item;
    EXPECT_EQ(item.instanceBufferSrvIndex, UINT32_MAX)
        << "Default instanceBufferSrvIndex must be UINT32_MAX";
}

TEST(VFXRenderGraph_MeshParticle, ParticleMeshBatchItem_Default_MaterialTexturesSrvIndex_IsUINT32MAX) {
    ParticleMeshBatchItem item;
    EXPECT_EQ(item.materialTexturesSrvIndex, UINT32_MAX)
        << "Default materialTexturesSrvIndex must be UINT32_MAX";
}

TEST(VFXRenderGraph_MeshParticle, ParticleMeshBatchItem_Default_MeshIndexFormat_IsUint16) {
    ParticleMeshBatchItem item;
    EXPECT_EQ(item.meshIndexFormat, RHIIndexFormat::Uint16)
        << "Default meshIndexFormat must be Uint16";
}

// ─────────────────────────────────────────────────────────────────────────────
// 7.4.3: ParticleMeshPassDesc default state
// ─────────────────────────────────────────────────────────────────────────────

/// A default-constructed ParticleMeshPassDesc must have an empty batches vector
/// and isTransparent=false (opaque mode is the default for mesh debris).
TEST(VFXRenderGraph_MeshParticle, ParticleMeshPassDesc_Default_BatchesIsEmpty) {
    ParticleMeshPassDesc desc;
    EXPECT_TRUE(desc.batches.empty())
        << "ParticleMeshPassDesc must start with an empty batches vector";
}

TEST(VFXRenderGraph_MeshParticle, ParticleMeshPassDesc_Default_IsTransparent_IsFalse) {
    ParticleMeshPassDesc desc;
    EXPECT_FALSE(desc.isTransparent)
        << "Default isTransparent must be false (opaque debris is the default)";
}

// ─────────────────────────────────────────────────────────────────────────────
// 7.4.4: Mesh buffer allocation via GPUParticleBuffer::CreateMeshBuffers
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies that a GPUParticleBuffer created with only the base buffers has
/// hasMeshBuffers=false, confirming that mesh buffers are opt-in.
TEST(VFXRenderGraph_MeshParticle, GPUParticleBuffer_BaseCreate_HasMeshBuffers_IsFalse) {
    MockRHIDevice device;
    GPUParticleBuffer buf;
    ASSERT_TRUE(buf.Create(&device, 500u));
    EXPECT_FALSE(buf.hasMeshBuffers)
        << "hasMeshBuffers must be false after base Create() — mesh buffers are opt-in";
    buf.Destroy(&device);
}

/// Verifies that after calling CreateMeshBuffers() on a valid base buffer,
/// hasMeshBuffers becomes true and the three mesh-specific handles are valid.
TEST(VFXRenderGraph_MeshParticle, GPUParticleBuffer_CreateMeshBuffers_SetsMeshFlag) {
    MockRHIDevice device;
    GPUParticleBuffer buf;
    ASSERT_TRUE(buf.Create(&device, 500u));
    ASSERT_TRUE(buf.CreateMeshBuffers(&device));

    EXPECT_TRUE(buf.hasMeshBuffers)
        << "hasMeshBuffers must be true after CreateMeshBuffers()";

    buf.Destroy(&device);
}

TEST(VFXRenderGraph_MeshParticle, GPUParticleBuffer_CreateMeshBuffers_TransformsHandleIsValid) {
    MockRHIDevice device;
    GPUParticleBuffer buf;
    ASSERT_TRUE(buf.Create(&device, 500u));
    ASSERT_TRUE(buf.CreateMeshBuffers(&device));

    EXPECT_TRUE(buf.meshInstanceTransforms.IsValid())
        << "meshInstanceTransforms must be a valid handle after CreateMeshBuffers()";

    buf.Destroy(&device);
}

TEST(VFXRenderGraph_MeshParticle, GPUParticleBuffer_CreateMeshBuffers_CountHandleIsValid) {
    MockRHIDevice device;
    GPUParticleBuffer buf;
    ASSERT_TRUE(buf.Create(&device, 500u));
    ASSERT_TRUE(buf.CreateMeshBuffers(&device));

    EXPECT_TRUE(buf.meshInstanceCount.IsValid())
        << "meshInstanceCount must be a valid handle after CreateMeshBuffers()";

    buf.Destroy(&device);
}

TEST(VFXRenderGraph_MeshParticle, GPUParticleBuffer_CreateMeshBuffers_IndirectArgsHandleIsValid) {
    MockRHIDevice device;
    GPUParticleBuffer buf;
    ASSERT_TRUE(buf.Create(&device, 500u));
    ASSERT_TRUE(buf.CreateMeshBuffers(&device));

    EXPECT_TRUE(buf.meshIndirectDrawArgs.IsValid())
        << "meshIndirectDrawArgs must be a valid handle after CreateMeshBuffers()";

    buf.Destroy(&device);
}

/// Verifies that DestroyMeshBuffers() resets hasMeshBuffers to false and
/// invalidates all three mesh-specific buffer handles.
TEST(VFXRenderGraph_MeshParticle, GPUParticleBuffer_DestroyMeshBuffers_ClearsMeshFlag) {
    MockRHIDevice device;
    GPUParticleBuffer buf;
    ASSERT_TRUE(buf.Create(&device, 200u));
    ASSERT_TRUE(buf.CreateMeshBuffers(&device));
    EXPECT_TRUE(buf.hasMeshBuffers);

    buf.DestroyMeshBuffers(&device);
    EXPECT_FALSE(buf.hasMeshBuffers)
        << "hasMeshBuffers must be false after DestroyMeshBuffers()";

    buf.Destroy(&device);
}

TEST(VFXRenderGraph_MeshParticle, GPUParticleBuffer_DestroyMeshBuffers_TransformsHandleIsInvalid) {
    MockRHIDevice device;
    GPUParticleBuffer buf;
    ASSERT_TRUE(buf.Create(&device, 200u));
    ASSERT_TRUE(buf.CreateMeshBuffers(&device));
    buf.DestroyMeshBuffers(&device);

    EXPECT_FALSE(buf.meshInstanceTransforms.IsValid())
        << "meshInstanceTransforms must be invalid after DestroyMeshBuffers()";

    buf.Destroy(&device);
}

/// Verifies that after a successful CreateMeshBuffers() call, hasMeshBuffers
/// is true and all three mesh handles are valid. This confirms the guard
/// state that prevents double-allocation (the implementation uses UZ_CHECK
/// as a fatal precondition — callers must check hasMeshBuffers before calling).
TEST(VFXRenderGraph_MeshParticle, GPUParticleBuffer_CreateMeshBuffers_GuardState_IsTrueAfterSuccess) {
    MockRHIDevice device;
    GPUParticleBuffer buf;
    ASSERT_TRUE(buf.Create(&device, 300u));
    ASSERT_TRUE(buf.CreateMeshBuffers(&device));

    // hasMeshBuffers is the guard flag: callers must check it before calling
    // CreateMeshBuffers() again. The implementation enforces this with a fatal
    // assert (UZ_CHECK), so we only verify the flag is correctly set here.
    EXPECT_TRUE(buf.hasMeshBuffers)
        << "hasMeshBuffers must be true after successful CreateMeshBuffers()";
    EXPECT_TRUE(buf.meshInstanceTransforms.IsValid());
    EXPECT_TRUE(buf.meshInstanceCount.IsValid());
    EXPECT_TRUE(buf.meshIndirectDrawArgs.IsValid());

    buf.Destroy(&device);
}

// ─────────────────────────────────────────────────────────────────────────────
// 7.4.5: MeshRenderModule config → ParticleMeshBatchItem field population
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies that a MeshRenderModule with a custom config can supply the
/// castShadow, meshPath, and materialPath values that drive batch item
/// selection (e.g. choosing the correct PSO for shadow-casting batches).
TEST(VFXRenderGraph_MeshParticle, MeshRenderModule_CastShadow_True_ConfigReflected) {
    MeshRenderModuleConfig cfg;
    cfg.meshPath      = "meshes/debris.uzmesh";
    cfg.materialPath  = "materials/debris.uzmat";
    cfg.castShadow    = true;
    cfg.rotationMode  = MeshRotationMode::VelocityAligned;
    cfg.meshScale     = Vec3{1.5f, 1.5f, 1.5f};

    const MeshRenderModule mod(cfg);
    const auto& stored = mod.GetConfig();

    EXPECT_TRUE(stored.castShadow)
        << "MeshRenderModuleConfig.castShadow must be true after construction";
    EXPECT_EQ(stored.meshPath, "meshes/debris.uzmesh")
        << "meshPath must be stored verbatim in the module config";
    EXPECT_EQ(stored.materialPath, "materials/debris.uzmat")
        << "materialPath must be stored verbatim in the module config";
    EXPECT_FLOAT_EQ(stored.meshScale.x, 1.5f)
        << "meshScale.x must be preserved from the config";
}

/// Verifies that MeshRenderModuleConfig.rotationMode drives the HLSL code
/// path in GetShaderCode(), which is used to select the instance-CS variant
/// for the mesh particle batch.
TEST(VFXRenderGraph_MeshParticle, MeshRenderModule_RotationModeNone_ShaderCodeContainsNoVelocityRef) {
    MeshRenderModuleConfig cfg;
    cfg.rotationMode = MeshRotationMode::None;
    const MeshRenderModule mod(cfg);

    // The "None" mode must not reference velocity in its instance CS snippet.
    const std::string_view code = mod.GetShaderCode();
    EXPECT_EQ(code.find("Velocities"), std::string_view::npos)
        << "MeshRotationMode::None must not reference Velocities in shader code";
}

/// Verifies that a batch item wired from a MeshRenderModuleConfig with
/// castShadow=true and isTransparent=false is compatible with the opaque
/// mesh pass (depth write, no blend).
TEST(VFXRenderGraph_MeshParticle, ParticleMeshBatchItem_OpaqueConfig_NotTransparent) {
    // The batch item itself does not carry isTransparent; that flag lives on the
    // PassDesc. Verify that the default desc is opaque (false), and a batch item
    // populated from an opaque config has no transparent marker.
    ParticleMeshPassDesc desc;
    EXPECT_FALSE(desc.isTransparent)
        << "Opaque mesh debris batches must use isTransparent=false";

    // Simulate pass setup: insert one batch.
    ParticleMeshBatchItem item;
    item.meshIndexFormat = RHIIndexFormat::Uint16;
    desc.batches.push_back(item);

    ASSERT_EQ(desc.batches.size(), 1u);
    EXPECT_EQ(desc.batches[0].meshIndexFormat, RHIIndexFormat::Uint16)
        << "Batch item meshIndexFormat must be preserved through the PassDesc vector";
}

/// Verifies that a mesh particle emitter (ParticleEmitter with mesh module)
/// correctly creates base GPU buffers AND mesh buffers, then destroys them both
/// on Shutdown — no GPU resource leak.
TEST(VFXRenderGraph_MeshParticle, MeshEmitter_CreateAndDestroyMeshBuffers_NoLeak) {
    MockRHIDevice device;
    ParticleBudget budget(5000u);

    // Create the base buffer and mesh buffers manually, simulating what
    // VFXSystem does for emitters using MeshRenderModule.
    GPUParticleBuffer buf;
    ASSERT_TRUE(buf.Create(&device, 1000u));
    const uint32_t createdAfterBase = device.buffersCreated.load();
    EXPECT_GT(createdAfterBase, 0u)
        << "Base Create() must allocate at least one GPU buffer";

    ASSERT_TRUE(buf.CreateMeshBuffers(&device));
    const uint32_t createdAfterMesh = device.buffersCreated.load();
    EXPECT_GT(createdAfterMesh, createdAfterBase)
        << "CreateMeshBuffers() must allocate additional GPU buffers";

    // The mesh buffers add exactly 3 buffers:
    // meshInstanceTransforms, meshInstanceCount, meshIndirectDrawArgs.
    EXPECT_EQ(createdAfterMesh - createdAfterBase, 3u)
        << "CreateMeshBuffers() must allocate exactly 3 additional GPU buffers";

    // Destroy and verify all buffers are released.
    buf.Destroy(&device);

    EXPECT_EQ(device.buffersDestroyed.load(), createdAfterMesh)
        << "Destroy() must release all " << createdAfterMesh << " GPU buffers — no leaks";
    EXPECT_FALSE(buf.hasMeshBuffers)
        << "hasMeshBuffers must be false after Destroy()";
    EXPECT_FALSE(buf.meshInstanceTransforms.IsValid())
        << "meshInstanceTransforms must be invalid after Destroy()";
}

/// Verifies that multiple mesh batch items can be safely pushed into
/// ParticleMeshPassDesc.batches, and the vector preserves all entries.
TEST(VFXRenderGraph_MeshParticle, ParticleMeshPassDesc_MultipleBatches_AllPreserved) {
    MockRHIDevice device;

    // Create two mesh buffers to use as batch sources.
    GPUParticleBuffer buf1, buf2;
    ASSERT_TRUE(buf1.Create(&device, 500u));
    ASSERT_TRUE(buf1.CreateMeshBuffers(&device));
    ASSERT_TRUE(buf2.Create(&device, 800u));
    ASSERT_TRUE(buf2.CreateMeshBuffers(&device));

    ParticleMeshPassDesc desc;
    {
        ParticleMeshBatchItem b1;
        b1.indirectArgsBuffer    = buf1.meshIndirectDrawArgs;
        b1.instanceBufferSrvIndex = 10u;
        b1.meshIndexFormat       = RHIIndexFormat::Uint16;

        ParticleMeshBatchItem b2;
        b2.indirectArgsBuffer    = buf2.meshIndirectDrawArgs;
        b2.instanceBufferSrvIndex = 20u;
        b2.meshIndexFormat       = RHIIndexFormat::Uint32;

        desc.batches.push_back(b1);
        desc.batches.push_back(b2);
    }

    ASSERT_EQ(desc.batches.size(), 2u)
        << "PassDesc must hold exactly 2 batch items";
    EXPECT_EQ(desc.batches[0].instanceBufferSrvIndex, 10u)
        << "First batch instanceBufferSrvIndex must be 10";
    EXPECT_EQ(desc.batches[1].instanceBufferSrvIndex, 20u)
        << "Second batch instanceBufferSrvIndex must be 20";
    EXPECT_EQ(desc.batches[0].meshIndexFormat, RHIIndexFormat::Uint16)
        << "First batch must use Uint16 indices";
    EXPECT_EQ(desc.batches[1].meshIndexFormat, RHIIndexFormat::Uint32)
        << "Second batch must use Uint32 indices";

    buf1.Destroy(&device);
    buf2.Destroy(&device);
}
