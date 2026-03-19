/// UzEngine PostProcessVolume — spatial post-process parameter blending.
///
/// Implements UE5-style PostProcessVolume system where multiple AABB volumes
/// overlap and blend post-process settings based on camera position, priority,
/// and blend radius.
///
/// @source UE5 PostProcessVolume.h — APostProcessVolume
/// @source UE5 Scene.cpp — FScene::AddPostProcessVolume
/// @source UE5 PostProcessing.cpp — GetFinalPostProcessSettings

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "vector3.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Override flag bits — one per setting field
// ─────────────────────────────────────────────────────────────────────────────

/// Bit indices for PostProcessSettings::overrideFlags.
/// When a bit is set, the corresponding field actively overrides lower-priority volumes.
/// When clear, the field is ignored during blending (pass-through from lower priority).
enum class PostProcessOverride : uint32_t {
    BloomIntensity                  = 0,
    BloomThreshold                  = 1,
    AutoExposureMinBrightness       = 2,
    AutoExposureMaxBrightness       = 3,
    AutoExposureBias                = 4,
    AutoExposureSpeedUp             = 5,
    AutoExposureSpeedDown           = 6,
    DepthOfFieldFocalDistance       = 7,
    DepthOfFieldFocalRegion         = 8,
    DepthOfFieldNearTransition      = 9,
    DepthOfFieldFarTransition       = 10,
    DepthOfFieldFstop               = 11,
    MotionBlurAmount                = 12,
    MotionBlurMax                   = 13,
    MotionBlurTargetFPS             = 14,
    WhiteTemp                       = 15,
    WhiteTint                       = 16,
    FilmSlope                       = 17,
    FilmToe                         = 18,
    FilmShoulder                    = 19,
    FilmBlackClip                   = 20,
    FilmWhiteClip                   = 21,
    VignetteIntensity               = 22,
    AmbientOcclusionIntensity       = 23,
    AmbientOcclusionRadius          = 24,
    AmbientOcclusionPower           = 25,
    ScreenSpaceReflectionIntensity  = 26,
    ScreenSpaceReflectionQuality    = 27,

    Count                           = 28,
};

// ─────────────────────────────────────────────────────────────────────────────
// PostProcessSettings — all post-process effect parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Contains all post-process effect parameters with defaults matching UE5.
///
/// Each field has a corresponding bit in `overrideFlags` (see PostProcessOverride).
/// During volume blending, only fields with their override bit set participate
/// in the lerp. Fields without the bit are passed through unchanged.
///
/// @source UE5 Scene.cpp — FPostProcessSettings defaults
struct PostProcessSettings {
    // ── Bloom ───────────────────────────────────────────────────────
    /// Overall bloom intensity multiplier.
    /// @source UE5 Scene.cpp:508 — BloomIntensity default 0.675
    float bloomIntensity = 0.675f;

    /// Bloom brightness threshold. Pixels below this luminance do not bloom.
    /// -1.0 means use the engine default threshold.
    /// @source UE5 Scene.cpp:509 — BloomThreshold default -1.0
    float bloomThreshold = -1.0f;

    // ── Auto-Exposure ───────────────────────────────────────────────
    /// Minimum average scene brightness for auto-exposure adaptation.
    /// @source UE5 Scene.cpp:196 — AutoExposureMinBrightness default 0.03
    float autoExposureMinBrightness = 0.03f;

    /// Maximum average scene brightness for auto-exposure adaptation.
    /// @source UE5 Scene.cpp:197 — AutoExposureMaxBrightness default 8.0
    float autoExposureMaxBrightness = 8.0f;

    /// Logarithmic exposure bias. 0 = no adjustment, 1 = 2x brighter, -1 = 2x darker.
    /// @source UE5 Scene.cpp:215 — AutoExposureBias default 0.0
    float autoExposureBias = 0.0f;

    /// Speed at which exposure adapts when the scene gets brighter (EV/s).
    /// @source UE5 Scene.cpp:213 — AutoExposureSpeedUp default 3.0
    float autoExposureSpeedUp = 3.0f;

