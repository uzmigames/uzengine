/// Sub-Emitter Demo — Headless firework cascade (no window, no GPU).
///
/// Loads three emitter configurations from .uzfx assets (rocket, burst, sparkle),
/// then validates the SubEmitterModule event propagation system by simulating
/// a 3-stage firework cascade through manual readback injection.

#include "mock_device.h"
#include "engine/vfx/vfx_system.h"
#include "engine/vfx/core/particle_budget.h"
#include "engine/vfx/events/vfx_event.h"
#include "engine/vfx/modules/sub_emitter_module.h"
#include "engine/asset/formats/uzfx_loader.h"

#include <cmath>
#include <cstdint>
#include "engine_loop.h"
#include "log.h"

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "file_system.h"

;
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Number of simulated dead rocket particles in the first cascade step.
static constexpr uint32_t kDeadRocketCount = 2u;

/// Number of simulated dead burst particles in the second cascade step.
static constexpr uint32_t kDeadBurstCount  = 2u;

/// Velocity scale applied to dead rockets before handing off to burst.
/// Rocket module uses inheritVelocity=0.0, so burst velocity should be zero.
static constexpr float kRocketInheritVelocity = 0.0f;

/// Velocity scale applied to dead burst particles before handing off to sparkle.
/// Burst module uses inheritVelocity=0.5, so sparkle velocity is half of burst velocity.
static constexpr float kBurstInheritVelocity  = 0.5f;

static constexpr uint32_t kBudgetMax = 100000u;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Returns true when two floats are equal within an absolute epsilon.
[[nodiscard]] bool FloatEq(float a, float b, float eps = 1e-5f) noexcept {
    const float diff = a - b;
    return (diff >= -eps) && (diff <= eps);
}

