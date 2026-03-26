/// VFX LOD system implementation — distance-based and screen-coverage-based
/// particle quality tier selection.

#include "vfx_lod.h"

#include "engine/logging/log.h"

#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Log category
// ─────────────────────────────────────────────────────────────────────────────

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// VFXLodLevelToString
// ─────────────────────────────────────────────────────────────────────────────

const char* VFXLodLevelToString(VFXLodLevel level) noexcept {
    switch (level) {
    case VFXLodLevel::Full:    return "Full";
    case VFXLodLevel::Reduced: return "Reduced";
    case VFXLodLevel::Minimal: return "Minimal";
    case VFXLodLevel::Culled:  return "Culled";
    }
    return "Unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXLodLevelToSpawnFactor
// ─────────────────────────────────────────────────────────────────────────────

float VFXLodLevelToSpawnFactor(VFXLodLevel level) noexcept {
    switch (level) {
    case VFXLodLevel::Full:    return 1.00f;
    case VFXLodLevel::Reduced: return 0.50f;
    case VFXLodLevel::Minimal: return 0.25f;
    case VFXLodLevel::Culled:  return 0.00f;
    }
    // Unknown tier — treat as culled to be safe.
    return 0.00f;
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXLodSettings::IsValid
// ─────────────────────────────────────────────────────────────────────────────

bool VFXLodSettings::IsValid() const noexcept {
    // All three thresholds must be positive and strictly increasing so that
    // the four distance bands are non-degenerate and unambiguous.
    return distances[0] > 0.0f
        && distances[0] < distances[1]
        && distances[1] < distances[2];
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXScreenCuller::ComputeScreenPixels
// ─────────────────────────────────────────────────────────────────────────────

float VFXScreenCuller::ComputeScreenPixels(
    float boundingRadius,
    float distance,
    float screenHeight
) noexcept {
    if (boundingRadius <= 0.0f || screenHeight <= 0.0f) {
        return 0.0f;
    }

    // Clamp to a small positive value to guard against zero/negative distances
    // (emitter coincident with camera, or behind the near plane).
    const float safeDist = std::max(distance, 0.001f);

    // Approximate projected sphere diameter in pixels.
    // The 0.5 factor converts from full screen height to half-height, which
    // corresponds to the vertical tan(half_fov) scaling in the projection.
    return (boundingRadius / safeDist) * screenHeight * 0.5f;
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXScreenCuller::ShouldCull
// ─────────────────────────────────────────────────────────────────────────────

bool VFXScreenCuller::ShouldCull(
    float boundingRadius,
    float distance,
    float screenHeight,
    float minScreenPixels
) noexcept {
    return ComputeScreenPixels(boundingRadius, distance, screenHeight) < minScreenPixels;
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXLodEvaluator::ComputeFromDistance
// ─────────────────────────────────────────────────────────────────────────────

VFXLodLevel VFXLodEvaluator::ComputeFromDistance(
    const Vec3& emitterPos,
    const Vec3& cameraPos,
    const VFXLodSettings& settings
) noexcept {
    // Use squared distances to avoid a sqrt per emitter per frame.
    const Vec3 delta = emitterPos - cameraPos;
    const float distSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;

    const float d0Sq = settings.distances[0] * settings.distances[0];
    const float d1Sq = settings.distances[1] * settings.distances[1];
    const float d2Sq = settings.distances[2] * settings.distances[2];

    if (distSq < d0Sq) { return VFXLodLevel::Full;    }
    if (distSq < d1Sq) { return VFXLodLevel::Reduced; }
    if (distSq < d2Sq) { return VFXLodLevel::Minimal; }
    return VFXLodLevel::Culled;
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXLodEvaluator::ComputeFromScreenSize
// ─────────────────────────────────────────────────────────────────────────────

VFXLodLevel VFXLodEvaluator::ComputeFromScreenSize(
    float boundingRadius,
    float distance,
    float screenHeight,
    float minScreenPixels
) noexcept {
    // Screen-size criterion is binary: either the emitter is large enough to
    // warrant any simulation, or it isn't. Finer quality tiers are determined
    // by the distance criterion in Evaluate().
    if (VFXScreenCuller::ShouldCull(boundingRadius, distance, screenHeight, minScreenPixels)) {
        return VFXLodLevel::Culled;
    }
    return VFXLodLevel::Full;
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXLodEvaluator::Evaluate
// ─────────────────────────────────────────────────────────────────────────────

VFXLodLevel VFXLodEvaluator::Evaluate(
    const Vec3& emitterPos,
    const Vec3& cameraPos,
    float boundingRadius,
    float screenHeight,
    const VFXLodSettings& settings,
    float minScreenPixels
) noexcept {
    const VFXLodLevel distanceLod = ComputeFromDistance(emitterPos, cameraPos, settings);

    // Compute the camera-to-emitter distance once for the screen-size check
    // rather than repeating the sqrt inside ComputeScreenPixels.
    const Vec3 delta = emitterPos - cameraPos;
    const float distance = std::sqrt(
        delta.x * delta.x + delta.y * delta.y + delta.z * delta.z
    );

    const VFXLodLevel screenLod = ComputeFromScreenSize(
        boundingRadius, distance, screenHeight, minScreenPixels
    );

    // The more restrictive (numerically higher) tier wins: an emitter that is
    // far away but large on screen still gets penalised for distance, and an
    // emitter that is close but tiny (e.g., sub-pixel dust) gets culled.
    return static_cast<VFXLodLevel>(
        std::max(
            static_cast<uint8_t>(distanceLod),
            static_cast<uint8_t>(screenLod)
        )
    );
}

} // namespace uz
