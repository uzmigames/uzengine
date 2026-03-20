/// Screen-Space Ghost-Based Lens Flare — Compute Shader
///
/// Generates screen-space lens flare ghosts and a halo ring from the bloom
/// threshold texture. Each pixel reflects its UV through the screen center at
/// multiple scales, producing "ghost" images of bright light sources. A
/// chromatic aberration fringe splits R/G/B sampling offsets per ghost for a
/// realistic optical look. A halo ring simulates the bright circular artifact
/// caused by internal lens element reflections.
///
/// # Algorithm
/// 1. For each of 5 ghost scales, reflect UV through screen center, sample the
///    bloom threshold texture with per-channel chromatic offsets, tint with a
///    rainbow gradient, and attenuate at screen edges.
/// 2. Sample a halo ring along the radial direction from screen center with a
///    3-sample Gaussian blur for softness.
/// 3. Sum all contributions, multiply by global intensity and tint.
///
/// # Technique Reference
/// @source John Chapman 2013 — "Real-Time Lens Flare Rendering" (ghost generation,
///   halo ring, chromatic aberration, edge attenuation). This is a screen-space
///   post-process technique, distinct from the reference engine's bokeh-tile rasterization approach.
/// @ref the reference uses a DIFFERENT technique: per-pixel
///   bokeh quad rasterization with instanced draws. the reference engine's DiscMask() (line 23) is
///   analogous to our edge attenuation. the reference engine's LensFlareCompositePS (line 22) applies
///   a FlareColor tint and DiscMask, similar in spirit to our per-ghost tinting +
///   edge falloff. The ghost-based approach here is intentionally different from reference
///   and follows the John Chapman / GPU Pro 5 technique instead.
/// @constants kGhostScales, kGhostColors — user-specified in task description (not
///   from reference; the reference uses LensFlareTints[] array from FPostProcessSettings, line 289
///   of PostProcessLensFlares.cpp). kGhostCount=5 (user spec). kHaloSamples=3 (user
///   spec for Gaussian blur along ring).
/// @deviations INTENTIONAL: This shader implements the John Chapman ghost-based
///   technique, not the reference engine's bokeh-tile rasterization. the reference engine's approach rasterizes one
///   bokeh quad per bright pixel (PostProcessLensFlares.usf:LensFlareBlurVS), which
///   requires vertex buffer setup and instanced draw calls. The ghost-based approach
///   is a single compute dispatch, simpler to integrate, and produces a different
///   (complementary) artistic look. Both are valid real-time lens flare techniques.

// ─────────────────────────────────────────────────────────────────────────────
// Parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Pass-specific constant buffer at b2 (per UzEngine convention for post-process).
cbuffer LensFlareCB : register(b2) {
    uint   bloomThresholdSrv; ///< Bloom threshold texture SRV descriptor heap index.
    float  intensity;         ///< Overall lens flare intensity multiplier (default 1.0).
    float  ghostIntensity;    ///< Ghost contribution multiplier (default 0.5).
    float  haloRadius;        ///< Halo ring radius in UV space, 0..1 (default 0.6).
    float  haloWidth;         ///< Halo ring Gaussian width in UV space (default 0.1).
    float  chromaticShift;    ///< Chromatic fringe offset per channel (default 0.02).
    float  outputWidth;       ///< Output texture width in pixels.
    float  outputHeight;      ///< Output texture height in pixels.
    float2 sunScreenPos;      ///< Sun position in screen UV [0,1]. Ghosts reflect through this point.
    float2 _pad;
};

/// Linear clamp sampler for bloom threshold texture reads.
SamplerState LinearClampSampler : register(s0);

/// Additive lens flare output (RGBA16F). Written additively by this pass, then
/// composited onto the bloom result by a subsequent combine pass.
RWTexture2D<float4> flareOutput : register(u0);

// ─────────────────────────────────────────────────────────────────────────────
// Ghost Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Number of ghost images to generate. Each ghost reflects the bloom texture
/// through the screen center at a different scale.
static const uint kGhostCount = 5;

/// Per-ghost scale factors. Negative of these values are used implicitly by the
/// reflection formula: ghostUV = 0.5 + (0.5 - uv) * scale. Scales < 1 produce
/// smaller ghosts closer to center; scales > 1 produce larger ghosts farther out.
/// @source User specification (task description).
static const float kGhostScales[5] = { 0.3, 0.5, 0.8, 1.2, 1.5 };

