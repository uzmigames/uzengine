/// Phase 3 -- LOD distance + screen-coverage evaluation.
///
/// Compiled as a separate translation unit to isolate `vfx_lod.h` from
/// `vfx_significance_manager.h`. Both headers declare `VFXLodLevel` inside
/// `namespace uz`; including them in the same TU causes a redefinition error.
///
/// Moves a simulated camera from 0 m to 300 m away from a stationary emitter
/// in 31 evenly-spaced steps (0, 10, 20, ..., 300). At each step, calls
/// `VFXLodEvaluator::Evaluate()` with both the distance criterion and the
/// screen-coverage criterion and prints the resulting LOD level. Validates that:
///  - Distance 0–49 m  produces Full.
///  - Distance 50–99 m produces Reduced.
///  - Distance 100–199 m produces Minimal.
///  - Distance 200+ m  produces Culled.
///  - A bounding sphere of 0.01 m at 100 m is culled by the screen-size
///    criterion (below the 4-pixel default threshold at 1080p).

#include "engine/vfx/lod/vfx_lod.h"

#include <cstdio>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Phase 3 constants
// ─────────────────────────────────────────────────────────────────────────────

/// Camera steps along the Z axis (metres from the emitter origin).
static constexpr int   kStepCount     = 31;
static constexpr float kStepSize      = 10.0f;  // 0, 10, 20, ..., 300 m

/// Simulated viewport height for screen-coverage calculations.
static constexpr float kScreenHeight  = 1080.0f;

/// Bounding sphere radius for the primary (normal-sized) emitter.
static constexpr float kBoundingRadiusLarge = 2.0f;

/// Bounding sphere radius for the tiny-emitter screen-cull test.
static constexpr float kBoundingRadiusTiny  = 0.01f;

/// Pixel threshold used by VFXScreenCuller::ShouldCull() (default = 4.0 px).
static constexpr float kMinScreenPixels = 4.0f;

// ─────────────────────────────────────────────────────────────────────────────
// RunLodPhase
// ─────────────────────────────────────────────────────────────────────────────

