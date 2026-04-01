/// UzEngine PostProcessVolumeComponent — ECS data for spatial post-process blending.
///
/// Stores post-process override settings for an AABB volume region.
/// When the camera is inside or near this volume, the overridden settings
/// blend with other volumes and the global defaults based on priority and
/// distance-based weight.
///
/// # Volume Bounds
/// The volume is an axis-aligned box centered at the entity's transform position.
/// `extents` defines half-extents in centimeters. The world-space AABB is:
///   [entityPos - extents, entityPos + extents]
///
/// # Unbound Mode
/// When `isUnbound` is true, the volume has infinite extent and applies globally.
/// This is useful for a "base layer" of post-process settings that everything
/// else overrides.
///
/// # Override Flags
/// Each setting has a corresponding `override*` bool. Only settings with their
/// override flag set participate in blending. Disabled overrides pass through
/// from lower-priority volumes or the engine default.
///
/// # Blending
/// Volumes are sorted by priority (ascending). Higher priority volumes are
/// applied later and override lower-priority ones. The blendRadius controls
/// the fade-in distance when the camera is outside the volume box.
///
/// @ref Epic Games GDC 2012 "PostProcessVolume blending architecture"
///
/// # Usage
/// ```cpp
/// auto entity = world.CreateEntity();
/// auto& vol = world.AddComponent<PostProcessVolumeComponent>(entity);
/// vol.priority = 1.0f;
/// vol.overrideBloomIntensity = true;
/// vol.bloomIntensity = 1.2f;
/// ```

#pragma once

#include "vector3.h"

#include <cstdint>

namespace uz {

/// Post-process volume settings attached to an entity with spatial transform.
///
/// The entity's `SceneComponent` provides the volume center position.
/// `extents` defines the half-size of the AABB in each axis (cm).
///
/// Only fields with their `override*` flag set participate in volume blending.
/// Non-overridden fields are ignored (pass-through from lower priority or defaults).
struct PostProcessVolumeComponent {
    // ── Volume Geometry ────────────────────────────────────────────────

    /// AABB half-extents in centimeters (local space, centered on entity position).
    /// Default: 500 cm (5 meter) cube in each direction.
    Vec3 extents = {500.f, 500.f, 500.f};

    /// When true, this volume has infinite extent and applies everywhere.
    /// Useful as a global base layer. Default: false.
    bool isUnbound = false;

    // ── Blending Control ───────────────────────────────────────────────

    /// Priority for volume ordering. Higher values override lower values.
    /// Volumes with equal priority blend in undefined order.
    float priority = 0.0f;

    /// Distance in centimeters over which the volume fades in as the camera
    /// approaches from outside. 0 = hard cut (no gradual transition).
    float blendRadius = 100.0f;

    /// Overall weight multiplier [0, 1]. 0 = no effect, 1 = full contribution.
    float blendWeight = 1.0f;

    /// Whether this volume is active. Disabled volumes are skipped entirely.
    bool enabled = true;

    // ── Bloom ──────────────────────────────────────────────────────────

    /// Override bloom intensity.
    bool overrideBloomIntensity = false;

    /// Overall bloom intensity multiplier.
    float bloomIntensity = 0.675f;

    /// Override bloom threshold.
    bool overrideBloomThreshold = false;

    /// Luminance threshold below which pixels do not bloom.
    /// -1.0 = use engine default.
    float bloomThreshold = -1.0f;

    // ── Auto-Exposure ──────────────────────────────────────────────────

    /// Override minimum average scene brightness for auto-exposure.
    bool overrideAutoExposureMinBrightness = false;

    /// Minimum average brightness for exposure adaptation.
    float autoExposureMinBrightness = 0.03f;

    /// Override maximum average scene brightness for auto-exposure.
    bool overrideAutoExposureMaxBrightness = false;

    /// Maximum average brightness for exposure adaptation.
    float autoExposureMaxBrightness = 8.0f;

    /// Override exposure bias (EV offset).
    bool overrideAutoExposureBias = false;

    /// Logarithmic exposure bias. 0 = neutral, 1 = 2x brighter, -1 = 2x darker.
    float autoExposureBias = 0.0f;

    /// Override exposure adaptation speed (brightening).
    bool overrideAutoExposureSpeedUp = false;

    /// Speed at which exposure adapts when the scene brightens (EV/s).
    float autoExposureSpeedUp = 3.0f;

    /// Override exposure adaptation speed (darkening).
    bool overrideAutoExposureSpeedDown = false;