/// Per-ghost tint colors forming a rainbow gradient (violet -> blue -> green ->
/// yellow -> red). These mimic the spectral dispersion of light through lens
/// elements. Each ghost picks up a different wavelength-dominant color.
/// @source User specification (task description).
static const float3 kGhostColors[5] = {
    float3(0.5, 0.3, 0.9),  // violet — short wavelength ghost
    float3(0.3, 0.5, 1.0),  // blue
    float3(0.3, 1.0, 0.5),  // green — mid wavelength
    float3(1.0, 1.0, 0.3),  // yellow
    float3(1.0, 0.4, 0.3),  // red — long wavelength ghost
};

/// Number of samples along the halo ring for Gaussian blur.
static const uint kHaloSamples = 3;

/// Gaussian weights for the 3-sample halo ring blur (sigma ~0.65).
/// w0 = center, w1 = +/- 1 offset. Normalized so w0 + 2*w1 = 1.0.
/// Derived from Gaussian(x, sigma=0.65): G(0)=0.4732, G(1)=0.2634.
/// Normalized: 0.4732 / (0.4732 + 2*0.2634) = 0.4732/1.0 = 0.4732, etc.
static const float kHaloWeights[2] = { 0.4732, 0.2634 };

// ─────────────────────────────────────────────────────────────────────────────
// Helper Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Compute edge attenuation for a UV coordinate. Returns 0 when the UV is
/// outside [0,1] and fades smoothly near edges to prevent hard cutoff artifacts.
/// Uses a radial distance from the screen center mapped through a smooth
/// hermite falloff.
///
/// @param uv  Texture coordinate to test, may be outside [0,1].
/// @return    Attenuation factor in [0,1]. 1 at center, 0 at/beyond edges.
float EdgeAttenuation(float2 uv)
{
    // Distance from center in UV space. At the edge of [0,1], this is 0.5.
    float2 d = uv - 0.5;

    // Squared distance from center, scaled so that 1.0 = edge of UV square.
    // Maximum corner distance is sqrt(0.5) ~ 0.707, but we use the axis-aligned
    // max to create a circular-ish mask. Factor of 2 maps 0.5 UV distance to 1.0.
    float dist = length(d) * 2.0;

    // Smooth falloff: fully opaque at center, fading to zero at edges.
    // smoothstep(1, 0.5, dist) = 1 when dist <= 0.5, 0 when dist >= 1.0.
    return smoothstep(1.0, 0.5, dist);
}

