/// VFX Advanced Demo -- Phase 3: LOD System.
///
/// Isolated in its own translation unit because `vfx_lod.h` and
/// `vfx_significance_manager.h` both declare `VFXLodLevel` inside `namespace uz`.
/// Including both in the same TU would produce a redefinition error.
///
/// This file includes only `vfx_lod.h`.

#include <algorithm>
#include <cstdio>

#include "engine/vfx/lod/vfx_lod.h"

// ─────────────────────────────────────────────────────────────────────────────
// LOD phase constants
// ─────────────────────────────────────────────────────────────────────────────

/// Number of distance samples between 0 m and kLodMaxDistance.
static constexpr int   kLodSteps         = 31;

/// Maximum camera distance exercised in the LOD table (metres).
static constexpr float kLodMaxDistance   = 300.0f;

/// Bounding radius (metres) of the simulated emitter's bounding sphere.
static constexpr float kBoundingRadius   = 5.0f;

/// Simulated viewport height (pixels) for screen-coverage calculations.
static constexpr float kScreenHeight     = 1080.0f;

/// Screen-size cull threshold (pixels). Emitters smaller than this are culled.
static constexpr float kMinScreenPixels  = 4.0f;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers (local to this TU)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

void PrintSeparator() {
    std::printf("──────────────────────────────────────────────────────────────\n");
}

