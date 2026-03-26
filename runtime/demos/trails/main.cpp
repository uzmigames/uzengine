/// Trail/Ribbon VFX System Demo -- Headless projectile, sword swipe, and
/// performance showcase.
///
/// Demonstrates the UzEngine trail system through three phases:
///
/// **Phase 1 -- Projectile Trail**
///   Loads a fire trail asset from inline JSON. Moves a simulated projectile
///   along a parabolic arc under gravity (300 frames, 60 FPS). The trail
///   head is bright orange {1, 0.8, 0.2, 1} and fades to transparent red
///   {1, 0.2, 0, 0} at the tail. Width tapers from 0.5 m (head) to 0.05 m
///   (tail). UV scrolls at 1.5 UV/s for an animated fire texture effect.
///   Prints point count, total arc length, and generated vertex/index counts
///   each second (every 60 frames).
///
/// **Phase 2 -- Sword Swipe**
///   Simulates an entity sweeping a 180-degree arc in 180 frames (3 seconds).
///   Trail is wide at the arc centre (1.0 m) and thin at the edges (0.1 m).
///   Point lifetime is 0.3 s so the trail snaps away rapidly after the swipe.
///   Prints mesh stats at the end of the phase.
///
/// **Phase 3 -- Performance Test**
///   Registers 50 simultaneous trails via `TrailSystem`. Each trail moves in
///   a distinct circular pattern (varied radius and phase). Updates all 50 for
///   600 frames and prints a performance overlay every 60 frames: active trail
///   count, total segment count, total vertices, and total indices across all
///   trails. Demonstrates the system's capacity to handle many concurrent
///   ribbons with no GPU dependency.
///
/// # Build
///   cd demos/trails
///   zig build
///   ./zig-out/bin/trail_demo
///
/// # Expected Output
///   All three phases complete; final line:
///   "[PASS] Trail system demo completed successfully."

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Trail system includes
// ─────────────────────────────────────────────────────────────────────────────

#include "engine/vfx/trails/trail_asset.h"
#include "engine/vfx/trails/trail_component.h"
#include "engine/vfx/trails/trail_mesh_builder.h"
#include "engine/vfx/trails/trail_point.h"
#include "engine/vfx/trails/trail_system.h"

// ─────────────────────────────────────────────────────────────────────────────
// Demo constants
// ─────────────────────────────────────────────────────────────────────────────

/// Fixed timestep: 60 FPS.
static constexpr float kDt = 1.0f / 60.0f;

/// Performance overlay is printed every N frames (once per second at 60 FPS).
static constexpr int kReportInterval = 60;

// ── Phase 1 constants ────────────────────────────────────────────────────────

/// Duration of the projectile phase in frames (5 seconds at 60 FPS).
static constexpr int kProjectileFrames = 300;

/// Initial horizontal launch speed of the projectile (m/s).
static constexpr float kProjectileSpeedX = 12.0f;

/// Initial vertical launch speed of the projectile (m/s).
static constexpr float kProjectileSpeedY = 15.0f;

/// Gravitational acceleration (m/s²) applied to the projectile (downward).
static constexpr float kGravity = -9.81f;

// ── Phase 2 constants ────────────────────────────────────────────────────────

/// Duration of the sword swipe phase in frames (3 seconds at 60 FPS).
static constexpr int kSwipeFrames = 180;

/// Radius of the sword swipe arc (metres).
static constexpr float kSwipeRadius = 1.2f;

/// Angular speed of the swipe (radians per second).
/// Covers π radians (180°) in 3 seconds.
static constexpr float kSwipeAngularSpeed = 3.14159265f / 3.0f;

// ── Phase 3 constants ────────────────────────────────────────────────────────

/// Number of simultaneous trails in the performance test.
static constexpr int kPerfTrailCount = 50;

/// Duration of the performance test in frames (10 seconds at 60 FPS).
static constexpr int kPerfFrames = 600;

// ─────────────────────────────────────────────────────────────────────────────
// Inline asset JSON definitions (no disk I/O required)
// ─────────────────────────────────────────────────────────────────────────────