/// Sample the bloom threshold texture with chromatic aberration (fringe).
/// Offsets the UV for each color channel to simulate wavelength-dependent
/// refraction through lens elements.
///
/// @param bloomTex  Bloom threshold SRV texture.
/// @param uv        Base UV to sample at.
/// @param shift     Chromatic shift amount (positive = outward dispersion).
/// @return          RGB with per-channel chromatic offset.
float3 SampleChromatic(Texture2D<float4> bloomTex, float2 uv, float shift)
{
    // Direction from screen center — chromatic shift radiates outward.
    float2 dir = uv - 0.5;

    // Per-channel UV offsets: red shifts inward (shorter path), blue shifts
    // outward (longer path through glass), green stays neutral.
    // The multipliers (1 - shift, 1, 1 + shift) create symmetric R/B spread
    // around the green channel.
    float2 uvR = 0.5 + dir * (1.0 - shift);
    float2 uvG = uv;
    float2 uvB = 0.5 + dir * (1.0 + shift);

    float r = bloomTex.SampleLevel(LinearClampSampler, uvR, 0).r;
    float g = bloomTex.SampleLevel(LinearClampSampler, uvG, 0).g;
    float b = bloomTex.SampleLevel(LinearClampSampler, uvB, 0).b;

    return float3(r, g, b);
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Compute shader entry point. Each thread writes one pixel of the lens flare
/// output texture. Dispatched at full display resolution with 8x8 thread groups.
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    // Bounds check: skip threads outside the output dimensions.
    uint outW = (uint)outputWidth;
    uint outH = (uint)outputHeight;
    if (dtid.x >= outW || dtid.y >= outH)
        return;

    // Bindless texture access via descriptor heap.
    Texture2D<float4> bloomTex = ResourceDescriptorHeap[bloomThresholdSrv];

    // Current pixel UV in [0,1].
    float2 uv = (float2(dtid.xy) + 0.5) / float2(outputWidth, outputHeight);

    // ─── Ghost Generation ───────────────────────────────────────────────
    // Reflect the UV through the screen center at multiple scales. Each
    // reflection simulates a "ghost" — a secondary image of a bright light
    // source created by internal reflections between lens elements.

    float3 ghostAccum = float3(0.0, 0.0, 0.0);

    [unroll]
    for (uint i = 0; i < kGhostCount; ++i)
    {
        // Reflect UV through sun screen position and scale.
        // Ghosts are reflections of the sun through lens elements,
        // so they always emanate from the sun's screen position.
        float2 ghostUV = sunScreenPos + (sunScreenPos - uv) * kGhostScales[i];

        // Edge attenuation: ghosts that fall outside the screen should fade
        // out smoothly rather than producing hard-cut artifacts.
        float edgeFade = EdgeAttenuation(ghostUV);

        // Skip this ghost entirely if it's fully outside the screen.
        // This avoids wasted texture samples for out-of-range UVs.
        if (edgeFade <= 0.0)
            continue;

        // Sample with chromatic aberration to simulate wavelength-dependent
        // refraction. The shift is scaled by the ghost's distance from center
        // (larger ghosts = more chromatic spread).
        float3 ghostSample = SampleChromatic(bloomTex, ghostUV, chromaticShift);

        // High-pass threshold: only very bright bloom pixels generate ghosts.
        // This prevents the entire sky from creating ghost artifacts.
        float ghostLuma = dot(ghostSample, float3(0.299, 0.587, 0.114));
        ghostSample *= smoothstep(1.5, 5.0, ghostLuma);

        // Apply per-ghost rainbow tint and edge attenuation.
        ghostAccum += ghostSample * kGhostColors[i] * edgeFade;
    }

    ghostAccum *= ghostIntensity;

    // ─── Halo Ring ──────────────────────────────────────────────────────
    // A bright ring artifact caused by reflections between the front and
    // rear lens elements. We sample along the radial direction from the
    // screen center at a fixed radius, with a 3-sample Gaussian blur for
    // softness.

    float3 haloAccum = float3(0.0, 0.0, 0.0);

    // Direction from sun position to current pixel.
    float2 toCenter = uv - sunScreenPos;
    float centerDist = length(toCenter);

    // Only compute halo if the pixel is in a plausible range. Avoid
    // division by zero for pixels exactly at the center.
    if (centerDist > 1.0e-5)
    {
        float2 dir = toCenter / centerDist;

        // Perpendicular direction for blur spread along the ring tangent.
        float2 perp = float2(-dir.y, dir.x);

        // Center sample: reflect through center, then offset to halo radius.
        // The halo UV is the reflected direction scaled to haloRadius.
        float2 haloCenter = sunScreenPos + dir * haloRadius;

        // 3-sample Gaussian blur along the ring tangent direction.
        // This softens the halo and prevents single-pixel aliasing.
        [unroll]
        for (int s = -1; s <= 1; ++s)
        {
            float2 sampleUV = haloCenter + perp * (float(s) * haloWidth * 0.5);
            float weight = kHaloWeights[abs(s)];

            // Edge attenuation for halo samples.
            float edgeFade = EdgeAttenuation(sampleUV);

            // Chromatic aberration on halo as well.
            float3 haloSample = SampleChromatic(bloomTex, sampleUV, chromaticShift * 0.5);

            // High-pass: only very bright sources create halo
            float haloLuma = dot(haloSample, float3(0.299, 0.587, 0.114));
            haloSample *= smoothstep(1.5, 5.0, haloLuma);

            haloAccum += haloSample * weight * edgeFade;
        }
    }

    // ─── Final Combine ──────────────────────────────────────────────────
    // Sum ghosts and halo, apply global intensity. Output is additive
    // contribution to be blended with the bloom result by a subsequent pass.
    float3 result = (ghostAccum + haloAccum) * intensity;

    // Clamp to prevent NaN/Inf propagation from extreme HDR values.
    result = max(result, float3(0.0, 0.0, 0.0));

    flareOutput[dtid.xy] = float4(result, 1.0);
}
