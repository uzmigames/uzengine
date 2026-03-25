/// UzEngine PostProcessVolume — spatial post-process parameter blending.
///
/// Implements PostProcessVolume system where multiple AABB volumes
/// overlap and blend post-process settings based on camera position, priority,
/// and blend radius.
///

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
    BloomIntensity                  = 0,  ///< Overall bloom intensity multiplier.
    BloomThreshold                  = 1,  ///< Luminance threshold below which pixels do not bloom.
    AutoExposureMinBrightness       = 2,  ///< Minimum average scene brightness for auto-exposure.
    AutoExposureMaxBrightness       = 3,  ///< Maximum average scene brightness for auto-exposure.
    AutoExposureBias                = 4,  ///< Logarithmic exposure bias (EV offset).
    AutoExposureSpeedUp             = 5,  ///< Adaptation speed when scene brightens (EV/s).
    AutoExposureSpeedDown           = 6,  ///< Adaptation speed when scene darkens (EV/s).
    DepthOfFieldFocalDistance       = 7,  ///< Distance to the focal plane (cm).
    DepthOfFieldFocalRegion         = 8,  ///< Width of the in-focus region (cm).
    DepthOfFieldNearTransition      = 9,  ///< Near blur transition width (cm).
    DepthOfFieldFarTransition       = 10, ///< Far blur transition width (cm).
    DepthOfFieldFstop               = 11, ///< Simulated camera f-stop aperture size.
    MotionBlurAmount                = 12, ///< Motion blur strength (0 = off, 1 = full).
    MotionBlurMax                   = 13, ///< Maximum blur distance (% of screen width).
    MotionBlurTargetFPS             = 14, ///< Target framerate for motion blur scaling.
    WhiteTemp                       = 15, ///< White balance color temperature (Kelvin).
    WhiteTint                       = 16, ///< White balance green-magenta tint shift.
    FilmSlope                       = 17, ///< ACES tonemap slope (mid-tone contrast).
    FilmToe                         = 18, ///< ACES tonemap toe (dark region rolloff).
    FilmShoulder                    = 19, ///< ACES tonemap shoulder (highlight compression).
    FilmBlackClip                   = 20, ///< ACES tonemap black clip (crush blacks below this).
    FilmWhiteClip                   = 21, ///< ACES tonemap white clip (clip whites above this).
    VignetteIntensity               = 22, ///< Screen-edge darkening intensity.
    AmbientOcclusionIntensity       = 23, ///< SSAO darkening intensity.
    AmbientOcclusionRadius          = 24, ///< SSAO sampling radius (cm).
    AmbientOcclusionPower           = 25, ///< SSAO power curve (contrast).
    ScreenSpaceReflectionIntensity  = 26, ///< SSR reflection intensity (0-100%).
    ScreenSpaceReflectionQuality    = 27, ///< SSR quality level (0-100%).

    Count                           = 28, ///< Total number of override flags.
};

// ─────────────────────────────────────────────────────────────────────────────
// PostProcessSettings — all post-process effect parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Contains all post-process effect parameters with standard defaults.
///
/// Each field has a corresponding bit in `overrideFlags` (see PostProcessOverride).
/// During volume blending, only fields with their override bit set participate
/// in the lerp. Fields without the bit are passed through unchanged.
///
struct PostProcessSettings {
    // ── Bloom ───────────────────────────────────────────────────────
    /// Overall bloom intensity multiplier.
    float bloomIntensity = 0.675f;

    /// Bloom brightness threshold. Pixels below this luminance do not bloom.
    /// -1.0 means use the engine default threshold.
    float bloomThreshold = -1.0f;

    // ── Auto-Exposure ───────────────────────────────────────────────
    /// Minimum average scene brightness for auto-exposure adaptation.
    float autoExposureMinBrightness = 0.03f;

    /// Maximum average scene brightness for auto-exposure adaptation.
    float autoExposureMaxBrightness = 8.0f;

    /// Logarithmic exposure bias. 0 = no adjustment, 1 = 2x brighter, -1 = 2x darker.
    float autoExposureBias = 0.0f;

    /// Speed at which exposure adapts when the scene gets brighter (EV/s).
    float autoExposureSpeedUp = 3.0f;

    /// Speed at which exposure adapts when the scene gets darker (EV/s).
    float autoExposureSpeedDown = 1.0f;

    // ── Depth of Field ──────────────────────────────────────────────
    /// Distance to the focal plane in centimeters. 0 = DoF disabled.
    float depthOfFieldFocalDistance = 0.0f;

    /// Width of the in-focus region around the focal plane (cm).
    float depthOfFieldFocalRegion = 0.0f;

    /// Width of the near blur transition region (cm).
    float depthOfFieldNearTransition = 300.0f;

    /// Width of the far blur transition region (cm).
    float depthOfFieldFarTransition = 500.0f;

    /// Simulated camera f-stop for cinematic DoF aperture size.
    float depthOfFieldFstop = 4.0f;

    // ── Motion Blur ─────────────────────────────────────────────────
    /// Strength of motion blur. 0 = disabled, 1 = full.
    float motionBlurAmount = 0.5f;

    /// Maximum blur distance in percent of screen width.
    float motionBlurMax = 5.0f;

    /// Target framerate for motion blur scaling. Lower = more blur per velocity.
    int motionBlurTargetFPS = 30;

    // ── Color Grading ───────────────────────────────────────────────
    /// White balance color temperature in Kelvin.
    float whiteTemp = 6500.0f;

    /// White balance tint (green-magenta shift). 0 = neutral.
    float whiteTint = 0.0f;

    /// ACES film tonemap slope (contrast in mid-tones).
    float filmSlope = 0.88f;

    /// ACES film tonemap toe (dark region rolloff).
    float filmToe = 0.55f;

    /// ACES film tonemap shoulder (highlight compression).
    float filmShoulder = 0.26f;

    /// ACES film tonemap black clip (crush blacks below this).
    float filmBlackClip = 0.0f;

    /// ACES film tonemap white clip (clip whites above this).
    float filmWhiteClip = 0.04f;

    // ── Vignette ────────────────────────────────────────────────────
    /// Screen-edge darkening intensity. 0 = disabled.
    float vignetteIntensity = 0.4f;

    // ── SSAO ────────────────────────────────────────────────────────
    /// Intensity of ambient occlusion darkening.
    float ambientOcclusionIntensity = 1.0f;

    /// Sampling radius for ambient occlusion in centimeters.
    float ambientOcclusionRadius = 200.0f;

    /// Power curve applied to AO result (higher = more contrast).
    float ambientOcclusionPower = 2.0f;

    // ── SSR ─────────────────────────────────────────────────────────
    /// Intensity of screen-space reflections (0-100 percent scale).
    float screenSpaceReflectionIntensity = 100.0f;

    /// Quality of screen-space reflections (0-100 percent scale).
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
    /// Volumes with equal priority blend in insertion order (undefined, depends on insertion order).
    float priority = 0.0f;

    /// Distance in centimeters over which the volume fades in as the camera
    /// approaches from outside. 0 = hard cut (fully on when inside, fully off when outside).
    float blendRadius = 100.0f;

    /// Overall contribution weight multiplier in [0, 1].
    /// 0 = this volume has no effect. 1 = full contribution.
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
    /// Algorithm :
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

    /// Internal storage for a registered volume and its integer ID.
    struct VolumeEntry {
        uint32_t id;
        PostProcessVolume volume;
    };

    std::vector<VolumeEntry> m_volumes;
    uint32_t m_nextId = 1;
};

} // namespace uz
