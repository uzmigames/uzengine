/// VFX Procedural Distortion Haze Pixel Shader — fullscreen radial heat shimmer.
///
/// Creates a procedural heat-haze distortion effect around an emitter's screen
/// position using layered animated sine waves with smooth radial falloff. This
/// is a fullscreen pass that writes UV offset vectors into an RGBA16F distortion
/// accumulation target, which the resolve pass (`distortion_resolve_ps.hlsl`)
/// later uses to warp the HDR scene color.
///
/// # Algorithm
/// 1. Compute distance from pixel UV to emitter screen UV.
/// 2. Apply smooth radial falloff: `smoothstep(radius, 0.0, dist)`.
/// 3. Generate animated UV offset from two layered sine waves along the radial
///    direction, producing rising-heat shimmer.
/// 4. Scale by global strength and falloff intensity.
/// 5. Output RG = UV offset direction, A = warp magnitude.
///
/// # Output Format
/// - R: Horizontal UV offset (positive = warp right).
/// - G: Vertical UV offset (positive = warp down).
/// - B: Unused (zero).
/// - A: Distortion intensity (warp magnitude for resolve pass).
///
/// The resolve pass applies: `warpedUV = baseUV + distortion.rg * distortion.a`
///
/// # Root Signature
/// - b0: DistortionHazeConstants (emitter UV, strength, time, radius, frequency).
/// - s0: linearSampler (bilinear, wrap — static sampler, unused by this shader).
///
/// @ref Vlachos 2015 "Advanced VR Rendering" — screen-space UV distortion
/// @ref Tatarchuk 2006 "Artist-Directable Real-Time Rain" — radial distortion waves
/// @constants UV_OFFSET_SCALE=0.02 (max UV displacement at full strength)
/// @constants WAVE_SPEED_PRIMARY=2.5 (primary sine wave animation speed)
/// @constants WAVE_SPEED_SECONDARY=1.7 (secondary sine wave animation speed)
/// @constants FREQ_MULTIPLIER_SECONDARY=1.6 (frequency ratio for second wave layer)
/// @constants AMPLITUDE_SECONDARY=0.35 (amplitude ratio of secondary wave vs primary)
/// @constants MIN_INTENSITY=0.001 (threshold below which pixels output zero)
/// @deviations NONE

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Maximum UV displacement at full strength (strength=1.0, falloff=1.0).
/// At 1920x1080 this is approximately 38 pixels horizontally — enough for a
/// visible heat shimmer without jarring warping.
///
/// @ref Vlachos 2015 "Advanced VR Rendering" — typical UV warp magnitudes
static const float UV_OFFSET_SCALE = 0.02f;

/// Animation speed for the primary sine wave (radians per second).
/// Controls how fast the heat shimmer ripples outward from the emitter.
///
/// @ref Tatarchuk 2006 "Artist-Directable Real-Time Rain" — wave animation rates
static const float WAVE_SPEED_PRIMARY = 2.5f;

/// Animation speed for the secondary sine wave (radians per second).
/// Offset from the primary to avoid synchronization, creating organic motion.
static const float WAVE_SPEED_SECONDARY = 1.7f;

/// Frequency multiplier for the secondary wave relative to the user-supplied
/// frequency. Creates visual richness by layering a higher-frequency ripple
/// on top of the primary wave.
static const float FREQ_MULTIPLIER_SECONDARY = 1.6f;

/// Amplitude ratio of the secondary wave relative to the primary.
/// Kept below 0.5 so the secondary adds detail without overpowering.
static const float AMPLITUDE_SECONDARY = 0.35f;

/// Intensity threshold below which the pixel outputs pure zero.
/// Prevents near-invisible distortion fragments from consuming blend bandwidth.
static const float MIN_INTENSITY = 0.001f;

/// Mathematical constant pi.
static const float PI = 3.14159265358979323846f;

