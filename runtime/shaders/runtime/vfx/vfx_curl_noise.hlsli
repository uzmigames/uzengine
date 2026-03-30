/// VFX Curl Noise — Animated divergence-free velocity field for particle forces.
///
/// Provides the AnimatedCurlNoise wrapper around the CurlNoise3D function
/// defined in vfx_common.hlsli. The wrapper applies spatial frequency scaling
/// and temporal animation (Z-axis scroll) to produce organic, time-varying
/// swirling motion suitable for smoke, fire, and dust particle effects.
///
/// The underlying curl computation uses central finite differences of a 4-octave
/// FBM (Fractal Brownian Motion) noise field. The curl of any smooth vector
/// field is inherently divergence-free (div(curl(F)) = 0), which means particles
/// driven by this force will neither clump together nor fly apart -- they
/// produce natural swirling motion that conserves volume.
///
/// @ref Bridson 2007 "Curl-Noise for Procedural Fluid Flow"
/// @constants eps=0.01 (finite-difference step, from CurlNoise3D in vfx_common.hlsli)

#ifndef VFX_CURL_NOISE_HLSLI
#define VFX_CURL_NOISE_HLSLI

// CurlNoise3D and FBM3D are defined in vfx_common.hlsli, which is already
// included by particle_update_cs.hlsl before this file.

/// Evaluates animated curl noise at a world-space position.
///
/// Scales the input position by `freq` to control swirl tightness, then
/// scrolls the noise field along the Z axis at `speed` units per second
/// to create temporal variation. The result is a divergence-free velocity
/// vector that can be accumulated as a force on particle velocity.
///
/// @ref Bridson 2007 "Curl-Noise for Procedural Fluid Flow" --
///      animated potential scrolls along Z at curlSpeed.
///
/// # Arguments
/// - `pos`:   world-space particle position.
/// - `time`:  total elapsed simulation time in seconds.
/// - `speed`: animation scroll speed (world units/second along Z).
/// - `freq`:  spatial frequency multiplier. Higher = tighter swirls.
///
/// # Returns
/// Divergence-free 3D velocity vector (not yet scaled by strength).
float3 AnimatedCurlNoise(float3 pos, float time, float speed, float freq)
{
    // Scale position by frequency to control swirl spatial scale.
    // Animate by scrolling the noise field along Z over time.
    float3 samplePos = pos * freq + float3(0.0f, 0.0f, time * speed);
    return CurlNoise3D(samplePos, time);
}

#endif // VFX_CURL_NOISE_HLSLI