    /// Speed at which exposure adapts when the scene gets darker (EV/s).
    /// @source UE5 Scene.cpp:214 — AutoExposureSpeedDown default 1.0
    float autoExposureSpeedDown = 1.0f;

    // ── Depth of Field ──────────────────────────────────────────────
    /// Distance to the focal plane in centimeters. 0 = DoF disabled.
    /// @source UE5 Scene.cpp:559 — DepthOfFieldFocalDistance default 0.0
    float depthOfFieldFocalDistance = 0.0f;

    /// Width of the in-focus region around the focal plane (cm).
    /// @source UE5 Scene.cpp:579 — DepthOfFieldFocalRegion default 0.0
    float depthOfFieldFocalRegion = 0.0f;

    /// Width of the near blur transition region (cm).
    /// @source UE5 Scene.cpp:580 — DepthOfFieldNearTransition default 300.0
    float depthOfFieldNearTransition = 300.0f;

    /// Width of the far blur transition region (cm).
    /// @source UE5 Scene.cpp:581 — DepthOfFieldFarTransition default 500.0
    float depthOfFieldFarTransition = 500.0f;

    /// Simulated camera f-stop for cinematic DoF aperture size.
    /// @source UE5 Scene.cpp:560 — DepthOfFieldFstop default 4.0
    float depthOfFieldFstop = 4.0f;

    // ── Motion Blur ─────────────────────────────────────────────────
    /// Strength of motion blur. 0 = disabled, 1 = full.
    /// @source UE5 Scene.cpp:598 — MotionBlurAmount default 0.5
    float motionBlurAmount = 0.5f;

    /// Maximum blur distance in percent of screen width.
    /// @source UE5 Scene.cpp:599 — MotionBlurMax default 5.0
    float motionBlurMax = 5.0f;

    /// Target framerate for motion blur scaling. Lower = more blur per velocity.
    /// @source UE5 Scene.cpp:600 — MotionBlurTargetFPS default 30
    int motionBlurTargetFPS = 30;

    // ── Color Grading ───────────────────────────────────────────────
    /// White balance color temperature in Kelvin.
    /// @source UE5 Scene.cpp:403 — WhiteTemp default 6500.0
    float whiteTemp = 6500.0f;

    /// White balance tint (green-magenta shift). 0 = neutral.
    /// @source UE5 Scene.cpp:404 — WhiteTint default 0.0
    float whiteTint = 0.0f;

    /// ACES film tonemap slope (contrast in mid-tones).
    /// @source UE5 Scene.cpp:478 — FilmSlope default 0.88
    float filmSlope = 0.88f;

    /// ACES film tonemap toe (dark region rolloff).
    /// @source UE5 Scene.cpp:479 — FilmToe default 0.55
    float filmToe = 0.55f;

    /// ACES film tonemap shoulder (highlight compression).
    /// @source UE5 Scene.cpp:480 — FilmShoulder default 0.26
    float filmShoulder = 0.26f;

    /// ACES film tonemap black clip (crush blacks below this).
    /// @source UE5 Scene.cpp:481 — FilmBlackClip default 0.0
    float filmBlackClip = 0.0f;

    /// ACES film tonemap white clip (clip whites above this).
    /// @source UE5 Scene.cpp:482 — FilmWhiteClip default 0.04
    float filmWhiteClip = 0.04f;

    // ── Vignette ────────────────────────────────────────────────────
    /// Screen-edge darkening intensity. 0 = disabled.
    /// @source UE5 Scene.cpp:506 — VignetteIntensity default 0.4
    float vignetteIntensity = 0.4f;

    // ── SSAO ────────────────────────────────────────────────────────
    /// Intensity of ambient occlusion darkening.
    /// @source UE5 Scene.cpp:500 — AmbientOcclusionIntensity default 1.0
    float ambientOcclusionIntensity = 1.0f;

    /// Sampling radius for ambient occlusion in centimeters.
    /// @source UE5 Scene.cpp:501 — AmbientOcclusionRadius default 200.0
    float ambientOcclusionRadius = 200.0f;

    /// Power curve applied to AO result (higher = more contrast).
    /// @source UE5 Scene.cpp:503 — AmbientOcclusionPower default 2.0
    float ambientOcclusionPower = 2.0f;

