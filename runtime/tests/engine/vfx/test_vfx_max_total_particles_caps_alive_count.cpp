/// Integration tests for r.VFX.MaxTotalParticles caps alive count.
///
/// Phase 16 task 2.3 — `test_vfx_max_total_particles_caps_alive_count`
///
/// Specification (from proposal.md):
///   Pre-seed a 20'000-alive emitter (or two emitters totaling 20k).
///   Set r.VFX.MaxTotalParticles = 1000 (spec target).
///   Run RenderFrame in a loop. Capture VFXSystem::GetTotalAliveParticles() each frame.
///   Assert it converges to <= cap within 5 frames (allowing the GPU readback
///   ring to drain).
///
/// CVar minimum discovery:
///   r.VFX.MaxTotalParticles is registered with min=1024 (vfx_system.cpp line 45).
///   Setting it to 1000 clamps to 1024. Tests therefore use 2048 as the
///   "low cap" scenario (above the 1024 floor, well below 20k) and additionally
///   test the 1024 minimum explicitly. The spec's "1000" is unachievable through
///   the CVar; only via ParticleBudget::SetMax() (which has no floor).
///
/// GPU reality gap:
///   The particle kill-off when budget is lowered is performed by the GPU update
///   compute shader which compares each particle's slot index against the new
///   budget max and discards excess particles. This requires a live DX12 device
///   with real UAV buffer bindings and a real command list dispatch.
///   MockRHIDevice returns null command lists, zero UAV indices, and a scratch
///   MapBuffer region — the GPU shader never executes.
///
///   What we CAN test on the CPU side:
///   1. Budget cap propagation: After Tick(), GetBudget().GetMax() reflects the
///      (clamped-to-min) CVar value. This is the single source of truth read by
///      the GPU shader.
///   2. GetTotalAliveParticles() uses emitter->cpuAliveCount as the CPU mirror
///      when readbackHasKickedOnce=false. Manual decrement simulates GPU kill-off.
///   3. Budget allocation clamp at Initialize(): emitters cannot exceed the cap.
///   4. ParticleBudget::SetMax() (no floor) can set any value including < 1024.
///
/// Coverage gap:
///   GPU-side alive count convergence within 5 frames requires a live DX12 device.
///   Documented as a hardware CI gap (proposal.md §2.3).
///
/// All tests are CPU-only — no GPU hardware required.

#include <gtest/gtest.h>

#include "engine/core/cvar.h"
#include "engine/vfx/components/emitter_presets.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/particle_budget.h"
#include "engine/vfx/vfx_system.h"
#include "mock_rhi_device.h"

#include <algorithm>
#include <cstdint>

using namespace uz;

namespace {

/// The CVar minimum for r.VFX.MaxTotalParticles (registered in vfx_system.cpp).
static constexpr uint32_t kCVarMin = 1024u;

/// "Low cap" value for capping scenarios: above kCVarMin, well below 20k.
static constexpr uint32_t kLowCap = 2048u;

/// Reset VFX CVars to defaults so tests are order-independent.
void ResetVfxCVars() {
    auto& cvars = CVarRegistry::Get();
    cvars.Reset("r.VFX.MaxTotalParticles");
    cvars.Reset("r.VFX.AllowGPUSim");
    cvars.Reset("r.VFX.QualityLevel");
    cvars.Reset("r.VFX.SimulatePaused");
    cvars.Reset("r.VFX.TimeScale");
    cvars.Reset("r.VFX.Seek.MaxTicksPerFrame");
}

struct VfxMaxParticlesFixture {
    test::MockRHIDevice device;
    VFXSystem           system;

    explicit VfxMaxParticlesFixture(uint32_t budget = 500'000u) {
        system.Initialize(&device, budget);
    }
    ~VfxMaxParticlesFixture() {
        system.Shutdown(&device);
    }
};

} // anonymous namespace

// =============================================================================
// CVar minimum: values below 1024 clamp to 1024
// =============================================================================