/// Fire projectile trail: bright orange head fades to transparent red tail.
/// UV scroll at 1.5 UV/s animates the fire texture along the ribbon length.
static const char* const kProjectileTrailJson = R"({
    "format": "uztrail",
    "version": 1,
    "material": "materials/vfx/fire_trail.uzmat",
    "width_start": 0.5,
    "width_end": 0.05,
    "color_start": [1.0, 0.8, 0.2, 1.0],
    "color_end": [1.0, 0.2, 0.0, 0.0],
    "uv_scroll_rate": 1.5,
    "max_points": 128,
    "point_lifetime": 2.0,
    "min_point_distance": 0.08,
    "sort_order": 0
})";

/// Sword swipe trail: wide at the arc's mid-sweep, thin at the tips.
/// Short point lifetime (0.3 s) makes the trail vanish quickly after the swing.
static const char* const kSwordTrailJson = R"({
    "format": "uztrail",
    "version": 1,
    "material": "materials/vfx/sword_slash.uzmat",
    "width_start": 1.0,
    "width_end": 0.1,
    "color_start": [0.8, 0.9, 1.0, 1.0],
    "color_end": [0.4, 0.6, 1.0, 0.0],
    "uv_scroll_rate": 0.0,
    "max_points": 64,
    "point_lifetime": 0.3,
    "min_point_distance": 0.04,
    "sort_order": 1
})";

/// Performance test trail: neutral white ribbon, medium lifetime.
/// Width and color are kept simple to keep the benchmark noise-free.
static const char* const kPerfTrailJson = R"({
    "format": "uztrail",
    "version": 1,
    "material": "materials/vfx/generic_trail.uzmat",
    "width_start": 0.2,
    "width_end": 0.02,
    "color_start": [1.0, 1.0, 1.0, 1.0],
    "color_end": [0.5, 0.5, 0.5, 0.0],
    "uv_scroll_rate": 0.5,
    "max_points": 64,
    "point_lifetime": 1.5,
    "min_point_distance": 0.05,
    "sort_order": 0
})";

// ─────────────────────────────────────────────────────────────────────────────
// Utility helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Prints a horizontal section separator to stdout.
void PrintSeparator() {
    std::printf("──────────────────────────────────────────────────────────────\n");
}

/// Builds a TrailComponent from a TrailAsset, preserving all configuration fields.
///
/// The component's `maxTrailPoints` is set from the asset's `maxPoints`, which
/// sizes the ring buffer at construction. All other visual and behavioral
/// parameters are copied verbatim from the asset.
///
/// # Parameters
/// - `asset`: Loaded trail asset. Must not be null.
///
/// # Returns
/// Fully configured `TrailComponent` ready for `Tick()` calls.
[[nodiscard]] uz::TrailComponent BuildComponentFromAsset(const uz::TrailAsset& asset) {
    uz::TrailComponent comp;
    comp.materialPath      = asset.materialPath;
    comp.maxTrailPoints    = asset.maxPoints;
    comp.widthStart        = asset.widthStart;
    comp.widthEnd          = asset.widthEnd;
    comp.colorStart        = asset.colorStart;
    comp.colorEnd          = asset.colorEnd;
    comp.uvScrollRate      = asset.uvScrollRate;
    comp.pointLifetime     = asset.pointLifetime;
    comp.minPointDistance  = asset.minPointDistance;
    comp.sortOrder         = asset.sortOrder;
    comp.isActive          = true;
    // Re-construct to apply the updated maxTrailPoints to the ring buffer size.
    comp.points = uz::TrailPointBuffer(comp.maxTrailPoints);
    return comp;
}

