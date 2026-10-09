/// Regression test — `MeshRenderModule` GPU buffer leak at emitter shutdown.
///
/// Until phase0_particle-mesh-bindless-rewrite-and-d3d12ma-leak section B,
/// `ParticleEmitter::Shutdown` destroyed the per-emitter `GPUParticleBuffer`
/// but never touched the render module — `MeshRenderModule` owns its own
/// `instanceTransformBuffer_` + `instanceColorBuffer_` (allocated on demand
/// when the emitter starts using mesh particles) and silently leaked them
/// past shutdown. D3D12MA reports the leak as `m_pMetadata->IsEmpty()`
/// asserting at process exit.
///
/// The fix introduced `IVFXModule::ReleaseGPUResources` (default no-op)
/// and overrode it on `MeshRenderModule` to forward to `DestroyGPUBuffers`.
/// `ParticleEmitter::Shutdown` now walks every module in the stack and
/// calls the virtual.
///
/// This test reproduces the lifecycle on a `MockRHIDevice` (CPU-only, no
/// real DX12 needed) and asserts the destroy-count balances the create-
/// count after `Shutdown`.

#include <gtest/gtest.h>

#include "components/particle_emitter.h"
#include "core/particle_budget.h"
#include "modules/mesh_render_module.h"
#include "mock_rhi_device.h"

using namespace uz;

// =============================================================================
// MeshRenderModule::ReleaseGPUResources — direct unit test
// =============================================================================

TEST(UZ_MeshRenderModule_LeakFix, ReleaseGPUResources_DestroysBuffersAllocatedByCreate) {
    test::MockRHIDevice device;

    MeshRenderModuleConfig cfg;
    cfg.maxInstances = 64;
    MeshRenderModule mod(cfg);

    // Allocate the per-instance transform + color buffers (2 buffers).
    ASSERT_TRUE(mod.CreateGPUBuffers(&device));
    ASSERT_TRUE(mod.HasGPUBuffers());
    EXPECT_EQ(device.buffersCreated.load(), 2u);
    EXPECT_EQ(device.buffersDestroyed.load(), 0u);

    // The fix entry point: ReleaseGPUResources must destroy both buffers
    // and reset the "has buffers" flag so a second call is a no-op.
    mod.ReleaseGPUResources(&device);
    EXPECT_FALSE(mod.HasGPUBuffers());
    EXPECT_EQ(device.buffersDestroyed.load(), 2u);

    // Idempotent — second call must not double-destroy.
    mod.ReleaseGPUResources(&device);
    EXPECT_EQ(device.buffersDestroyed.load(), 2u);
}

TEST(UZ_MeshRenderModule_LeakFix, ReleaseGPUResources_NoBuffers_IsNoOp) {
    test::MockRHIDevice device;
    MeshRenderModule mod;

    EXPECT_FALSE(mod.HasGPUBuffers());

    // No allocate -> Release must be safe.
    mod.ReleaseGPUResources(&device);
    EXPECT_EQ(device.buffersDestroyed.load(), 0u);
    EXPECT_FALSE(mod.HasGPUBuffers());
}

// =============================================================================
// ParticleEmitter::Shutdown — integration test for the leak fix
// =============================================================================
//
// Verifies that after `Shutdown(device, budget)`:
// 1. The render module's GPU buffers are destroyed (the fix).
// 2. The buffer destroy count balances the create count for the module
//    (no orphan allocations remain).
// 3. The emitter is back in the uninitialised state.
// =============================================================================

TEST(UZ_ParticleEmitter_LeakFix,
     Shutdown_CallsReleaseGPUResources_OnMeshRenderModule) {
    test::MockRHIDevice device;
    ParticleBudget budget(10'000);

    ParticleEmitter emitter;
    emitter.maxParticles = 32;

    // Attach a mesh render module + allocate its private buffers.
    MeshRenderModuleConfig cfg;
    cfg.maxInstances = 32;
    auto mod = std::make_unique<MeshRenderModule>(cfg);
    ASSERT_TRUE(mod->CreateGPUBuffers(&device));
    EXPECT_TRUE(mod->HasGPUBuffers());

    // Snapshot how many buffers the mesh module allocated. Subsequent
    // ParticleEmitter::Initialize will allocate its own GPU particle
    // buffers; we only care that `Shutdown` returns the count to (at least)
    // module-create + emitter-create + emitter-destroy + module-destroy.
    const uint32_t buffersAfterModule = device.buffersCreated.load();

    emitter.moduleStack.SetRenderModule(std::move(mod));
    ASSERT_TRUE(emitter.Initialize(&device, budget));

    const uint32_t buffersAfterInit = device.buffersCreated.load();
    EXPECT_GT(buffersAfterInit, buffersAfterModule)
        << "ParticleEmitter::Initialize must allocate its own GPU buffers";

    // Shutdown must destroy *every* allocation that the emitter + its
    // modules pulled from the device. Before the fix, the 2 mesh module
    // buffers stayed alive past Shutdown and tripped the D3D12MA leak
    // assert at process exit.
    emitter.Shutdown(&device, budget);

    EXPECT_EQ(device.buffersDestroyed.load(), device.buffersCreated.load())
        << "After Shutdown the destroy count MUST equal the create count — "
           "any imbalance is a GPU buffer leak that D3D12MA will surface "
           "as `m_pMetadata->IsEmpty()` at process exit";

    // Verify the mesh module specifically dropped its handles. Cast back
    // through the moduleStack — the renderModule pointer is still alive
    // because std::unique_ptr destroys when the emitter itself is destroyed.
    const auto* meshAfter = dynamic_cast<const MeshRenderModule*>(
        emitter.moduleStack.renderModule.get());
    ASSERT_NE(meshAfter, nullptr);
    EXPECT_FALSE(meshAfter->HasGPUBuffers());
}

TEST(UZ_ParticleEmitter_LeakFix, Shutdown_NoMeshModule_BalancesNormally) {
    // Sprite/no-render emitters must continue to balance — the new virtual
    // call must not change behaviour for modules that don't override it.
    test::MockRHIDevice device;
    ParticleBudget budget(10'000);

    ParticleEmitter emitter;
    emitter.maxParticles = 16;
    ASSERT_TRUE(emitter.Initialize(&device, budget));

    emitter.Shutdown(&device, budget);

    EXPECT_EQ(device.buffersDestroyed.load(), device.buffersCreated.load())
        << "Sprite emitters must balance the same way mesh emitters now do";
}

TEST(UZ_ParticleEmitter_LeakFix, Shutdown_CalledTwice_DoesNotDoubleDestroy) {
    // ReleaseGPUResources must be idempotent end-to-end (the second
    // Shutdown is a no-op because `initialized` flips to false on the
    // first call).
    test::MockRHIDevice device;
    ParticleBudget budget(10'000);

    ParticleEmitter emitter;
    emitter.maxParticles = 16;

    MeshRenderModuleConfig cfg;
    cfg.maxInstances = 16;
    auto mod = std::make_unique<MeshRenderModule>(cfg);
    ASSERT_TRUE(mod->CreateGPUBuffers(&device));
    emitter.moduleStack.SetRenderModule(std::move(mod));
    ASSERT_TRUE(emitter.Initialize(&device, budget));

    emitter.Shutdown(&device, budget);
    const uint32_t destroysAfterFirst = device.buffersDestroyed.load();

    // Second Shutdown — must be a guarded no-op.
    emitter.Shutdown(&device, budget);
    EXPECT_EQ(device.buffersDestroyed.load(), destroysAfterFirst);
}