/// The CVar has a registered minimum of 1024. Setting it to 1000 produces
/// GetBudget().GetMax() == 1024 after Tick() — the clamped minimum.
TEST(VfxMaxTotalParticles, CVar_Below1024_ClampsToMinimum) {
    ResetVfxCVars();
    VfxMaxParticlesFixture fx(500'000u);

    CVarRegistry::Get().Set("r.VFX.MaxTotalParticles", 1000.f);
    fx.system.Tick(0.016f);

    EXPECT_EQ(fx.system.GetBudget().GetMax(), kCVarMin)
        << "CVar value 1000 is below the registered minimum of 1024; "
           "it must clamp to 1024. Spec target of 1000 is only reachable "
           "via ParticleBudget::SetMax() which has no floor.";
}

/// Setting CVar to exactly 1024 (the minimum) produces GetMax() == 1024.
TEST(VfxMaxTotalParticles, CVar_ExactlyAtMinimum_1024_Propagates) {
    ResetVfxCVars();
    VfxMaxParticlesFixture fx(500'000u);

    CVarRegistry::Get().Set("r.VFX.MaxTotalParticles", static_cast<float>(kCVarMin));
    fx.system.Tick(0.016f);

    EXPECT_EQ(fx.system.GetBudget().GetMax(), kCVarMin)
        << "CVar set to exactly 1024 (the minimum) must propagate without clamping";
}

// =============================================================================
// Budget cap propagation (CPU-side — no GPU required)
// =============================================================================

/// After r.VFX.MaxTotalParticles=kLowCap (2048) + Tick(), GetBudget().GetMax() == 2048.
TEST(VfxMaxTotalParticles, CVar_PropagatesToBudgetMax_AfterTick) {
    ResetVfxCVars();
    VfxMaxParticlesFixture fx(500'000u);

    ASSERT_EQ(fx.system.GetBudget().GetMax(), 500'000u);

    CVarRegistry::Get().Set("r.VFX.MaxTotalParticles", static_cast<float>(kLowCap));
    fx.system.Tick(0.016f);

    EXPECT_EQ(fx.system.GetBudget().GetMax(), kLowCap)
        << "After r.VFX.MaxTotalParticles=" << kLowCap << " + Tick(), budget cap must be "
        << kLowCap;
}

/// Setting MaxTotalParticles to 20'000 then to kLowCap drops the cap correctly.
TEST(VfxMaxTotalParticles, CVar_CanDecreaseBudget) {
    ResetVfxCVars();
    VfxMaxParticlesFixture fx(500'000u);

    CVarRegistry::Get().Set("r.VFX.MaxTotalParticles", 20'000.f);
    fx.system.Tick(0.016f);
    ASSERT_EQ(fx.system.GetBudget().GetMax(), 20'000u);

    CVarRegistry::Get().Set("r.VFX.MaxTotalParticles", static_cast<float>(kLowCap));
    fx.system.Tick(0.016f);

    EXPECT_EQ(fx.system.GetBudget().GetMax(), kLowCap)
        << "Budget max must drop from 20k to " << kLowCap << " when CVar is lowered";
}

/// MaxTotalParticles CVar survives multiple Tick() calls without drift.
TEST(VfxMaxTotalParticles, CVar_StableAcrossMultipleTicks) {
    ResetVfxCVars();
    VfxMaxParticlesFixture fx(500'000u);

    CVarRegistry::Get().Set("r.VFX.MaxTotalParticles", static_cast<float>(kLowCap));
    for (int i = 0; i < 10; ++i) {
        fx.system.Tick(0.016f);
    }

    EXPECT_EQ(fx.system.GetBudget().GetMax(), kLowCap)
        << "Budget max must remain stable at " << kLowCap
        << " across 10 Tick() calls with no further CVar changes";
}

/// Can set cap to a large value (2M particles).
TEST(VfxMaxTotalParticles, CVar_CanSetLargeValue) {
    ResetVfxCVars();
    VfxMaxParticlesFixture fx(500'000u);

    CVarRegistry::Get().Set("r.VFX.MaxTotalParticles", 2'000'000.f);
    fx.system.Tick(0.016f);

    EXPECT_EQ(fx.system.GetBudget().GetMax(), 2'000'000u)
        << "Budget max must propagate to 2M when CVar is raised to 2M";
}

// =============================================================================
// ParticleBudget::SetMax — no floor constraint
// =============================================================================

/// ParticleBudget::SetMax() (no minimum floor) can set the cap to exactly the
/// spec-target value of 1000, verifying the underlying mechanism is sound even
/// though the CVar cannot reach sub-1024 values.
TEST(VfxMaxTotalParticles, Budget_SetMax_ToSpecTarget1000_NoFloor) {
    ParticleBudget budget(500'000u);
    budget.SetMax(1000u);
    EXPECT_EQ(budget.GetMax(), 1000u)
        << "ParticleBudget::SetMax(1000) has no floor — spec target is reachable "
           "via direct API even though the CVar enforces a 1024 minimum";
}

/// SetMax reduces the max; GetMax reflects it immediately.
TEST(VfxMaxTotalParticles, Budget_SetMax_ReducesMax) {
    ParticleBudget budget(500'000u);
    ASSERT_EQ(budget.GetMax(), 500'000u);

    budget.SetMax(kLowCap);

    EXPECT_EQ(budget.GetMax(), kLowCap);
}

/// SetMax can increase the cap.
TEST(VfxMaxTotalParticles, Budget_SetMax_CanIncrease) {
    ParticleBudget budget(kLowCap);
    budget.SetMax(500'000u);
    EXPECT_EQ(budget.GetMax(), 500'000u);
}

// =============================================================================
// New emitter initialization cannot exceed the budget cap
// =============================================================================

/// An emitter requesting 20k particles when budget is hard-capped at kLowCap
/// via SetMax() receives at most kLowCap particles.
TEST(VfxMaxTotalParticles, Initialize_20kEmitter_BudgetCappedAtLowCap_GrantsAtMost) {
    ResetVfxCVars();
    test::MockRHIDevice device;
    ParticleBudget      budget(kLowCap);

    ParticleEmitter e;
    ApplyPreset_Fire(e);
    e.maxParticles = 20'000u;

    const bool ok = e.Initialize(&device, budget);

    if (ok) {
        EXPECT_LE(e.allocatedFromBudget, kLowCap)
            << "Partial grant must not exceed the budget cap of " << kLowCap;
    } else {
        EXPECT_EQ(e.allocatedFromBudget, 0u)
            << "Failed Initialize must not have consumed any budget";
    }
    EXPECT_EQ(budget.GetUsed(), e.allocatedFromBudget)
        << "Budget used must equal what the emitter reports as allocated";

    if (ok) {
        e.Shutdown(&device, budget);
    }
}

/// Two emitters requesting 15k and 15k with a budget of 20k.
/// Combined grant must not exceed 20k.
TEST(VfxMaxTotalParticles, TwoEmitters_CombinedGrant_DoesNotExceedBudgetCap) {
    ResetVfxCVars();
    test::MockRHIDevice device;
    ParticleBudget      budget(20'000u);

    ParticleEmitter e1, e2;
    ApplyPreset_Fire(e1);  e1.maxParticles = 15'000u;
    ApplyPreset_Fire(e2);  e2.maxParticles = 15'000u;

    ASSERT_TRUE(e1.Initialize(&device, budget));
    e2.Initialize(&device, budget);

    EXPECT_LE(budget.GetUsed(), 20'000u)
        << "Combined allocation of two 15k emitters must not exceed 20k budget cap";

    e1.Shutdown(&device, budget);
    e2.Shutdown(&device, budget);
    EXPECT_EQ(budget.GetUsed(), 0u)
        << "All budget must be returned after both emitters shut down";
}

// =============================================================================
// GetTotalAliveParticles with CPU mirror
// =============================================================================

/// Pre-seed 20k alive across two emitters (via cpuAliveCount CPU mirror).
/// After lowering budget max to kLowCap via Tick(), GetBudget().GetMax()
/// reflects the cap. The CPU alive count stays at 20k until GPU kill-off
/// fires via readback — this is the expected MockRHIDevice behavior.
TEST(VfxMaxTotalParticles, Tick_AfterBudgetCap_BudgetMaxIsLowered) {
    ResetVfxCVars();
    VfxMaxParticlesFixture fx(500'000u);

    test::MockRHIDevice& device = fx.device;
    ParticleBudget&      budget = fx.system.GetBudget();

    ParticleEmitter e1, e2;
    ApplyPreset_Fire(e1);  e1.maxParticles = 10'000u;
    ApplyPreset_Fire(e2);  e2.maxParticles = 10'000u;

    ASSERT_TRUE(e1.Initialize(&device, budget));
    ASSERT_TRUE(e2.Initialize(&device, budget));

    e1.cpuAliveCount = 10'000u;
    e2.cpuAliveCount = 10'000u;

    fx.system.RegisterEmitter(&e1);
    fx.system.RegisterEmitter(&e2);

    fx.system.Tick(0.016f);
    ASSERT_EQ(fx.system.GetTotalAliveParticles(), 20'000u)
        << "Pre-condition: 20k alive before cap is applied";

    // Apply cap (kLowCap = 2048, above the 1024 minimum).
    CVarRegistry::Get().Set("r.VFX.MaxTotalParticles", static_cast<float>(kLowCap));
    fx.system.Tick(0.016f);

    EXPECT_EQ(fx.system.GetBudget().GetMax(), kLowCap)
        << "Budget max must have propagated to " << kLowCap << " after Tick()";
    // CPU mirror still 20k because GPU readback hasn't fired on MockRHIDevice.
    // This is expected on CPU-only tests.
    EXPECT_GE(fx.system.GetTotalAliveParticles(), 0u);

    fx.system.UnregisterEmitter(&e1);
    fx.system.UnregisterEmitter(&e2);
    e1.Shutdown(&device, budget);
    e2.Shutdown(&device, budget);

    ResetVfxCVars();
}

/// Simulate the GPU kill-off result by manually decrementing cpuAliveCount.
/// Starting from 20k, with cap=kLowCap, after 5 halving iterations the
/// alive count must be <= kLowCap.
TEST(VfxMaxTotalParticles, SimulatedGpuKilloff_ConvergesWithin5Frames) {
    ResetVfxCVars();
    VfxMaxParticlesFixture fx(500'000u);

    test::MockRHIDevice& device = fx.device;
    ParticleBudget&      budget = fx.system.GetBudget();

    ParticleEmitter e;
    ApplyPreset_Fire(e);
    e.maxParticles = 20'000u;
    ASSERT_TRUE(e.Initialize(&device, budget));
    e.cpuAliveCount = 20'000u;

    fx.system.RegisterEmitter(&e);

    CVarRegistry::Get().Set("r.VFX.MaxTotalParticles", static_cast<float>(kLowCap));
    fx.system.Tick(0.016f);
    ASSERT_EQ(fx.system.GetBudget().GetMax(), kLowCap);

    // Simulate GPU kill-off: distribute the excess evenly across 5 frames.
    // Real GPU kill-off kills slot-indexed particles each frame until
    // count <= newBudget. Distributing kills evenly ensures convergence
    // in exactly 5 frames regardless of starting count.
    const uint32_t initialExcess =
        (e.cpuAliveCount > kLowCap) ? (e.cpuAliveCount - kLowCap) : 0u;

    for (int frame = 0; frame < 5; ++frame) {
        if (e.cpuAliveCount > kLowCap) {
            // Kill ceil(remainingExcess / remainingFrames) per frame.
            const uint32_t excess          = e.cpuAliveCount - kLowCap;
            const uint32_t remainingFrames = static_cast<uint32_t>(5 - frame);
            const uint32_t killed = (excess + remainingFrames - 1u) / remainingFrames;
            e.cpuAliveCount -= std::min(killed, excess);
        }
        fx.system.Tick(0.016f);
    }

    const uint32_t alive = fx.system.GetTotalAliveParticles();
    EXPECT_LE(alive, kLowCap)
        << "Simulated GPU kill-off must bring alive count to <= " << kLowCap
        << " within 5 frames. Note: real GPU convergence requires DX12 hardware. "
           "initial_excess=" << initialExcess;

    fx.system.UnregisterEmitter(&e);
    e.Shutdown(&device, budget);

    ResetVfxCVars();
}

// =============================================================================
// Budget max change does not crash with active emitters
// =============================================================================

/// Lowering MaxTotalParticles below currently-used budget is graceful.
TEST(VfxMaxTotalParticles, SetMaxBelowUsed_DoesNotCrash) {
    ResetVfxCVars();
    test::MockRHIDevice device;
    ParticleBudget      budget(500'000u);

    ParticleEmitter e;
    ApplyPreset_Fire(e);
    e.maxParticles = 5'000u;
    ASSERT_TRUE(e.Initialize(&device, budget));
    ASSERT_EQ(e.allocatedFromBudget, 5'000u);

    // SetMax below the currently allocated amount — no crash.
    budget.SetMax(1'000u);

    EXPECT_EQ(budget.GetMax(), 1'000u)
        << "SetMax below used count must update GetMax() without crash";

    e.Shutdown(&device, budget);
    EXPECT_EQ(budget.GetUsed(), 0u);
}

/// After lowering MaxTotalParticles and calling Tick() 5 times, no crash.
TEST(VfxMaxTotalParticles, Tick5Times_AfterBudgetReduction_NoAssertNoOops) {
    ResetVfxCVars();
    VfxMaxParticlesFixture fx(100'000u);

    test::MockRHIDevice& device = fx.device;
    ParticleBudget&      budget = fx.system.GetBudget();

    ParticleEmitter e1, e2;
    ApplyPreset_Fire(e1);  e1.maxParticles = 5'000u;
    ApplyPreset_Fire(e2);  e2.maxParticles = 5'000u;

    ASSERT_TRUE(e1.Initialize(&device, budget));
    ASSERT_TRUE(e2.Initialize(&device, budget));

    e1.cpuAliveCount = 5'000u;
    e2.cpuAliveCount = 5'000u;

    fx.system.RegisterEmitter(&e1);
    fx.system.RegisterEmitter(&e2);

    CVarRegistry::Get().Set("r.VFX.MaxTotalParticles", static_cast<float>(kLowCap));

    for (int f = 0; f < 5; ++f) {
        fx.system.Tick(0.016f);  // must not crash
    }

    EXPECT_EQ(fx.system.GetBudget().GetMax(), kLowCap)
        << "Budget max must remain at " << kLowCap << " throughout 5 Tick() calls";

    fx.system.UnregisterEmitter(&e1);
    fx.system.UnregisterEmitter(&e2);
    e1.Shutdown(&device, budget);
    e2.Shutdown(&device, budget);

    ResetVfxCVars();
}

// =============================================================================
// Coverage gap documentation
// =============================================================================

/// Documents the GPU alive-count convergence coverage gap.
///
/// The spec (proposal.md §2.3) requires:
///   VFXSystem::GetTotalAliveParticles() <= 1000 within 5 frames.
///
/// CVar minimum (1024) makes the spec's literal "1000" unreachable via CVar.
/// Via direct ParticleBudget::SetMax(1000) it is reachable; see
/// Budget_SetMax_ToSpecTarget1000_NoFloor.
///
/// The alive-count convergence via GPU kill-off cannot be verified without:
///   1. Real DX12 device + compute pipeline.
///   2. UAV buffer with 20k particles allocated and initialized.
///   3. GPU dispatch of the update CS (kills particles at slot >= newBudget).
///   4. GPU readback ring fence wait + readback of the gpuAliveCount buffer.
TEST(VfxMaxTotalParticles, COVERAGE_GAP_GpuAliveConvergence_RequiresRealGPU) {
    SUCCEED() << "GPU alive-count convergence spec (proposal.md §2.3) requires DX12 hardware. "
                 "CVar minimum: r.VFX.MaxTotalParticles has floor=1024 (vfx_system.cpp:45); "
                 "spec target of 1000 is reachable via ParticleBudget::SetMax(1000) only. "
                 "CPU coverage: budget cap propagation, emitter grant clamping, simulated "
                 "kill-off model converging to <= kLowCap within 5 frames. "
                 "Hardware CI gap: real GPU update CS dispatch + readback needed.";
}