    /// Speed at which exposure adapts when the scene darkens (EV/s).
    float autoExposureSpeedDown = 1.0f;

    // ── Depth of Field ─────────────────────────────────────────────────

    /// Override focal distance.
    bool overrideDofFocalDistance = false;

    /// Distance to the focal plane in centimeters. 0 = DoF disabled.
    float dofFocalDistance = 0.0f;

    /// Override focal region width.
    bool overrideDofFocalRegion = false;

    /// Width of the in-focus region around the focal plane (cm).
    float dofFocalRegion = 0.0f;

    /// Override near blur transition distance.
    bool overrideDofNearTransition = false;

    /// Near blur transition width (cm).
    float dofNearTransition = 300.0f;

    /// Override far blur transition distance.
    bool overrideDofFarTransition = false;

    /// Far blur transition width (cm).
    float dofFarTransition = 500.0f;

    /// Override f-stop aperture.
    bool overrideDofFstop = false;

    /// Simulated camera f-stop for cinematic DoF. Lower = wider aperture = more blur.
    float dofFstop = 4.0f;

    // ── Motion Blur ────────────────────────────────────────────────────

    /// Override motion blur amount.
    bool overrideMotionBlurAmount = false;

    /// Motion blur strength. 0 = disabled, 1 = full.
    float motionBlurAmount = 0.5f;

    /// Override maximum motion blur distance.
    bool overrideMotionBlurMax = false;

    /// Maximum blur distance in percent of screen width.
    float motionBlurMax = 5.0f;

    /// Override motion blur target FPS.
    bool overrideMotionBlurTargetFPS = false;

    /// Target framerate for motion blur velocity scaling. Lower = more blur.
    int motionBlurTargetFPS = 30;

    // ── Color Grading / White Balance ──────────────────────────────────

    /// Override white balance temperature.
    bool overrideWhiteTemp = false;

    /// White balance color temperature in Kelvin.
    float whiteTemp = 6500.0f;

    /// Override white balance tint.
    bool overrideWhiteTint = false;

    /// White balance green-magenta tint shift. 0 = neutral.
    float whiteTint = 0.0f;

    // ── ACES Tonemap ───────────────────────────────────────────────────

    /// Override ACES film slope.
    bool overrideFilmSlope = false;

    /// ACES tonemap slope (mid-tone contrast).
    float filmSlope = 0.88f;

    /// Override ACES film toe.
    bool overrideFilmToe = false;

    /// ACES tonemap toe (dark region rolloff).
    float filmToe = 0.55f;

    /// Override ACES film shoulder.
    bool overrideFilmShoulder = false;

    /// ACES tonemap shoulder (highlight compression).
    float filmShoulder = 0.26f;

    /// Override ACES film black clip.
    bool overrideFilmBlackClip = false;

    /// ACES tonemap black clip (crush blacks below this).
    float filmBlackClip = 0.0f;

    /// Override ACES film white clip.
    bool overrideFilmWhiteClip = false;

    /// ACES tonemap white clip (clip whites above this).
    float filmWhiteClip = 0.04f;

    // ── Vignette ───────────────────────────────────────────────────────

    /// Override vignette intensity.
    bool overrideVignetteIntensity = false;

    /// Screen-edge darkening intensity. 0 = disabled.
    float vignetteIntensity = 0.4f;

    // ── SSAO ───────────────────────────────────────────────────────────

    /// Override ambient occlusion intensity.
    bool overrideAmbientOcclusionIntensity = false;

    /// SSAO darkening intensity.
    float ambientOcclusionIntensity = 1.0f;

    /// Override ambient occlusion radius.
    bool overrideAmbientOcclusionRadius = false;

    /// SSAO sampling radius in centimeters.
    float ambientOcclusionRadius = 200.0f;

    /// Override ambient occlusion power.
    bool overrideAmbientOcclusionPower = false;

    /// SSAO power curve (contrast). Higher = more contrast.
    float ambientOcclusionPower = 2.0f;

    // ── SSR ────────────────────────────────────────────────────────────

    /// Override SSR intensity.
    bool overrideScreenSpaceReflectionIntensity = false;

    /// Screen-space reflection intensity (0-100%).
    float screenSpaceReflectionIntensity = 100.0f;

    /// Override SSR quality.
    bool overrideScreenSpaceReflectionQuality = false;

    /// Screen-space reflection quality (0-100%).
    float screenSpaceReflectionQuality = 50.0f;
};

} // namespace uz