    // ── SSR ─────────────────────────────────────────────────────────
    /// Intensity of screen-space reflections (0-100 percent scale).
    /// @source UE5 Scene.cpp:491 — ScreenSpaceReflectionIntensity default 100.0
    float screenSpaceReflectionIntensity = 100.0f;

    /// Quality of screen-space reflections (0-100 percent scale).
    /// @source UE5 Scene.cpp:493 — ScreenSpaceReflectionQuality default 50.0
    float screenSpaceReflectionQuality = 50.0f;

    // ── Override flags ──────────────────────────────────────────────
    /// Bitmask of PostProcessOverride values.
    /// When a bit is set, the corresponding field is actively contributed by this
    /// volume during blending. When clear, the field is pass-through.
    uint32_t overrideFlags = 0;

    /// Sets an override flag for a specific setting.
    void SetOverride(PostProcessOverride flag) noexcept {
        overrideFlags |= (1u << static_cast<uint32_t>(flag));
    }

    /// Clears an override flag for a specific setting.
    void ClearOverride(PostProcessOverride flag) noexcept {
        overrideFlags &= ~(1u << static_cast<uint32_t>(flag));
    }

    /// Returns true if the given override flag is set.
    [[nodiscard]] bool HasOverride(PostProcessOverride flag) const noexcept {
        return (overrideFlags & (1u << static_cast<uint32_t>(flag))) != 0;
    }

    /// Sets all override flags (all fields will participate in blending).
    void SetAllOverrides() noexcept {
        overrideFlags = (1u << static_cast<uint32_t>(PostProcessOverride::Count)) - 1;
    }

    /// Clears all override flags.
    void ClearAllOverrides() noexcept {
        overrideFlags = 0;
    }

    /// Linearly interpolates all numeric fields from `*this` toward `other` by `alpha`.
    ///
    /// Float fields are lerped: `field = field * (1 - alpha) + other.field * alpha`.
    /// Integer fields snap to `other` when `alpha > 0.5`.
    /// Override flags are NOT lerped -- they are OR'd (any active override stays active).
    ///
    /// @source UE5 PostProcessing.cpp — OverridePostProcessSettings lerp logic
    ///
    /// # Arguments
    /// * `other` -- Source settings to blend toward.
    /// * `alpha` -- Blend factor in [0, 1]. 0 = no change, 1 = fully `other`.
    void Lerp(const PostProcessSettings& other, float alpha) noexcept;
};

// ─────────────────────────────────────────────────────────────────────────────
// PostProcessVolume — spatial volume with blending parameters
// ─────────────────────────────────────────────────────────────────────────────

/// An axis-aligned box in world space that applies PostProcessSettings to
/// cameras within or near it. Multiple volumes blend based on priority,
/// distance, and weight.
///
/// @source UE5 PostProcessVolume.h — APostProcessVolume
struct PostProcessVolume {
    // ── Spatial bounds ──────────────────────────────────────────────
    /// AABB minimum corner in world space (centimeters).
    Vec3 boundsMin;

    /// AABB maximum corner in world space (centimeters).
    Vec3 boundsMax;

    /// When true, this volume has no spatial bounds -- it applies everywhere.
    /// Useful for a global "base" post-process layer.
    bool isUnbound = false;

    // ── Blending ────────────────────────────────────────────────────
    /// Volumes with higher priority are applied later and override lower ones.
    /// Volumes with equal priority blend in insertion order (undefined in UE5).
    float priority = 0.0f;

    /// Distance in centimeters over which the volume fades in as the camera
    /// approaches from outside. 0 = hard cut (fully on when inside, fully off when outside).
    /// @source UE5 PostProcessVolume.h — BlendRadius
    float blendRadius = 100.0f;

    /// Overall contribution weight multiplier in [0, 1].
    /// 0 = this volume has no effect. 1 = full contribution.
    /// @source UE5 PostProcessVolume.h — BlendWeight
    float blendWeight = 1.0f;

    /// When false, this volume is skipped during blending.
    bool enabled = true;

    // ── Settings ────────────────────────────────────────────────────
    /// The post-process settings this volume contributes.
    PostProcessSettings settings;
};

