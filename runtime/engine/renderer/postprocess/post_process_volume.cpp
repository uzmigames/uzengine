/// UzEngine PostProcessVolume — implementation.
///
/// @source UE5 PostProcessing.cpp — OverridePostProcessSettings
/// @source UE5 Scene.cpp — FScene::AddPostProcessVolume

#include "post_process_volume.h"

#include <algorithm>
#include <cmath>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// PostProcessSettings::Lerp
// ─────────────────────────────────────────────────────────────────────────────

void PostProcessSettings::Lerp(const PostProcessSettings& other, float alpha) noexcept {
    // Clamp alpha to [0, 1] for safety.
    alpha = std::clamp(alpha, 0.0f, 1.0f);

    if (alpha <= 0.0f) {
        return;  // No blending needed.
    }

    const float oneMinusAlpha = 1.0f - alpha;

    // Helper: lerp a single float field.
    auto lerpFloat = [oneMinusAlpha, alpha](float& dst, float src) {
        dst = dst * oneMinusAlpha + src * alpha;
    };

    // Helper: snap an int field to src when alpha crosses 0.5.
    auto snapInt = [alpha](int& dst, int src) {
        if (alpha > 0.5f) {
            dst = src;
        }
    };

    // ── Bloom ───────────────────────────────────────────────────────
    lerpFloat(bloomIntensity, other.bloomIntensity);
    lerpFloat(bloomThreshold, other.bloomThreshold);

    // ── Auto-Exposure ───────────────────────────────────────────────
    lerpFloat(autoExposureMinBrightness, other.autoExposureMinBrightness);
    lerpFloat(autoExposureMaxBrightness, other.autoExposureMaxBrightness);
    lerpFloat(autoExposureBias, other.autoExposureBias);
    lerpFloat(autoExposureSpeedUp, other.autoExposureSpeedUp);
    lerpFloat(autoExposureSpeedDown, other.autoExposureSpeedDown);

    // ── Depth of Field ──────────────────────────────────────────────
    lerpFloat(depthOfFieldFocalDistance, other.depthOfFieldFocalDistance);
    lerpFloat(depthOfFieldFocalRegion, other.depthOfFieldFocalRegion);
    lerpFloat(depthOfFieldNearTransition, other.depthOfFieldNearTransition);
    lerpFloat(depthOfFieldFarTransition, other.depthOfFieldFarTransition);
    lerpFloat(depthOfFieldFstop, other.depthOfFieldFstop);

    // ── Motion Blur ─────────────────────────────────────────────────
    lerpFloat(motionBlurAmount, other.motionBlurAmount);
    lerpFloat(motionBlurMax, other.motionBlurMax);
    snapInt(motionBlurTargetFPS, other.motionBlurTargetFPS);

    // ── Color Grading ───────────────────────────────────────────────
    lerpFloat(whiteTemp, other.whiteTemp);
    lerpFloat(whiteTint, other.whiteTint);
    lerpFloat(filmSlope, other.filmSlope);
    lerpFloat(filmToe, other.filmToe);
    lerpFloat(filmShoulder, other.filmShoulder);
    lerpFloat(filmBlackClip, other.filmBlackClip);
    lerpFloat(filmWhiteClip, other.filmWhiteClip);

    // ── Vignette ────────────────────────────────────────────────────
    lerpFloat(vignetteIntensity, other.vignetteIntensity);

    // ── SSAO ────────────────────────────────────────────────────────
    lerpFloat(ambientOcclusionIntensity, other.ambientOcclusionIntensity);
    lerpFloat(ambientOcclusionRadius, other.ambientOcclusionRadius);
    lerpFloat(ambientOcclusionPower, other.ambientOcclusionPower);

    // ── SSR ─────────────────────────────────────────────────────────
    lerpFloat(screenSpaceReflectionIntensity, other.screenSpaceReflectionIntensity);
    lerpFloat(screenSpaceReflectionQuality, other.screenSpaceReflectionQuality);

    // ── Override flags ──────────────────────────────────────────────
    // OR the flags: once a field is marked overridden, it stays overridden.
    overrideFlags |= other.overrideFlags;
}