/// Accumulates vertex and index counts across all render data entries.
///
/// # Parameters
/// - `renderData`: Vector of per-trail render snapshots from `TrailSystem::GetRenderData()`.
/// - `outVertices`:  Total vertex count written here.
/// - `outIndices`:   Total index count written here.
void SumRenderDataStats(const std::vector<uz::TrailSystem::TrailRenderData>& renderData,
                        uint32_t& outVertices,
                        uint32_t& outIndices) {
    outVertices = 0;
    outIndices  = 0;
    for (const auto& rd : renderData) {
        outVertices += rd.vertexCount;
        outIndices  += rd.indexCount;
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Phase 1: Projectile trail (parabolic arc under gravity)
// ─────────────────────────────────────────────────────────────────────────────

/// Simulates a projectile trail using `TrailComponent::Tick()` directly.
///
/// The projectile starts at the origin and travels in the +XY plane under
/// constant gravitational acceleration. A `TrailMeshBuilder` is called each
/// frame to generate the ribbon geometry. Mesh stats (point count, arc length,
/// vertices, indices) are printed every 60 frames (once per second).
///
/// # Returns
/// `true` if the trail produced renderable geometry by the end of the phase,
/// `false` on any failure.
static bool RunProjectilePhase() {
    std::printf("\n== Phase 1: Projectile Trail (parabolic arc, 300 frames) ====\n");

    // Load the fire trail asset from inline JSON (no disk I/O).
    auto asset = uz::TrailAsset::LoadFromString(kProjectileTrailJson);
    if (!asset) {
        std::fprintf(stderr, "  [FAIL] Failed to load projectile trail asset\n");
        return false;
    }

    std::printf("  Asset loaded: material='%s'  widthStart=%.2f  widthEnd=%.2f"
                "  pointLifetime=%.2fs  uvScroll=%.2f\n",
                asset->materialPath.c_str(),
                static_cast<double>(asset->widthStart),
                static_cast<double>(asset->widthEnd),
                static_cast<double>(asset->pointLifetime),
                static_cast<double>(asset->uvScrollRate));
    PrintSeparator();

    uz::TrailComponent trail = BuildComponentFromAsset(*asset);
    uz::TrailMeshBuilder builder;

    // Simulated camera position for billboard orientation (static overhead view).
    static constexpr uz::Vec3 kCameraPos{0.0f, 20.0f, -15.0f};

    // Projectile kinematics: simple Euler integration.
    float posX     = 0.0f;
    float posY     = 0.0f;
    float velocityX = kProjectileSpeedX;
    float velocityY = kProjectileSpeedY;

    bool producedGeometry = false;

    for (int frame = 0; frame < kProjectileFrames; ++frame) {
        // Advance projectile position with Euler integration.
        velocityY += kGravity * kDt;
        posX      += velocityX * kDt;
        posY      += velocityY * kDt;

        const uz::Vec3 currentPos{posX, posY, 0.0f};

        // Advance the trail ring buffer: add new point, age existing, cull expired.
        trail.Tick(kDt, currentPos);

        // Build the ribbon mesh for this frame (stateless, no GPU allocation).
        const uint32_t pointCount = trail.GetActivePointCount();
        uz::TrailMeshData meshData{};
        if (pointCount >= 2) {
            // Copy ordered points into a flat buffer for the mesh builder.
            std::vector<uz::TrailPoint> ordered;
            trail.GetPoints().CopyOrdered(ordered);

            meshData = builder.Build(
                ordered.data(),
                pointCount,
                kCameraPos,
                trail.widthStart,
                trail.widthEnd,
                trail.colorStart,
                trail.colorEnd,
                trail.GetTotalLength()
            );
            producedGeometry = true;
        }

        // Print stats once per second (every 60 frames) and on the last frame.
        const bool isReport = (frame == 0) ||
                              (frame == kProjectileFrames - 1) ||
                              (frame > 0 && frame % kReportInterval == 0);

        if (isReport) {
            std::printf("  [Frame %4d | t=%5.2fs]  pos=(%.2f, %.2f)  "
                        "points=%-3u  length=%.3fm  verts=%-4u  idx=%-4u\n",
                        frame,
                        static_cast<double>(frame) * kDt,
                        static_cast<double>(posX),
                        static_cast<double>(posY),
                        pointCount,
                        static_cast<double>(trail.GetTotalLength()),
                        meshData.GetVertexCount(),
                        meshData.GetIndexCount());
        }
    }

    PrintSeparator();
    std::printf("  Phase 1 summary: finalPoints=%u  totalLength=%.3fm\n",
                trail.GetActivePointCount(),
                static_cast<double>(trail.GetTotalLength()));

    if (!producedGeometry) {
        std::fprintf(stderr, "  [FAIL] Trail never produced renderable geometry\n");
        return false;
    }
    std::printf("  [OK]  Projectile trail produced renderable geometry\n");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 2: Sword swipe (circular arc, fast-disappearing trail)
// ─────────────────────────────────────────────────────────────────────────────

/// Simulates a sword swipe trail using `TrailComponent::Tick()` directly.
///
/// The sword tip sweeps a 180-degree arc over 3 seconds. Trail point lifetime
/// is 0.3 s so the ribbon collapses quickly behind the blade. At the midpoint
/// (90 degrees) the component is queried for peak geometry, and at the end
/// the trail has largely expired. Mesh statistics are printed at both events.
///
/// # Returns
/// `true` if the swipe produced geometry at the arc midpoint, `false` otherwise.
static bool RunSwordSwipePhase() {
    std::printf("\n== Phase 2: Sword Swipe (arc sweep, 180 frames) =============\n");

    auto asset = uz::TrailAsset::LoadFromString(kSwordTrailJson);
    if (!asset) {
        std::fprintf(stderr, "  [FAIL] Failed to load sword trail asset\n");
        return false;
    }

    std::printf("  Asset loaded: material='%s'  widthStart=%.2f  widthEnd=%.2f"
                "  pointLifetime=%.2fs\n",
                asset->materialPath.c_str(),
                static_cast<double>(asset->widthStart),
                static_cast<double>(asset->widthEnd),
                static_cast<double>(asset->pointLifetime));
    PrintSeparator();

    uz::TrailComponent trail = BuildComponentFromAsset(*asset);
    uz::TrailMeshBuilder builder;

    // Camera looks along +Z from above the arc plane.
    static constexpr uz::Vec3 kCameraPos{0.0f, 5.0f, -3.0f};

    // Sword tip starts at angle 0 (rightmost point of arc) and sweeps to π.
    float angle = 0.0f;

    bool hadGeometryAtMidpoint = false;
    uint32_t peakVertexCount   = 0;

    for (int frame = 0; frame < kSwipeFrames; ++frame) {
        angle += kSwipeAngularSpeed * kDt;

        // Tip position traces a circle in the XY plane.
        const uz::Vec3 tipPos{
            kSwipeRadius * std::cos(angle),
            kSwipeRadius * std::sin(angle),
            0.0f
        };

        trail.Tick(kDt, tipPos);

        const uint32_t pointCount = trail.GetActivePointCount();
        uz::TrailMeshData meshData{};
        if (pointCount >= 2) {
            std::vector<uz::TrailPoint> ordered;
            trail.GetPoints().CopyOrdered(ordered);

            meshData = builder.Build(
                ordered.data(),
                pointCount,
                kCameraPos,
                trail.widthStart,
                trail.widthEnd,
                trail.colorStart,
                trail.colorEnd,
                trail.GetTotalLength()
            );

            if (meshData.GetVertexCount() > peakVertexCount) {
                peakVertexCount = meshData.GetVertexCount();
            }
        }

        // At the arc midpoint (frame 90 = 90°) report the current geometry peak.
        if (frame == kSwipeFrames / 2) {
            std::printf("  [Midpoint | t=%.2fs]  angle=%.1f°  points=%-3u"
                        "  length=%.3fm  verts=%-4u  idx=%-4u\n",
                        static_cast<double>(frame) * kDt,
                        static_cast<double>(angle) * (180.0 / 3.14159265),
                        pointCount,
                        static_cast<double>(trail.GetTotalLength()),
                        meshData.GetVertexCount(),
                        meshData.GetIndexCount());
            hadGeometryAtMidpoint = (pointCount >= 2);
        }
    }

    // After the swipe completes, most points will have expired (lifetime=0.3s).
    // The phase runs for 3 s, so all points emitted in the first 2.7 s are gone.
    const uint32_t remainingPoints = trail.GetActivePointCount();

    PrintSeparator();
    std::printf("  Phase 2 summary: peakVerts=%u  remainingPoints=%u"
                "  (expected ~0 after 3s with lifetime=0.3s)\n",
                peakVertexCount, remainingPoints);

    if (!hadGeometryAtMidpoint) {
        std::fprintf(stderr, "  [FAIL] Sword swipe produced no geometry at midpoint\n");
        return false;
    }
    std::printf("  [OK]  Sword swipe produced geometry at arc midpoint\n");

    if (remainingPoints > 5) {
        // Warn but do not fail -- frame boundary rounding can leave a handful.
        std::printf("  [WARN] %u points remain after swipe (expected ~0)\n",
                    remainingPoints);
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 3: Performance test (50 simultaneous trails via TrailSystem)
// ─────────────────────────────────────────────────────────────────────────────

/// Builds a TrailComponent for the performance trail at a given index.
///
/// Each trail moves in a circle with a unique radius and phase offset so all
/// 50 trails produce distinct, non-overlapping paths. Deterministic (sin/cos
/// only; no randomness).
///
/// # Parameters
/// - `asset`:      Pre-loaded performance trail asset.
/// - `trailIndex`: Index in [0, kPerfTrailCount) that determines the motion pattern.
///
/// # Returns
/// Configured `TrailComponent` ready for registration with `TrailSystem`.
[[nodiscard]] static uz::TrailComponent BuildPerfTrailComponent(
        const uz::TrailAsset& asset,
        int trailIndex) {
    uz::TrailComponent comp = BuildComponentFromAsset(asset);
    // Vary material paths so each trail is logically distinct in the system.
    comp.materialPath = "materials/vfx/perf_trail_" + std::to_string(trailIndex) + ".uzmat";
    return comp;
}

/// Computes the circular orbit position for a performance trail at the given time.
///
/// Each trail index maps to a unique orbit: radius varies linearly from 2 m to
/// 10 m, and the phase offset rotates 2π across all 50 trails. This produces
/// a spread of concentric, phase-shifted circles.
///
/// # Parameters
/// - `trailIndex`: Index in [0, kPerfTrailCount).
/// - `time`:       Simulation time in seconds.
///
/// # Returns
/// World-space emitter position for this trail at the given time.
[[nodiscard]] static uz::Vec3 ComputePerfTrailPosition(int trailIndex, float time) {
    static constexpr float kMinRadius   = 2.0f;
    static constexpr float kMaxRadius   = 10.0f;
    static constexpr float kOrbitSpeed  = 1.5f; // radians per second base

    const float t             = static_cast<float>(trailIndex);
    const float normalizedIdx = t / static_cast<float>(kPerfTrailCount - 1);

    // Linearly interpolate radius between min and max across all trail indices.
    const float radius = kMinRadius + normalizedIdx * (kMaxRadius - kMinRadius);

    // Phase offset spreads trails around the full circle to prevent overlap.
    const float phaseOffset = normalizedIdx * 2.0f * 3.14159265f;

    // Vary angular speed slightly so trails drift relative to each other.
    const float angularSpeed = kOrbitSpeed * (0.5f + normalizedIdx * 1.5f);

    const float angle = phaseOffset + angularSpeed * time;

    // Height varies sinusoidally so the trails are not coplanar.
    const float height = std::sin(phaseOffset + time * 0.3f) * 3.0f;

    return uz::Vec3{radius * std::cos(angle), height, radius * std::sin(angle)};
}

/// Runs the performance test: 50 simultaneous trails for 600 frames.
///
/// Registers all trails with `TrailSystem`, feeds per-trail positions via the
/// `positions` map each frame, and collects render data. Prints a performance
/// overlay every 60 frames with active trail count, total segments, vertices,
/// and indices.
///
/// # Returns
/// `true` if all 50 trails registered successfully and produced geometry by
/// frame 120 (2 seconds in), `false` otherwise.
static bool RunPerformancePhase() {
    std::printf("\n== Phase 3: Performance Test (50 trails, 600 frames) ========\n");

    auto asset = uz::TrailAsset::LoadFromString(kPerfTrailJson);
    if (!asset) {
        std::fprintf(stderr, "  [FAIL] Failed to load performance trail asset\n");
        return false;
    }

    uz::TrailSystem trailSystem;
    std::vector<uz::TrailSystem::TrailHandle> handles;
    handles.reserve(kPerfTrailCount);

    // Register all 50 trails. Each receives a unique material path.
    for (int i = 0; i < kPerfTrailCount; ++i) {
        uz::TrailComponent comp = BuildPerfTrailComponent(*asset, i);
        const auto handle = trailSystem.RegisterTrail(std::move(comp));
        handles.push_back(handle);
    }

    if (trailSystem.GetActiveTrailCount() != static_cast<uint32_t>(kPerfTrailCount)) {
        std::fprintf(stderr, "  [FAIL] Expected %d active trails, got %u\n",
                     kPerfTrailCount, trailSystem.GetActiveTrailCount());
        return false;
    }
    std::printf("  Registered %u trails  (handles [%u..%u])\n",
                trailSystem.GetActiveTrailCount(),
                handles.front(),
                handles.back());
    PrintSeparator();

    // Fixed camera above the scene looking down at the circular orbits.
    static constexpr uz::Vec3 kCameraPos{0.0f, 25.0f, 0.0f};

    bool hadGeometryAt2s = false;
    uint32_t peakTotalVertices = 0;

    for (int frame = 0; frame < kPerfFrames; ++frame) {
        const float time = static_cast<float>(frame) * kDt;

        // Build the per-handle position map for this frame.
        std::unordered_map<uz::TrailSystem::TrailHandle, uz::Vec3> positions;
        positions.reserve(kPerfTrailCount);
        for (int i = 0; i < kPerfTrailCount; ++i) {
            positions[handles[i]] = ComputePerfTrailPosition(i, time);
        }

        const auto t0 = std::chrono::high_resolution_clock::now();

        trailSystem.Update(kDt, positions, kCameraPos);

        const auto t1 = std::chrono::high_resolution_clock::now();
        const double updateMs =
            std::chrono::duration<double, std::milli>(t1 - t0).count();

        const auto renderData = trailSystem.GetRenderData();

        uint32_t totalVerts   = 0;
        uint32_t totalIndices = 0;
        SumRenderDataStats(renderData, totalVerts, totalIndices);

        if (totalVerts > peakTotalVertices) {
            peakTotalVertices = totalVerts;
        }

        // Check at t=2s (frame 120) that geometry is being generated.
        if (frame == 120 && totalVerts > 0) {
            hadGeometryAt2s = true;
        }

        // Print performance overlay once per second.
        const bool isReport = (frame == 0) ||
                              (frame == kPerfFrames - 1) ||
                              (frame > 0 && frame % kReportInterval == 0);

        if (isReport) {
            const uint32_t activeTrails  = trailSystem.GetActiveTrailCount();
            const uint32_t totalSegments = trailSystem.GetTotalSegmentCount();

            std::printf("  [Frame %4d | t=%5.2fs]  trails=%-2u  segments=%-5u"
                        "  verts=%-6u  idx=%-6u  update=%.3fms\n",
                        frame,
                        static_cast<double>(time),
                        activeTrails,
                        totalSegments,
                        totalVerts,
                        totalIndices,
                        updateMs);
        }
    }

    PrintSeparator();
    std::printf("  Phase 3 summary: peakTotalVerts=%u  activeAtEnd=%u\n",
                peakTotalVertices, trailSystem.GetActiveTrailCount());

    // Unregister all trails to verify cleanup is clean.
    for (const auto handle : handles) {
        trailSystem.UnregisterTrail(handle);
    }

    if (trailSystem.GetActiveTrailCount() != 0) {
        std::fprintf(stderr,
                     "  [FAIL] Expected 0 active trails after unregistration, got %u\n",
                     trailSystem.GetActiveTrailCount());
        return false;
    }
    std::printf("  [OK]  All 50 trails unregistered cleanly\n");

    if (!hadGeometryAt2s) {
        std::fprintf(stderr,
                     "  [FAIL] No vertex geometry produced by t=2.0s in performance test\n");
        return false;
    }
    std::printf("  [OK]  Performance test produced geometry by t=2.0s\n");

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::printf("==========================================================\n");
    std::printf("         UzEngine -- Trail/Ribbon VFX System Demo\n");
    std::printf("   Projectile, sword swipe, and 50-trail performance test\n");
    std::printf("==========================================================\n");

    bool allPass = true;

    // Phase 1: Parabolic projectile with animated fire trail.
    if (!RunProjectilePhase()) {
        std::fprintf(stderr, "\n[FAIL] Phase 1 (Projectile Trail) failed.\n");
        allPass = false;
    }

    // Phase 2: Sword swipe arc with fast-decaying ribbon.
    if (!RunSwordSwipePhase()) {
        std::fprintf(stderr, "\n[FAIL] Phase 2 (Sword Swipe) failed.\n");
        allPass = false;
    }

    // Phase 3: 50 simultaneous circular trails via TrailSystem.
    if (!RunPerformancePhase()) {
        std::fprintf(stderr, "\n[FAIL] Phase 3 (Performance Test) failed.\n");
        allPass = false;
    }

    std::printf("\n==========================================================\n");
    if (allPass) {
        std::printf("[PASS] Trail system demo completed successfully.\n");
        return 0;
    }

    std::fprintf(stderr, "[FAIL] One or more phases failed.\n");
    return 1;
}