// ─────────────────────────────────────────────────────────────────────────────
// Input Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Interpolants from the fullscreen triangle vertex shader (fullscreen_vs.hlsl).
/// The VS generates position and UV from SV_VertexID with no vertex buffer.
struct PSInput {
    float4 positionCS : SV_Position; ///< Screen-space position.
    float2 texcoord   : TEXCOORD0;   ///< Fullscreen UV in [0,1].
};

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0) — Haze Parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame constants for the procedural haze effect.
/// Filled by the CPU each frame. 32 bytes = 8 uint32 root constants.
/// SM5.0-compatible cbuffer syntax (DX11 does not support ConstantBuffer<T> template).
cbuffer DistortionHazeCB : register(b0) {
    float2 emitterScreenUV; ///< Emitter position in screen UV [0,1].
    float  hazeStrength;    ///< Global distortion strength (0 = off, 1 = full).
    float  hazeTime;        ///< Total elapsed time in seconds for animation.
    float  hazeRadius;      ///< Falloff radius in screen UV units (e.g. 0.3 = 30% of screen).
    float  hazeFrequency;   ///< Wave frequency for animated haze (higher = tighter ripples).
    float  _pad0;           ///< Padding to 16-byte alignment.
    float  _pad1;           ///< Padding to 16-byte alignment.
};

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Computes procedural radial heat-haze distortion UV offsets.
///
/// The effect simulates rising hot air around the emitter's screen position.
/// Two layered sine waves modulate the UV offset along the radial direction
/// from emitter to pixel, creating an organic heat shimmer. A smooth radial
/// falloff confines the effect to a circular region.
///
/// # Parameters
/// - input: interpolated fullscreen triangle vertex data.
///
/// # Returns
/// float4(uvOffset.x, uvOffset.y, 0.0, intensity) for RGBA16F accumulation.
///
/// @ref Vlachos 2015 "Advanced VR Rendering" — screen-space UV distortion
/// @ref Tatarchuk 2006 "Artist-Directable Real-Time Rain" — radial distortion waves
float4 main(PSInput input) : SV_Target {
    // ── Load parameters from root constants ──
    float2 emitterUV = emitterScreenUV;
    float  strength  = hazeStrength;
    float  time      = hazeTime;
    float  radius    = hazeRadius;
    float  frequency = hazeFrequency;

    // ── Early out if effect is globally disabled ──
    if (strength <= 0.0f) {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // ── Compute vector from emitter to pixel in UV space ──
    float2 toPixel = input.texcoord - emitterUV;
    float  dist    = length(toPixel);

    // ── Early out for pixels outside the falloff radius ──
    // Pixels beyond the radius contribute zero distortion.
    if (dist >= radius) {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // ── Radial falloff ──
    // smoothstep produces a smooth [0,1] falloff from the radius edge (0) to
    // the emitter center (1). This creates a natural intensity gradient that
    // is strongest near the emitter and fades to nothing at the perimeter.
    float falloff = smoothstep(radius, 0.0f, dist);

    // ── Compute radial direction ──
    // When the pixel is at the exact emitter center (dist == 0), the radial
    // direction is undefined. Default to an upward bias (+Y in UV = downward
    // on screen) to simulate heat rising. The epsilon check avoids NaN from
    // normalizing a zero-length vector.
    float2 radialDir;
    if (dist > 1e-6f) {
        radialDir = toPixel / dist;
    } else {
        // At the emitter center, bias distortion upward (heat rises).
        // In UV space, negative Y = upward on screen.
        radialDir = float2(0.0f, -1.0f);
    }

    // ── Normalized distance for wave input ──
    // Map distance to [0,1] within the radius for consistent wave behavior
    // regardless of radius size.
    float normalizedDist = dist / max(radius, 1e-6f);

    // ── Primary sine wave ──
    // The wave propagates outward from the emitter: higher dist = later phase.
    // Subtracting time creates outward motion (phase decreases over time at
    // a fixed position, moving the wave crest outward).
    float phase1   = normalizedDist * frequency * PI * 2.0f - time * WAVE_SPEED_PRIMARY;
    float wave1    = sin(phase1);

    // ── Secondary sine wave (detail layer) ──
    // Higher frequency and different speed break up the regularity of the
    // primary wave, creating a more organic heat shimmer appearance.
    float phase2   = normalizedDist * frequency * FREQ_MULTIPLIER_SECONDARY * PI * 2.0f
                     - time * WAVE_SPEED_SECONDARY;
    float wave2    = sin(phase2);

    // ── Combine waves ──
    // The primary wave dominates; the secondary adds visual richness.
    float combinedWave = wave1 + wave2 * AMPLITUDE_SECONDARY;

    // ── Compute UV offset ──
    // The offset is along the radial direction, modulated by the combined
    // wave pattern and the radial falloff. UV_OFFSET_SCALE limits the maximum
    // displacement to prevent jarring warping.
    float2 uvOffset = radialDir * combinedWave * falloff * UV_OFFSET_SCALE;

    // ── Compute output intensity ──
    // Intensity is the product of the radial falloff and global strength.
    // The resolve pass multiplies RG by A, so this controls the final warp
    // magnitude. Using falloff (not falloff * |wave|) ensures smooth edges
    // even when the wave crosses zero.
    float intensity = falloff * saturate(strength);

    // ── Discard nearly-invisible pixels ──
    if (intensity < MIN_INTENSITY) {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    return float4(uvOffset.x, uvOffset.y, 0.0f, intensity);
}