// ─────────────────────────────────────────────────────────────────────────────
// PostProcessVolumeManager
// ─────────────────────────────────────────────────────────────────────────────

uint32_t PostProcessVolumeManager::AddVolume(const PostProcessVolume& volume) {
    const uint32_t id = m_nextId++;
    m_volumes.push_back({id, volume});
    return id;
}

void PostProcessVolumeManager::RemoveVolume(uint32_t id) {
    auto it = std::find_if(m_volumes.begin(), m_volumes.end(),
        [id](const VolumeEntry& entry) { return entry.id == id; });

    if (it != m_volumes.end()) {
        // Swap-and-pop for O(1) removal. Order does not matter because
        // ComputeBlendedSettings sorts by priority each frame.
        if (it != m_volumes.end() - 1) {
            *it = std::move(m_volumes.back());
        }
        m_volumes.pop_back();
    }
}

void PostProcessVolumeManager::Clear() {
    m_volumes.clear();
}

PostProcessVolume* PostProcessVolumeManager::FindVolume(uint32_t id) {
    auto it = std::find_if(m_volumes.begin(), m_volumes.end(),
        [id](const VolumeEntry& entry) { return entry.id == id; });
    return (it != m_volumes.end()) ? &it->volume : nullptr;
}

const PostProcessVolume* PostProcessVolumeManager::FindVolume(uint32_t id) const {
    auto it = std::find_if(m_volumes.begin(), m_volumes.end(),
        [id](const VolumeEntry& entry) { return entry.id == id; });
    return (it != m_volumes.end()) ? &it->volume : nullptr;
}

float PostProcessVolumeManager::DistanceToAABB(
    const Vec3& point,
    const Vec3& boundsMin,
    const Vec3& boundsMax) noexcept
{
    // For each axis, compute how far the point is outside the AABB.
    // If inside on that axis, the contribution is 0.
    const float dx = std::max(0.0f, std::max(boundsMin.x - point.x, point.x - boundsMax.x));
    const float dy = std::max(0.0f, std::max(boundsMin.y - point.y, point.y - boundsMax.y));
    const float dz = std::max(0.0f, std::max(boundsMin.z - point.z, point.z - boundsMax.z));

    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

PostProcessSettings PostProcessVolumeManager::ComputeBlendedSettings(
    const Vec3& cameraPosition) const
{
    // Start with engine defaults.
    PostProcessSettings result;

    if (m_volumes.empty()) {
        return result;
    }

    // Build a sorted list by priority (ascending). The highest-priority volume
    // is applied last and therefore "wins" on contested fields.
    // We copy the entries because this is a const method and we need to sort.
    //
    // @source UE5 PostProcessing.cpp — volumes sorted by priority, applied in order
    auto sorted = m_volumes;
    std::sort(sorted.begin(), sorted.end(),
        [](const VolumeEntry& a, const VolumeEntry& b) {
            return a.volume.priority < b.volume.priority;
        });

    for (const auto& entry : sorted) {
        const auto& vol = entry.volume;

        // Skip disabled volumes.
        if (!vol.enabled) {
            continue;
        }

        float blendFactor = vol.blendWeight;

        if (!vol.isUnbound) {
            // Compute unsigned distance from camera to AABB surface.
            const float dist = DistanceToAABB(cameraPosition, vol.boundsMin, vol.boundsMax);

            if (vol.blendRadius > 0.0f) {
                // Smooth fade: fully on at distance 0, fully off at blendRadius.
                blendFactor *= std::clamp(1.0f - dist / vol.blendRadius, 0.0f, 1.0f);
            } else {
                // Hard cut: only applies when the camera is inside the AABB.
                blendFactor *= (dist <= 0.0f) ? 1.0f : 0.0f;
            }
        }

        // Only blend if this volume contributes any weight.
        if (blendFactor > 0.0f) {
            result.Lerp(vol.settings, blendFactor);
        }
    }

    return result;
}

} // namespace uz