/// Prints a section header to stdout.
void PrintSection(const char* title) {
    std::printf("\n─────────────────────────────────────────────────\n");
    std::printf("  %s\n", title);
    std::printf("─────────────────────────────────────────────────\n");
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Wraps the sub_emitter_demo demo as an EngineLoop subsystem.
class SubEmitterDemoSubsystem final : public uz::ISubsystem {
public:
    [[nodiscard]] const char* GetName() const override { return "SubEmitterDemo"; }
    [[nodiscard]] uz::InitPhase GetInitPhase() const override { return uz::InitPhase::Init; }
    [[nodiscard]] uz::TickGroup GetTickGroup() const override { return uz::TickGroup::None; }

    [[nodiscard]] bool Init() override {
std::printf("==========================================================\n");
    std::printf("       UzEngine -- Sub-Emitter Firework Demo\n");
    std::printf("  Headless cascade: Rocket -> Burst -> Sparkle\n");
    std::printf("==========================================================\n");

    // ── Load .uzfx assets ───────────────────────────────────────────────
    const char* kAssetPaths[] = {
        "assets/firework_rocket.uzfx",
        "assets/firework_burst.uzfx",
        "assets/firework_sparkle.uzfx",
    };

    std::vector<uz::UzfxDesc> descs;
    for (const char* path : kAssetPaths) {
        auto json = uz::FileReadAllText(path);
        if (json.empty()) {
            std::fprintf(stderr, "[Error] Cannot read %s\n", path);
            return false;
        }
        auto desc = uz::UzfxLoader::Load(json);
        if (desc.name.empty()) {
            std::fprintf(stderr, "[Error] Failed to parse %s\n", path);
            return false;
        }
        std::printf("[Asset] Loaded '%s' (version %u) from %s\n",
                    desc.name.c_str(), desc.version, path);
        descs.push_back(std::move(desc));
    }

    // ── Minimal VFX system initialization ─────────────────────────────
    uz::DemoMockDevice device;

    uz::VFXSystem vfxSystem;
    vfxSystem.Initialize(&device, kBudgetMax);

    std::printf("\n[Init] VFXSystem initialized -- budget: %u particles\n", kBudgetMax);

    // ── Create event queues for the two cascade links ──────────────────
    uz::VFXEventQueue rocketToBurstQueue;
    uz::VFXEventQueue burstToSparkleQueue;

    std::printf("[Init] Created 2 VFXEventQueues (rocket->burst, burst->sparkle)\n");

    // ── Create SubEmitterModules ───────────────────────────────────────
    // Stage 1 -- Rocket module: fires when a rocket particle dies.
    auto rocketModule = std::make_unique<uz::SubEmitterModule>();
    rocketModule->childGraphId    = 1;
    rocketModule->trigger         = uz::SubEmitterTrigger::OnDeath;
    rocketModule->inheritVelocity = kRocketInheritVelocity;
    rocketModule->inheritColor    = true;
    rocketModule->SetChildEventQueue(&rocketToBurstQueue);

    // Stage 2 -- Burst module: fires when a burst particle dies.
    auto burstModule = std::make_unique<uz::SubEmitterModule>();
    burstModule->childGraphId    = 2;
    burstModule->trigger         = uz::SubEmitterTrigger::OnDeath;
    burstModule->inheritVelocity = kBurstInheritVelocity;
    burstModule->inheritColor    = true;
    burstModule->SetChildEventQueue(&burstToSparkleQueue);

    std::printf("[Init] Created 3 firework stages:\n");
    std::printf("  Rocket  (%s) inheritVelocity=%.1f inheritColor=%s -> rocketToBurstQueue\n",
                rocketModule->GetName(),
                static_cast<double>(rocketModule->inheritVelocity),
                rocketModule->inheritColor ? "true" : "false");
    std::printf("  Burst   (%s) inheritVelocity=%.1f inheritColor=%s -> burstToSparkleQueue\n",
                burstModule->GetName(),
                static_cast<double>(burstModule->inheritVelocity),
                burstModule->inheritColor ? "true" : "false");
    std::printf("  Sparkle (leaf -- no sub-emitter)\n");

    // ── Verify parameter exposure ──────────────────────────────────────
    PrintSection("Parameter Check");

    const auto rocketParams = rocketModule->GetParameters();
    std::printf("  RocketModule has %zu parameters:", rocketParams.size());
    for (const auto& p : rocketParams) {
        std::printf(" %s", p.name);
    }
    std::printf("\n");

    const auto burstParams = burstModule->GetParameters();
    std::printf("  BurstModule  has %zu parameters:", burstParams.size());
    for (const auto& p : burstParams) {
        std::printf(" %s", p.name);
    }
    std::printf("\n");

    // ── Stage 1 simulation: 2 rocket particles die ────────────────────
    PrintSection("Stage 1 -- Rocket OnDeath -> Burst Queue");

    uz::SubEmitterReadback rocketReadback;
    {
        uz::EventPayload p0;
        p0.position = {   0.0f, 0.0f, 1000.0f };
        p0.velocity = { 100.0f, 0.0f,  200.0f };
        p0.color    = {   1.0f, 0.4f,    0.0f, 1.0f };
        p0.age      = 2.5f;
        rocketReadback.flaggedParticles.push_back(p0);

        uz::EventPayload p1;
        p1.position = { 500.0f, 0.0f, 1200.0f };
        p1.velocity = { -80.0f, 0.0f,  150.0f };
        p1.color    = {   1.0f, 0.6f,    0.0f, 1.0f };
        p1.age      = 3.1f;
        rocketReadback.flaggedParticles.push_back(p1);
    }

    std::printf("[Stage1] Feeding %zu dead rocket particle(s) to RocketModule::Execute()\n",
                rocketReadback.flaggedParticles.size());

    {
        uz::ExecuteContext ctx;
        ctx.numParticles  = static_cast<uint32_t>(rocketReadback.flaggedParticles.size());
        ctx.dt            = 1.0f / 60.0f;
        ctx.frameNumber   = 1u;
        ctx.parameterData = &rocketReadback;

        rocketModule->Execute(ctx);
    }

    std::printf("[Stage1] rocketToBurstQueue.HasPending() = %s\n",
                rocketToBurstQueue.HasPending() ? "true" : "false");
    std::printf("[Stage1] rocketToBurstQueue.GetPendingCount() = %zu\n",
                rocketToBurstQueue.GetPendingCount());

    auto burstEvents = rocketToBurstQueue.Drain();

    std::printf("[Stage1] Drained %zu event(s) from rocketToBurstQueue:\n",
                burstEvents.size());
    for (const auto& ev : burstEvents) {
        std::printf("  event name='%s' emissionCount=%u payloads=%zu\n",
                    ev.name.c_str(),
                    ev.emissionCount,
                    ev.payloads.size());
        for (size_t i = 0; i < ev.payloads.size(); ++i) {
            const auto& p = ev.payloads[i];
            std::printf("  [%zu] pos=(%.1f,%.1f,%.1f) vel=(%.4f,%.4f,%.4f) "
                        "color=(%.2f,%.2f,%.2f,%.2f) age=%.3f\n",
                        i,
                        static_cast<double>(p.position.x),
                        static_cast<double>(p.position.y),
                        static_cast<double>(p.position.z),
                        static_cast<double>(p.velocity.x),
                        static_cast<double>(p.velocity.y),
                        static_cast<double>(p.velocity.z),
                        static_cast<double>(p.color.x),
                        static_cast<double>(p.color.y),
                        static_cast<double>(p.color.z),
                        static_cast<double>(p.color.w),
                        static_cast<double>(p.age));
        }
    }

    // ── Stage 2 simulation: burst particles die ────────────────────────
    PrintSection("Stage 2 -- Burst OnDeath -> Sparkle Queue");

    uz::SubEmitterReadback burstReadback;
    {
        uz::EventPayload p0;
        p0.position = {   0.0f, 0.0f, 1000.0f };
        p0.velocity = {  50.0f, 30.0f,  80.0f };
        p0.color    = {   1.0f, 0.4f,   0.0f, 1.0f };
        p0.age      = 0.8f;
        burstReadback.flaggedParticles.push_back(p0);

        uz::EventPayload p1;
        p1.position = { 500.0f, 0.0f, 1200.0f };
        p1.velocity = { -40.0f, 60.0f,  60.0f };
        p1.color    = {   1.0f, 0.6f,   0.0f, 1.0f };
        p1.age      = 0.9f;
        burstReadback.flaggedParticles.push_back(p1);
    }

    std::printf("[Stage2] Feeding %zu dead burst particle(s) to BurstModule::Execute()\n",
                burstReadback.flaggedParticles.size());

    {
        uz::ExecuteContext ctx;
        ctx.numParticles  = static_cast<uint32_t>(burstReadback.flaggedParticles.size());
        ctx.dt            = 1.0f / 60.0f;
        ctx.frameNumber   = 2u;
        ctx.parameterData = &burstReadback;

        burstModule->Execute(ctx);
    }

    std::printf("[Stage2] burstToSparkleQueue.HasPending() = %s\n",
                burstToSparkleQueue.HasPending() ? "true" : "false");
    std::printf("[Stage2] burstToSparkleQueue.GetPendingCount() = %zu\n",
                burstToSparkleQueue.GetPendingCount());

    auto sparkleEvents = burstToSparkleQueue.Drain();

    std::printf("[Stage2] Drained %zu event(s) from burstToSparkleQueue:\n",
                sparkleEvents.size());
    for (const auto& ev : sparkleEvents) {
        std::printf("  event name='%s' emissionCount=%u payloads=%zu\n",
                    ev.name.c_str(),
                    ev.emissionCount,
                    ev.payloads.size());
        for (size_t i = 0; i < ev.payloads.size(); ++i) {
            const auto& p = ev.payloads[i];
            std::printf("  [%zu] pos=(%.1f,%.1f,%.1f) vel=(%.4f,%.4f,%.4f) "
                        "color=(%.2f,%.2f,%.2f,%.2f) age=%.3f\n",
                        i,
                        static_cast<double>(p.position.x),
                        static_cast<double>(p.position.y),
                        static_cast<double>(p.position.z),
                        static_cast<double>(p.velocity.x),
                        static_cast<double>(p.velocity.y),
                        static_cast<double>(p.velocity.z),
                        static_cast<double>(p.color.x),
                        static_cast<double>(p.color.y),
                        static_cast<double>(p.color.z),
                        static_cast<double>(p.color.w),
                        static_cast<double>(p.age));
        }
    }

    // ── Cascade summary ────────────────────────────────────────────────
    PrintSection("Cascade Summary");

    const size_t burstPayloadCount   = burstEvents.empty()   ? 0 : burstEvents[0].payloads.size();
    const size_t sparklePayloadCount = sparkleEvents.empty() ? 0 : sparkleEvents[0].payloads.size();

    std::printf("  Rocket  dead: %u  -> Burst queue payloads:   %zu\n",
                kDeadRocketCount, burstPayloadCount);
    std::printf("  Burst   dead: %u  -> Sparkle queue payloads: %zu\n",
                kDeadBurstCount, sparklePayloadCount);
    std::printf("\n  Rocket -> Burst -> Sparkle\n");

    // ── Validation ─────────────────────────────────────────────────────
    PrintSection("Validation");

    bool pass = true;

    // 1. Module names must be stable identifiers.
    if (std::string_view{rocketModule->GetName()} != "sub_emitter") {
        std::fprintf(stderr, "[FAIL] RocketModule name: expected 'sub_emitter', got '%s'\n",
                     rocketModule->GetName());
        pass = false;
    } else {
        std::printf("  [OK] RocketModule::GetName() == 'sub_emitter'\n");
    }

    if (std::string_view{burstModule->GetName()} != "sub_emitter") {
        std::fprintf(stderr, "[FAIL] BurstModule name: expected 'sub_emitter', got '%s'\n",
                     burstModule->GetName());
        pass = false;
    } else {
        std::printf("  [OK] BurstModule::GetName() == 'sub_emitter'\n");
    }

    // 2. Parameter count must be 4.
    if (rocketParams.size() != 4) {
        std::fprintf(stderr, "[FAIL] RocketModule param count: expected 4, got %zu\n",
                     rocketParams.size());
        pass = false;
    } else {
        std::printf("  [OK] RocketModule::GetParameters() returns 4 params\n");
    }

    // 3. Burst queue must have received exactly kDeadRocketCount payloads.
    if (burstEvents.empty()) {
        std::fprintf(stderr, "[FAIL] rocketToBurstQueue: no events emitted after Stage 1\n");
        pass = false;
    } else if (burstEvents[0].payloads.size() != kDeadRocketCount) {
        std::fprintf(stderr, "[FAIL] rocketToBurstQueue payloads: expected %u, got %zu\n",
                     kDeadRocketCount, burstEvents[0].payloads.size());
        pass = false;
    } else {
        std::printf("  [OK] rocketToBurstQueue received %u payload(s) (== kDeadRocketCount)\n",
                    kDeadRocketCount);
    }

    // 4. inheritVelocity=0.0: all velocities in burst payloads must be zero.
    if (!burstEvents.empty()) {
        bool velZero = true;
        for (const auto& p : burstEvents[0].payloads) {
            if (!FloatEq(p.velocity.x, 0.0f) ||
                !FloatEq(p.velocity.y, 0.0f) ||
                !FloatEq(p.velocity.z, 0.0f)) {
                velZero = false;
                break;
            }
        }
        if (!velZero) {
            std::fprintf(stderr,
                         "[FAIL] RocketModule inheritVelocity=0.0 but burst payloads have non-zero velocity\n");
            pass = false;
        } else {
            std::printf("  [OK] Burst payloads have zero velocity (inheritVelocity=0.0 honored)\n");
        }
    }

    // 5. inheritColor=true: burst payload colors must match rocket colors.
    if (!burstEvents.empty() && burstEvents[0].payloads.size() >= 2) {
        const uz::Vec4 expectedColor0 = {1.0f, 0.4f, 0.0f, 1.0f};
        const uz::Vec4 expectedColor1 = {1.0f, 0.6f, 0.0f, 1.0f};
        const auto& p0 = burstEvents[0].payloads[0];
        const auto& p1 = burstEvents[0].payloads[1];

        const bool colorOk =
            FloatEq(p0.color.x, expectedColor0.x) && FloatEq(p0.color.y, expectedColor0.y) &&
            FloatEq(p0.color.z, expectedColor0.z) && FloatEq(p0.color.w, expectedColor0.w) &&
            FloatEq(p1.color.x, expectedColor1.x) && FloatEq(p1.color.y, expectedColor1.y) &&
            FloatEq(p1.color.z, expectedColor1.z) && FloatEq(p1.color.w, expectedColor1.w);

        if (!colorOk) {
            std::fprintf(stderr, "[FAIL] Burst payload colors do not match source rocket colors\n");
            pass = false;
        } else {
            std::printf("  [OK] Burst payload colors match rocket colors (inheritColor=true honored)\n");
        }
    }

    // 6. Sparkle queue must have received exactly kDeadBurstCount payloads.
    if (sparkleEvents.empty()) {
        std::fprintf(stderr, "[FAIL] burstToSparkleQueue: no events emitted after Stage 2\n");
        pass = false;
    } else if (sparkleEvents[0].payloads.size() != kDeadBurstCount) {
        std::fprintf(stderr, "[FAIL] burstToSparkleQueue payloads: expected %u, got %zu\n",
                     kDeadBurstCount, sparkleEvents[0].payloads.size());
        pass = false;
    } else {
        std::printf("  [OK] burstToSparkleQueue received %u payload(s) (== kDeadBurstCount)\n",
                    kDeadBurstCount);
    }

    // 7. inheritVelocity=0.5: sparkle velocity = 0.5 * burst velocity.
    if (!sparkleEvents.empty() && sparkleEvents[0].payloads.size() >= 2) {
        const auto& sp0 = sparkleEvents[0].payloads[0];
        const auto& sp1 = sparkleEvents[0].payloads[1];

        const float expectedVx0 = burstReadback.flaggedParticles[0].velocity.x * kBurstInheritVelocity;
        const float expectedVy0 = burstReadback.flaggedParticles[0].velocity.y * kBurstInheritVelocity;
        const float expectedVz0 = burstReadback.flaggedParticles[0].velocity.z * kBurstInheritVelocity;
        const float expectedVx1 = burstReadback.flaggedParticles[1].velocity.x * kBurstInheritVelocity;
        const float expectedVy1 = burstReadback.flaggedParticles[1].velocity.y * kBurstInheritVelocity;
        const float expectedVz1 = burstReadback.flaggedParticles[1].velocity.z * kBurstInheritVelocity;

        const bool velOk =
            FloatEq(sp0.velocity.x, expectedVx0) && FloatEq(sp0.velocity.y, expectedVy0) &&
            FloatEq(sp0.velocity.z, expectedVz0) && FloatEq(sp1.velocity.x, expectedVx1) &&
            FloatEq(sp1.velocity.y, expectedVy1) && FloatEq(sp1.velocity.z, expectedVz1);

        if (!velOk) {
            std::fprintf(stderr,
                         "[FAIL] BurstModule inheritVelocity=0.5 but sparkle velocities are wrong\n");
            pass = false;
        } else {
            std::printf("  [OK] Sparkle velocities are half burst velocities (inheritVelocity=0.5 honored)\n");
        }
    }

    // 8. inheritColor=true: sparkle colors must match burst colors.
    if (!sparkleEvents.empty() && sparkleEvents[0].payloads.size() >= 2) {
        const auto& sp0 = sparkleEvents[0].payloads[0];
        const auto& sp1 = sparkleEvents[0].payloads[1];
        const uz::Vec4 expectedColor0 = {1.0f, 0.4f, 0.0f, 1.0f};
        const uz::Vec4 expectedColor1 = {1.0f, 0.6f, 0.0f, 1.0f};

        const bool colorOk =
            FloatEq(sp0.color.x, expectedColor0.x) && FloatEq(sp0.color.y, expectedColor0.y) &&
            FloatEq(sp0.color.z, expectedColor0.z) && FloatEq(sp0.color.w, expectedColor0.w) &&
            FloatEq(sp1.color.x, expectedColor1.x) && FloatEq(sp1.color.y, expectedColor1.y) &&
            FloatEq(sp1.color.z, expectedColor1.z) && FloatEq(sp1.color.w, expectedColor1.w);

        if (!colorOk) {
            std::fprintf(stderr, "[FAIL] Sparkle payload colors do not match source burst colors\n");
            pass = false;
        } else {
            std::printf("  [OK] Sparkle payload colors match burst colors (inheritColor=true honored)\n");
        }
    }

    // 9. Both queues must be empty after Drain().
    if (rocketToBurstQueue.HasPending()) {
        std::fprintf(stderr, "[FAIL] rocketToBurstQueue still has pending events after Drain()\n");
        pass = false;
    } else {
        std::printf("  [OK] rocketToBurstQueue is empty after Drain()\n");
    }

    if (burstToSparkleQueue.HasPending()) {
        std::fprintf(stderr, "[FAIL] burstToSparkleQueue still has pending events after Drain()\n");
        pass = false;
    } else {
        std::printf("  [OK] burstToSparkleQueue is empty after Drain()\n");
    }

    // 10. Execute() with null parameterData must be a no-op.
    {
        uz::ExecuteContext nullCtx;
        nullCtx.parameterData = nullptr;
        nullCtx.numParticles  = 0u;
        nullCtx.dt            = 0.0f;
        nullCtx.frameNumber   = 99u;
        rocketModule->Execute(nullCtx);

        if (rocketToBurstQueue.HasPending()) {
            std::fprintf(stderr, "[FAIL] Execute(null parameterData) should be no-op but emitted events\n");
            pass = false;
        } else {
            std::printf("  [OK] Execute(null parameterData) is a no-op (no crash, no events)\n");
        }
    }

    // 11. Execute() with empty flaggedParticles must be a no-op.
    {
        uz::SubEmitterReadback emptyReadback;
        uz::ExecuteContext emptyCtx;
        emptyCtx.parameterData = &emptyReadback;
        emptyCtx.numParticles  = 0u;
        emptyCtx.dt            = 0.0f;
        emptyCtx.frameNumber   = 100u;
        rocketModule->Execute(emptyCtx);

        if (rocketToBurstQueue.HasPending()) {
            std::fprintf(stderr,
                         "[FAIL] Execute(empty readback) should be no-op but emitted events\n");
            pass = false;
        } else {
            std::printf("  [OK] Execute(empty readback) is a no-op (no crash, no events)\n");
        }
    }

    // ── Cleanup ────────────────────────────────────────────────────────
    vfxSystem.Shutdown(&device);

    // ── Final result ───────────────────────────────────────────────────
    std::printf("\n==========================================================\n");
    std::printf("[OK] Sub-emitter firework: %zu stages loaded from .uzfx\n", descs.size());

    if (pass) {
        std::printf("  Rocket -> Burst -> Sparkle\n");
        std::printf("  [PASS] Sub-emitter firework demo completed successfully.\n");
        std::printf("==========================================================\n");
        return true;
    } else {
        std::fprintf(stderr, "  [FAIL] One or more validation checks failed.\n");
        std::printf("==========================================================\n");
        return false;
    }
    }

    void Shutdown() override {}
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    uz::LogInit();

    uz::EngineLoop loop;
    loop.RegisterSubsystem(std::make_unique<SubEmitterDemoSubsystem>());

    uz::EngineConfig config = uz::ParseCommandLine(argc, argv);
    config.maxFrames = 1;
    int result = loop.Run(config);

    uz::LogShutdown();
    return result;
}