/// Runs the LOD evaluation phase.
///
/// # Returns
/// `true` if all distance-band assertions pass and the tiny-emitter cull test
/// succeeds; `false` on the first failure.
bool RunLodPhase() {
    std::printf("\n"
                "══════════════════════════════════════════════════════════════\n"
                "  Phase 3 -- LOD System (distance + screen coverage)\n"
                "══════════════════════════════════════════════════════════════\n");

    // Default thresholds: Full<50 / Reduced<100 / Minimal<200 / Culled>=200.
    const uz::VFXLodSettings settings{};
    if (!settings.IsValid()) {
        std::fprintf(stderr, "  [FAIL] Default VFXLodSettings::IsValid() returned false\n");
        return false;
    }

    std::printf("  LOD thresholds: Full<%.0fm  Reduced<%.0fm  Minimal<%.0fm  Culled>=%.0fm\n",
                static_cast<double>(settings.distances[0]),
                static_cast<double>(settings.distances[1]),
                static_cast<double>(settings.distances[2]),
                static_cast<double>(settings.distances[2]));
    std::printf("  Emitter radius: %.2f m  |  Viewport height: %.0f px\n\n",
                static_cast<double>(kBoundingRadiusLarge),
                static_cast<double>(kScreenHeight));

    std::printf("  %-8s  %-10s  %-12s  %-12s  %-10s  %s\n",
                "Dist(m)", "ScreenPx", "DistLOD", "ScreenLOD", "FinalLOD", "SpawnFactor");
    std::printf("  ──────────────────────────────────────────────────────────\n");

    // Stationary emitter at the world origin.
    const uz::Vec3 emitterPos{0.0f, 0.0f, 0.0f};

    bool allOk = true;

    for (int step = 0; step < kStepCount; ++step) {
        const float distance = static_cast<float>(step) * kStepSize;

        // Camera moves directly along the +Z axis.
        const uz::Vec3 cameraPos{0.0f, 0.0f, distance};

        const uz::VFXLodLevel distLod = uz::VFXLodEvaluator::ComputeFromDistance(
            emitterPos, cameraPos, settings);

        const uz::VFXLodLevel screenLod = uz::VFXLodEvaluator::ComputeFromScreenSize(
            kBoundingRadiusLarge, distance, kScreenHeight, kMinScreenPixels);

        const uz::VFXLodLevel finalLod = uz::VFXLodEvaluator::Evaluate(
            emitterPos, cameraPos,
            kBoundingRadiusLarge, kScreenHeight,
            settings, kMinScreenPixels);

        const float screenPixels = uz::VFXScreenCuller::ComputeScreenPixels(
            kBoundingRadiusLarge, distance, kScreenHeight);

        const float spawnFactor = uz::VFXLodLevelToSpawnFactor(finalLod);

        std::printf("  %-8.0f  %-10.2f  %-12s  %-12s  %-10s  %.2f\n",
                    static_cast<double>(distance),
                    static_cast<double>(screenPixels),
                    uz::VFXLodLevelToString(distLod),
                    uz::VFXLodLevelToString(screenLod),
                    uz::VFXLodLevelToString(finalLod),
                    static_cast<double>(spawnFactor));

        // Validate distance-band assignments using the default thresholds.
        uz::VFXLodLevel expectedDistLod;
        if (distance < settings.distances[0]) {
            expectedDistLod = uz::VFXLodLevel::Full;
        } else if (distance < settings.distances[1]) {
            expectedDistLod = uz::VFXLodLevel::Reduced;
        } else if (distance < settings.distances[2]) {
            expectedDistLod = uz::VFXLodLevel::Minimal;
        } else {
            expectedDistLod = uz::VFXLodLevel::Culled;
        }

        if (distLod != expectedDistLod) {
            std::fprintf(stderr,
                         "  [FAIL] dist=%.0fm: expected DistLOD=%s, got %s\n",
                         static_cast<double>(distance),
                         uz::VFXLodLevelToString(expectedDistLod),
                         uz::VFXLodLevelToString(distLod));
            allOk = false;
        }

        // Validate spawn factor consistency with the LOD level.
        float expectedFactor = 0.0f;
        switch (finalLod) {
            case uz::VFXLodLevel::Full:    expectedFactor = 1.00f; break;
            case uz::VFXLodLevel::Reduced: expectedFactor = 0.50f; break;
            case uz::VFXLodLevel::Minimal: expectedFactor = 0.25f; break;
            case uz::VFXLodLevel::Culled:  expectedFactor = 0.00f; break;
        }
        if (spawnFactor != expectedFactor) {
            std::fprintf(stderr,
                         "  [FAIL] dist=%.0fm: spawn factor %.2f != expected %.2f\n",
                         static_cast<double>(distance),
                         static_cast<double>(spawnFactor),
                         static_cast<double>(expectedFactor));
            allOk = false;
        }
    }

    std::printf("  ──────────────────────────────────────────────────────────\n\n");

    // ── Tiny-emitter screen-cull test ─────────────────────────────────────────
    // A 0.01 m bounding sphere at 100 m produces ~0.054 px -- well below the 4 px
    // threshold. The Evaluate() call must return Culled regardless of distance band.
    {
        static constexpr float kTestDistance = 100.0f;
        const uz::Vec3 farCamera{0.0f, 0.0f, kTestDistance};

        const uz::VFXLodLevel tinyLod = uz::VFXLodEvaluator::Evaluate(
            emitterPos, farCamera,
            kBoundingRadiusTiny, kScreenHeight,
            settings, kMinScreenPixels);

        const float tinyPixels = uz::VFXScreenCuller::ComputeScreenPixels(
            kBoundingRadiusTiny, kTestDistance, kScreenHeight);

        std::printf("  Screen-cull test: radius=%.3f m  dist=%.0f m  "
                    "screenPx=%.4f  lod=%s\n",
                    static_cast<double>(kBoundingRadiusTiny),
                    static_cast<double>(kTestDistance),
                    static_cast<double>(tinyPixels),
                    uz::VFXLodLevelToString(tinyLod));

        if (tinyLod != uz::VFXLodLevel::Culled) {
            std::fprintf(stderr,
                         "  [FAIL] Tiny emitter at %.0f m should be Culled by screen "
                         "criterion (got %s, screenPx=%.4f)\n",
                         static_cast<double>(kTestDistance),
                         uz::VFXLodLevelToString(tinyLod),
                         static_cast<double>(tinyPixels));
            allOk = false;
        } else {
            std::printf("  [OK]  Tiny emitter correctly culled by screen-size criterion\n");
        }
    }

    // ── VFXLodSettings validation ─────────────────────────────────────────────
    {
        uz::VFXLodSettings badSettings;
        badSettings.distances[0] = 200.0f;
        badSettings.distances[1] = 100.0f; // Not strictly increasing — invalid.
        badSettings.distances[2] = 300.0f;

        if (badSettings.IsValid()) {
            std::fprintf(stderr,
                         "  [FAIL] Non-increasing thresholds should fail IsValid()\n");
            allOk = false;
        } else {
            std::printf("  [OK]  Non-increasing LOD thresholds correctly rejected by IsValid()\n");
        }
    }

    // ── String helper round-trips ─────────────────────────────────────────────
    const uz::VFXLodLevel levels[] = {
        uz::VFXLodLevel::Full,
        uz::VFXLodLevel::Reduced,
        uz::VFXLodLevel::Minimal,
        uz::VFXLodLevel::Culled,
    };
    std::printf("  LOD level string table:\n");
    for (const auto lvl : levels) {
        std::printf("    %-8s  spawnFactor=%.2f\n",
                    uz::VFXLodLevelToString(lvl),
                    static_cast<double>(uz::VFXLodLevelToSpawnFactor(lvl)));
    }

    if (allOk) {
        std::printf("  [OK]  All LOD distance-band and screen-cull assertions passed\n");
    }
    return allOk;
}
