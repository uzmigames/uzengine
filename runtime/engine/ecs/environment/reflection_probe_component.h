/// UzEngine ReflectionProbeComponent — ECS data for local reflection capture probes.
///
/// Equivalent to standard reflection capture component. Defines a spherical
/// volume within which a captured cubemap provides localized specular and
/// diffuse IBL. Probes are blended by the deferred lighting shader based on
/// influence radius, blend distance, and priority.
///
/// Multiple entities may carry this component. The renderer gathers all
/// enabled probes, sorts by priority, and uploads the top N to a
/// StructuredBuffer<ReflectionProbeGPU> for the lighting pass.
///
/// # Relationship to Other Components
/// - **SkyLightComponent**: Provides the fallback global IBL cubemap used
///   when no reflection probe covers a pixel (or as a blend target at
///   probe boundaries).
/// - **SkyAtmosphereComponent**: Defines the physical sky model. Probes
///   capture the sky atmosphere when recaptured at runtime.
///
/// # Usage
/// ```cpp
/// auto entity = world.CreateEntity();
/// auto& probe = world.AddComponent<ReflectionProbeComponent>(entity);
/// probe.influenceRadius = 500.0f;  // 5m sphere
/// probe.resolution = 256;          // Higher-quality capture
/// probe.priority = 1;              // Override lower-priority probes
/// ```

#pragma once

#include <cstdint>

namespace uz {

/// Local reflection capture probe parameters.
///
/// Defines a spherical influence volume where a prefiltered cubemap
/// replaces or blends with the global sky IBL. The renderer sorts
/// active probes by priority and blends overlapping probes using
/// the blend distance fade zone at the sphere edge.
///
/// # Parity
/// Maps to standard ReflectionCapture parameters. Key notes:
/// - UzEngine uses centimeters (1 unit = 1 cm).
/// - Capture is deferred to the renderer — the component only stores
///   configuration; actual cubemap resources are managed by the capture system.
/// @ref McGuire 2017 "Efficient GPU Screen-Space Ray Tracing" — probe blending
/// @ref Lagarde 2012 "Local Image-based Lighting With Parallax-corrected Cubemaps"
struct ReflectionProbeComponent {
    // ── Influence Volume ─────────────────────────────────────────────────

    /// Influence radius in cm — pixels inside this sphere use this probe's cubemap.
    /// Objects outside this radius receive no contribution from this probe.
    /// @ref Standard ReflectionCapture — InfluenceRadius default 300cm (3m)
    float influenceRadius = 300.0f;

    /// Blend distance in cm — fade zone at the edge of the influence sphere.
    /// Within this band (from influenceRadius-blendDistance to influenceRadius),
    /// the probe's contribution is linearly faded to zero, allowing smooth
    /// transitions between adjacent probes or the fallback sky IBL.
    /// @ref Standard ReflectionCapture — blend transition default 100cm
    float blendDistance = 100.0f;

    // ── Capture Configuration ────────────────────────────────────────────

    /// Capture resolution per cubemap face (power of 2).
    /// 128 is a good quality/performance balance. Higher resolutions produce
    /// sharper reflections but increase VRAM and capture cost.
    /// @ref Standard ReflectionCapture — CaptureResolution default 128
    uint32_t resolution = 128;

    // ── Priority & State ─────────────────────────────────────────────────

    /// Priority for overlapping probes (higher wins).
    /// When a pixel falls inside multiple probe influence volumes, the probe
    /// with the highest priority is used first, with lower-priority probes
    /// blended in as fallback where the higher-priority probe fades out.
    /// @ref Standard ReflectionCapture — SortPriority default 0
    int32_t priority = 0;

    /// Whether this probe is enabled.
    /// Disabled probes are skipped during gathering and do not contribute
    /// to the lighting pass.
    bool enabled = true;

    /// Whether this probe needs recapture (dirty flag).
    /// Set to true when the probe is first created or when scene geometry
    /// changes within its influence volume. The capture system clears this
    /// flag after a successful cubemap capture.
    bool needsCapture = true;

    // ── Runtime (filled by renderer, not user) ───────────────────────────

    /// SRV index of the prefiltered specular cubemap in the bindless heap.
    /// UINT32_MAX indicates the probe has not been captured yet and should
    /// not contribute to lighting.
    uint32_t prefilteredSrvIndex = UINT32_MAX;

    /// SRV index of the irradiance cubemap in the bindless heap.
    /// Used for the diffuse IBL contribution from this probe.
    /// UINT32_MAX indicates not yet captured.
    uint32_t irradianceSrvIndex = UINT32_MAX;
};

} // namespace uz