// ─────────────────────────────────────────────────────────────────────────────
// PostProcessVolumeManager — runtime volume registry and blender
// ─────────────────────────────────────────────────────────────────────────────

/// Manages a collection of PostProcessVolumes and blends their settings
/// for a given camera position. Volumes are sorted by priority (ascending)
/// so that the highest-priority volume has final say.
///
/// This is a frame-local helper -- it is NOT thread-safe. Call from the
/// game thread or under external synchronization.
///
/// @source UE5 PostProcessing.cpp — GetFinalPostProcessSettings
/// @source UE5 Scene.cpp — FScene::AddPostProcessVolume
class PostProcessVolumeManager {
public:
    PostProcessVolumeManager() = default;
    ~PostProcessVolumeManager() = default;

    // Non-copyable, movable.
    PostProcessVolumeManager(const PostProcessVolumeManager&) = delete;
    PostProcessVolumeManager& operator=(const PostProcessVolumeManager&) = delete;
    PostProcessVolumeManager(PostProcessVolumeManager&&) noexcept = default;
    PostProcessVolumeManager& operator=(PostProcessVolumeManager&&) noexcept = default;

    /// Adds a volume to the manager.
    ///
    /// # Arguments
    /// * `volume` -- The volume to add. Copied into internal storage.
    ///
    /// # Returns
    /// A unique non-zero ID that can be used to remove the volume later.
    /// IDs are monotonically increasing and never reused.
    [[nodiscard]] uint32_t AddVolume(const PostProcessVolume& volume);

    /// Removes a previously added volume by its ID.
    ///
    /// If the ID is not found, this is a no-op.
    ///
    /// # Arguments
    /// * `id` -- The ID returned by AddVolume().
    void RemoveVolume(uint32_t id);

    /// Removes all volumes from the manager.
    void Clear();

    /// Computes the final blended PostProcessSettings for a camera at `cameraPosition`.
    ///
    /// Algorithm (matches UE5 OverridePostProcessSettings):
    /// 1. Start with default PostProcessSettings (all fields at engine defaults).
    /// 2. Collect all enabled volumes.
    /// 3. Sort by priority ascending (lowest priority first, highest last).
    /// 4. For each volume:
    ///    a. If unbound: blend factor = blendWeight.
    ///    b. If bounded: compute unsigned distance from camera to AABB surface.
    ///       - Inside AABB: distance = 0, blend factor = blendWeight.
    ///       - Outside AABB with blendRadius > 0:
    ///         blend factor = saturate(1 - distance / blendRadius) * blendWeight.
    ///       - Outside AABB with blendRadius == 0: blend factor = 0 (hard cut).
    ///    c. If blend factor > 0, lerp current result toward this volume's settings.
    /// 5. Return the final blended result.
    ///
    /// @source UE5 PostProcessing.cpp — OverridePostProcessSettings
    ///
    /// # Arguments
    /// * `cameraPosition` -- Camera world-space position (centimeters).
    ///
    /// # Returns
    /// Blended settings representing the combined effect of all active volumes.
    [[nodiscard]] PostProcessSettings ComputeBlendedSettings(const Vec3& cameraPosition) const;

    /// Returns the number of registered volumes (including disabled ones).
    [[nodiscard]] uint32_t GetVolumeCount() const noexcept {
        return static_cast<uint32_t>(m_volumes.size());
    }

    /// Returns a pointer to a volume's data by ID, or nullptr if not found.
    /// The pointer is invalidated by AddVolume(), RemoveVolume(), or Clear().
    [[nodiscard]] PostProcessVolume* FindVolume(uint32_t id);

    /// Const overload.
    [[nodiscard]] const PostProcessVolume* FindVolume(uint32_t id) const;

private:
    /// Computes the unsigned distance from a point to the surface of an AABB.
    /// Returns 0 if the point is inside the AABB.
    [[nodiscard]] static float DistanceToAABB(
        const Vec3& point,
        const Vec3& boundsMin,
        const Vec3& boundsMax) noexcept;

    struct VolumeEntry {
        uint32_t id;
        PostProcessVolume volume;
    };

    std::vector<VolumeEntry> m_volumes;
    uint32_t m_nextId = 1;
};

} // namespace uz