void PrintPhaseHeader(const char* title) {
    std::printf("\n");
    PrintSeparator();
    std::printf("  %s\n", title);
    PrintSeparator();
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// RunLodPhase -- exported to main.cpp via forward declaration
// ─────────────────────────────────────────────────────────────────────────────

/// Simulates a camera receding from an emitter and prints the LOD evaluation table.
///
/// The emitter is fixed at the world origin. The camera starts 0 m away and
/// moves to `kLodMaxDistance` in `kLodSteps` equal increments. At each step,
/// calls `VFXLodEvaluator::Evaluate()` using default `VFXLodSettings`
/// thresholds (50 / 100 / 200 m) and a 5 m bounding radius. Prints the
/// distance → distance-LOD → screen-LOD → combined-LOD → spawn-factor table.
///
/// Also exercises `VFXScreenCuller::ShouldCull()` at five representative
/// distances to verify screen-pixel-coverage arithmetic.
///
/// # Returns
/// `true` if LOD levels are monotone non-decreasing as distance grows and all
/// screen-cull boundary checks match expectations; `false` otherwise.
bool RunLodPhase() {
    PrintPhaseHeader("Phase 3 -- LOD System (camera 0 m to 300 m)");

    const uz::Vec3 kEmitterPos{0.0f, 0.0f, 0.0f};

    uz::VFXLodSettings settings;
    // Explicit assignment documents the default thresholds used in this demo.
    settings.distances[0] =  50.0f;
    settings.distances[1] = 100.0f;
    settings.distances[2] = 200.0f;

    if (!settings.IsValid()) {
        std::fprintf(stderr, "  [FAIL] Default VFXLodSettings are not valid\n");
        return false;
    }

    std::printf("  Thresholds: Full < %.0f m | Reduced < %.0f m | Minimal < %.0f m | Culled >= %.0f m\n",
                static_cast<double>(settings.distances[0]),
                static_cast<double>(settings.distances[1]),
                static_cast<double>(settings.distances[2]),
                static_cast<double>(settings.distances[2]));
    std::printf("  BoundingRadius=%.1f m  ScreenHeight=%.0f px  MinScreenPixels=%.1f px\n\n",
                static_cast<double>(kBoundingRadius),
                static_cast<double>(kScreenHeight),
                static_cast<double>(kMinScreenPixels));

    std::printf("  %-10s  %-9s  %-9s  %-9s  %s\n",
                "Dist(m)", "LOD(dist)", "LOD(scrn)", "LOD(fin)", "SpawnFactor");
    PrintSeparator();

    bool monotone  = true;
    auto prevLevel = uz::VFXLodLevel::Full;

    for (int step = 0; step < kLodSteps; ++step) {
        const float distance =
            (static_cast<float>(step) / static_cast<float>(kLodSteps - 1))
            * kLodMaxDistance;

        // Camera is placed along the +X axis at the given distance.
        const uz::Vec3 cameraPos{distance, 0.0f, 0.0f};

        const uz::VFXLodLevel distLod = uz::VFXLodEvaluator::ComputeFromDistance(
            kEmitterPos, cameraPos, settings);

        const uz::VFXLodLevel screenLod = uz::VFXLodEvaluator::ComputeFromScreenSize(
            kBoundingRadius, distance, kScreenHeight, kMinScreenPixels);

        const uz::VFXLodLevel finalLod = uz::VFXLodEvaluator::Evaluate(
            kEmitterPos, cameraPos,
            kBoundingRadius, kScreenHeight,
            settings, kMinScreenPixels);

        const float spawnFactor = uz::VFXLodLevelToSpawnFactor(finalLod);

        std::printf("  %10.1f  %-9s  %-9s  %-9s  %.2f\n",
                    static_cast<double>(distance),
                    uz::VFXLodLevelToString(distLod),
                    uz::VFXLodLevelToString(screenLod),
                    uz::VFXLodLevelToString(finalLod),
                    static_cast<double>(spawnFactor));

        // LOD must be monotone non-decreasing as distance grows.
        if (static_cast<uint8_t>(finalLod) < static_cast<uint8_t>(prevLevel)) {
            std::fprintf(stderr,
                         "  [FAIL] LOD went from %s to %s at %.1f m -- "
                         "must be non-decreasing\n",
                         uz::VFXLodLevelToString(prevLevel),
                         uz::VFXLodLevelToString(finalLod),
                         static_cast<double>(distance));
            monotone = false;
        }
        prevLevel = finalLod;
    }

    PrintSeparator();

    // ── Screen-size cull boundary test ────────────────────────────────────────
    // At very large distances a 5 m sphere projects to fewer than 4 px on a
    // 1080 p display. The exact cross-over depends on the formula in vfx_lod.cpp.
    struct ScreenCullCase {
        float distance;
        bool  expectCulled;
    };

    static constexpr ScreenCullCase kScreenCases[] = {
        {    1.0f, false },  // 5 m sphere at 1 m -- large on screen
        {   50.0f, false },  // still many pixels at 50 m
        {  200.0f, false },  // marginal but likely still > 4 px
        {  800.0f, true  },  // sub-4-pixel at 800 m
        { 2000.0f, true  },  // far beyond cull threshold
    };

    std::printf("\n  Screen-size cull boundary (radius=%.1f m, screen=%.0f px, min=%.1f px):\n",
                static_cast<double>(kBoundingRadius),
                static_cast<double>(kScreenHeight),
                static_cast<double>(kMinScreenPixels));
    std::printf("  %-12s  %-12s  %-12s  %s\n",
                "Distance(m)", "ScreenPx", "ShouldCull", "Expected");

    bool screenOk = true;
    for (const auto& sc : kScreenCases) {
        const float screenPx = uz::VFXScreenCuller::ComputeScreenPixels(
            kBoundingRadius, sc.distance, kScreenHeight);
        const bool culled = uz::VFXScreenCuller::ShouldCull(
            kBoundingRadius, sc.distance, kScreenHeight, kMinScreenPixels);

        const bool match = (culled == sc.expectCulled);
        if (!match) {
            std::fprintf(stderr,
                         "  [FAIL] Distance=%.1f: ShouldCull=%s expected=%s\n",
                         static_cast<double>(sc.distance),
                         culled ? "yes" : "no",
                         sc.expectCulled ? "yes" : "no");
            screenOk = false;
        }

        std::printf("  %12.1f  %12.3f  %-12s  %s\n",
                    static_cast<double>(sc.distance),
                    static_cast<double>(screenPx),
                    culled ? "yes" : "no",
                    match ? "OK" : "FAIL");
    }

    const bool allOk = monotone && screenOk;
    std::printf("\n  Phase 3 result: monotone=%s  screenCull=%s  [%s]\n",
                monotone ? "yes" : "NO",
                screenOk ? "yes" : "NO",
                allOk    ? "OK"  : "FAIL");

    return allOk;
}
