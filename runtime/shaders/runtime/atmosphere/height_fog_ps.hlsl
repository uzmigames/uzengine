/// @ref Hillaire 2016 "Physically Based & Unified Volumetric Rendering in Frostbite" — exponential fog integration
/// @constants FLT_EPSILON2=0.01 (Taylor expansion threshold),
///            Falloff clamp=-127.0f (IEEE exp2 overflow guard),
///            rsqrt guard=0.00000001f (zero-length ray guard),
///            UniformPhaseFunction=1/(4*PI) (isotropic phase normalization)
/// @deviations NONE
///
/// Height fog pixel shader — exponential height fog with two density layers.
///
/// Fullscreen pass that reads scene depth, reconstructs world position,
/// and computes exponential height fog with:
///   - Two fog density layers with independent height falloff
///   - Observer height clamping (MaxWorldObserverHeight)
///   - Fog start distance exclusion with density re-computation
///   - End distance (horizontal XY-plane clamping)
///   - Fog cutoff distance
///   - Directional inscattering (sun glow in fog) with start distance
///   - Sky atmosphere ambient injection into inscattering
///   - Sky atmosphere post-transmittance sun illuminance for directional inscattering
///   - Sky pixels: integrates fog along the view ray using farPlane distance
///
/// # Root Signature
/// - b0: 8x uint32 root constants (bindless descriptor indices)
/// - b1: PerFrameCB (camera, invViewProj, lighting globals)
/// - b2: HeightFogCB (all fog parameters — matches C++ HeightFogCB struct)
/// - s0: static linear clamp sampler
///
/// # Blend State
/// Output: float4(inscatterColor, fogOpacity)
/// Blend: SrcAlpha, InvSrcAlpha (premultiplied inscattering + opacity)

// ─────────────────────────────────────────────────────────────────────────────
// Input Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Interpolants from the fullscreen triangle vertex shader (fullscreen_vs.hlsl).
struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (bindless descriptor indices)
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Root constants at b0 — descriptor heap index for the depth SRV.
/// Only one texture is needed: scene depth for world position reconstruction.
struct FogIndices {
    uint depthIdx;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};
ConstantBuffer<FogIndices> fogIndices : register(b0);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// PerFrameCB at b1 — subset of fields used by this shader
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame constant buffer. Layout MUST match the C++ PerFrameCB struct and
/// all other shaders that reference b1. Only fields read by this shader are
/// commented; unused fields still occupy their slots for correct padding.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;     // 64 bytes (offset 0)
    float4   cameraPos;              // 16 bytes (offset 64) — xyz = world position
    float4   timeParams;             // 16 bytes (offset 80) — unused here
    float4   lightDir;               // 16 bytes (offset 96) — unused here (fog uses sunDir from HeightFogCB)
    float4   lightColor;             // 16 bytes (offset 112) — unused here
    float4   ambientColor;           // 16 bytes (offset 128) — unused here
    row_major float4x4 invViewProj;  // 64 bytes (offset 144) — for world pos reconstruction
    float    nearPlane;              // 4 bytes  (offset 208)
    float    farPlane;               // 4 bytes  (offset 212) — used for sky fog ray length
    float    _framePad[10];          // 40 bytes (offset 216) — remaining per-frame padding
};

// ─────────────────────────────────────────────────────────────────────────────
// HeightFogCB at b2 — exponential height fog parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Height fog constant buffer. Layout MUST match the C++ HeightFogCB struct
/// (constant_buffers.h). Contains all parameters for two-layer exponential
/// height fog with directional inscattering and sky atmosphere injection.
///
/// The CPU pre-computes RayOriginTerms for each layer:
///   fogDensity = density * exp2(-falloff * (observerZ - fogHeight))
/// so the shader does not need to know the camera height explicitly for the
/// base case (only for start-distance re-computation, which uses rawDensity).
cbuffer HeightFogCB : register(b2) {
    // ── Primary fog layer ────────────────────────────────────────────────
    /// Fog inscattering color (populated by fog constant buffer on CPU)
    float4 fogColor;                 // xyz = inscattering color, w = unused

    /// Precomputed ray origin density: density * exp2(-falloff*(observerZ - fogHeight))
    float  fogDensity;               // PRE-COMPUTED: density * exp2(-falloff*(observerZ - fogHeight))

    /// Height falloff coefficient for primary fog layer
    float  fogHeightFalloff;         // Height falloff coefficient

    /// Fog base height in world-space (cm)
    float  fogBaseHeight;            // Fog base height (world-space, cm)

    /// Maximum fog opacity (0-1). MinFogOpacity is computed as (1 - fogMaxOpacity).
    float  fogMaxOpacity;            // Maximum fog opacity (0-1)

    /// Minimum distance before fog begins (cm)
    float  fogStartDistance;         // Minimum distance before fog begins (cm)

    /// Beyond this distance, fog factor = 1 (0 = unlimited)
    float  fogCutoffDistance;        // Beyond this distance, fog factor = 1 (0 = unlimited)

    /// Clamped max observer Z for shader-side height compensation
    float  maxObserverHeight;        // Clamped max observer Z for shader-side height compensation

    /// Raw (un-precomputed) density for start distance recalculation
    float  rawDensity;               // Raw (un-precomputed) density for start distance recalculation

    // ── Second fog layer ─────────────────────────────────────────────────
    /// Precomputed second layer density: secondDensity * exp2(-falloff*(observerZ-baseH))
    float  secondFogDensity;         // PRE-COMPUTED: secondDensity * exp2(-falloff*(observerZ-baseH))

    /// Second layer height falloff coefficient
    float  secondFogHeightFalloff;   // Second layer height falloff coefficient

    /// Raw density for start distance recalculation (second layer)
    float  secondFogRawDensity;      // Raw density for start distance recalculation

    /// Second layer base height (cm)
    float  secondFogBaseHeight;      // Second layer base height (cm)

    // ── Directional inscattering ─────────────────────────────────────────
    /// Sun-facing fog glow color (xyz), exponent packed in w
    float4 directionalInscatteringColor; // xyz = sun-facing fog glow color, w = exponent

    /// Phase function power (higher = tighter sun halo)
    float  directionalInscatteringExponent; // Phase function power (higher = tighter sun halo)

    /// Start distance for directional inscattering
    float  directionalInscatteringStartDistance; // Start distance for directional inscattering

    float  _fogPad2[2];             // Pad to 16-byte boundary

    // ── Sky atmosphere injection ──────────────────────────────────────────
    /// Scale for sky atmosphere ambient contribution to fog inscattering
    float4 skyAtmContribColorScale;  // xyz = scale for sky atmosphere ambient contribution

    /// Sky atmosphere height fog contribution factor (0-1, default 1.0)
    float  heightFogContribution;    // SkyAtmosphere.HeightFogContribution (0-1, default 1.0)

    /// Sky light diffuse color (from SH L0 or cubemap average)
    float  skyLightColorR;           // Sky light diffuse color R (from SH L0 or cubemap average)
    float  skyLightColorG;           // Sky light diffuse color G
    float  skyLightColorB;           // Sky light diffuse color B

    // ── Sun post-transmittance illuminance ────────────────────────────────
    /// Post-transmittance sun illuminance (atmosphere-filtered sun color at ground level)
    float  sunIllumPostTransR;       // Post-transmittance sun illuminance R
    float  sunIllumPostTransG;       // Post-transmittance sun illuminance G
    float  sunIllumPostTransB;       // Post-transmittance sun illuminance B
    float  sunIllumPostTransA;       // 1.0 = has atmosphere sun, 0.0 = none

    /// Sun direction in world-space (toward sun)
    float  sunDirX;                  // Sun direction X (world-space, toward sun)
    float  sunDirY;                  // Sun direction Y
    float  sunDirZ;                  // Sun direction Z
    float  _sunPad;                  // Pad to 16-byte boundary
};

// ─────────────────────────────────────────────────────────────────────────────
// Depth Texture (SM5 fallback)
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_BINDLESS
Texture2D<float> sceneDepth : register(t0);
#endif

SamplerState linearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Threshold for Taylor expansion selection in IntegrateExponentialFogRay.
/// When |Falloff| <= 0.01, the exact formula (1 - exp2(-x))/x is numerically
/// unstable due to division near zero; the Taylor expansion is used instead.
static const float FLT_EPSILON2 = 0.01f;

/// Normalization constant for the isotropic (uniform) phase function: 1 / (4 * PI).
/// Applied after the directional cosine-power phase when not matching volumetric fog.
static const float UNIFORM_PHASE_FUNCTION = 1.0f / (4.0f * 3.14159265359f);

// ─────────────────────────────────────────────────────────────────────────────
// IntegrateExponentialFogRay
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Hillaire 2016 "Physically Based & Unified Volumetric Rendering in Frostbite" — exponential fog integration
/// @constants Falloff clamp = -127.0f (IEEE exp2 overflow guard), FLT_EPSILON2 = 0.01f (Taylor threshold)
/// @deviations NONE
///
/// Calculates the line integral of exponential height fog along a ray segment.
///
/// The exponential fog density function is:
///   d(z) = GlobalDensity * exp2(-HeightFalloff * z)
///
/// The line integral from z0 to z1 along a ray is:
///   integral = RayOriginTerms * (1 - exp2(-Falloff)) / Falloff
/// where Falloff = HeightFalloff * RayDirectionZ.
///
/// When |Falloff| is very small (ray nearly horizontal), this formula is
/// numerically unstable; a first-order Taylor expansion is used instead:
///   integral = RayOriginTerms * (ln(2) - 0.5 * ln(2)^2 * Falloff)
///
/// @param FogHeightFalloff  Height falloff coefficient for this fog layer.
/// @param RayDirectionZ     Z component of the (unnormalized) ray direction.
/// @param RayOriginTerms    Pre-computed density * exp2(-falloff * (originZ - fogHeight)).
/// @return The line integral (optical depth per unit ray length) for this layer.
float IntegrateExponentialFogRay(float FogHeightFalloff, float RayDirectionZ, float RayOriginTerms)
{
    // Clamp to prevent exp2() overflow (IEEE float max exponent).
    float Falloff = max(-127.0f, FogHeightFalloff * RayDirectionZ);

    // Exact formula: (1 - exp2(-Falloff)) / Falloff
    float LineIntegral = (1.0f - exp2(-Falloff)) / Falloff;

    // Taylor expansion around Falloff = 0:
    //   ln(2) - 0.5 * ln(2)^2 * Falloff
    // where log() is the natural logarithm (ln).
    float Ln2 = log(2.0f);
    float LineIntegralTaylor = Ln2 - (0.5f * Ln2 * Ln2) * Falloff;

    // Select exact or Taylor based on magnitude (FLT_EPSILON2 = 0.01 threshold).
    return RayOriginTerms * (abs(Falloff) > FLT_EPSILON2 ? LineIntegral : LineIntegralTaylor);
}

// ─────────────────────────────────────────────────────────────────────────────
// World Position Reconstruction
// ─────────────────────────────────────────────────────────────────────────────

/// Reconstructs world-space position from screen UV and hardware depth.
///
/// @param uv     Screen-space texture coordinate [0,1]^2.
/// @param depth  Hardware depth value (reversed-Z: 1.0 = near, 0.0 = far).
/// @return World-space position in centimeters.
float3 ReconstructWorldPosition(float2 uv, float depth)
{
    // Convert UV to NDC: [0,1] -> [-1,1], flip Y for DX convention.
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    float4 clipPos = float4(ndc, depth, 1.0f);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Hillaire 2016 "Physically Based & Unified Volumetric Rendering in Frostbite" — exponential fog integration
/// @deviations NONE
///
/// Computes exponential height fog for every pixel and outputs inscattering
/// color + fog opacity for alpha-blended compositing over the lit scene.
///
/// Blend state: SrcAlpha, InvSrcAlpha
///   result = inscatter * fogOpacity + sceneColor * (1 - fogOpacity)
float4 main(FSInput input) : SV_Target
{
    float2 uv = input.texcoord;

    // ── Sample depth ─────────────────────────────────────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float> depthTex = ResourceDescriptorHeap[fogIndices.depthIdx];
    float depth = depthTex.Sample(linearSampler, uv).r;
#else
    float depth = sceneDepth.Sample(linearSampler, uv).r;
#endif

    // ── Reconstruct world position ───────────────────────────────────────
    // Reversed-Z: depth = 1.0 at near plane, depth ~ 0.0 at far plane (sky).
    float3 camPos3 = cameraPos.xyz;
    float3 worldPos;
    float3 CameraToReceiver;
    float  CameraToReceiverLength;
    bool   isSky = (depth <= 0.0001f);

    if (isSky)
    {
        // Sky pixel: no geometry hit. Reconstruct a ray direction using a
        // small depth value (near the near plane in reversed-Z) to get a
        // valid direction vector, then extend to farPlane distance.
        float3 farWorldPos = ReconstructWorldPosition(uv, 0.0002f);
        float3 rayDir = normalize(farWorldPos - camPos3);
        float  skyDist = farPlane;
        CameraToReceiver = rayDir * skyDist;
        CameraToReceiverLength = skyDist;
    }
    else
    {
        worldPos = ReconstructWorldPosition(uv, depth);
        CameraToReceiver = worldPos - camPos3;
        CameraToReceiverLength = length(CameraToReceiver);
    }

    // ── Step 3: MinFogOpacity and MaxWorldObserverHeight ─────────────────
    /// MinFogOpacity is the minimum transmittance (fog factor) —
    /// it prevents fog from becoming fully opaque.
    /// We compute it from fogMaxOpacity (which is 1.0 - MinFogOpacity).
    float MinFogOpacity = 1.0f - fogMaxOpacity;
    float MaxWorldObserverHeight = maxObserverHeight;

    // ── Step 4: Observer height clamping ──────────────────────────────────
    /// Clamp camera Z to MaxWorldObserverHeight.
    float3 WorldObserverOrigin = float3(camPos3.xy, min(camPos3.z, MaxWorldObserverHeight));

    // ── Step 5: End distance (horizontal XY plane clamping) ──────────────
    /// If EndDistance > 0 and the XY distance exceeds it, scale the ray
    /// so that the horizontal distance does not exceed EndDistance.
    /// Not currently exposed in HeightFogCB. This block is ready for when EndDistance is added.
    // (EndDistance is not in our current CB — skip this step for now)

    // ── Step 6: Compensate ray for observer height clamping ──────────────
    /// When the observer is clamped downward, the Z component of the view
    /// vector must be adjusted so the fog integral starts from the clamped
    /// position rather than the true camera position.
    CameraToReceiver.z += camPos3.z - WorldObserverOrigin.z;

    float CameraToReceiverLengthSqr = dot(CameraToReceiver, CameraToReceiver);
    float CameraToReceiverLengthInv = rsqrt(max(CameraToReceiverLengthSqr, 0.00000001f));
    CameraToReceiverLength = CameraToReceiverLengthSqr * CameraToReceiverLengthInv;
    float3 CameraToReceiverNormalized = CameraToReceiver * CameraToReceiverLengthInv;

    // ── Step 7: Initialize ray parameters ────────────────────────────────
    float RayOriginTerms = fogDensity;              // Pre-computed on CPU
    float RayOriginTermsSecond = secondFogDensity;  // Pre-computed on CPU
    float RayLength = CameraToReceiverLength;
    float RayDirectionZ = CameraToReceiver.z;

    // ── Step 8: Start distance exclusion ─────────────────────────────────
    /// When fogStartDistance > 0, fog integration begins at the exclusion
    /// point rather than the camera. The ray is shortened and the density
    /// terms are re-computed at the exclusion intersection height using
    /// the raw (un-precomputed) density values.
    float ExcludeDistance = max(0.0f, fogStartDistance);

    if (ExcludeDistance > 0.0f)
    {
        // Parametric intersection time along the ray at the exclusion distance.
        float ExcludeIntersectionTime = ExcludeDistance * CameraToReceiverLengthInv;

        // Compute Z height at exclusion intersection and remaining Z delta.
        float CameraToExclusionIntersectionZ = ExcludeIntersectionTime * CameraToReceiver.z;
        float ExclusionIntersectionZ = WorldObserverOrigin.z + CameraToExclusionIntersectionZ;
        float ExclusionIntersectionToReceiverZ = CameraToReceiver.z - CameraToExclusionIntersectionZ;

        // Shorten ray to start at exclusion point.
        RayLength = (1.0f - ExcludeIntersectionTime) * CameraToReceiverLength;
        RayDirectionZ = ExclusionIntersectionToReceiverZ;

        // Re-compute primary layer density at the exclusion intersection height.
        float Exponent = max(-127.0f, fogHeightFalloff * (ExclusionIntersectionZ - fogBaseHeight));
        RayOriginTerms = rawDensity * exp2(-Exponent);

        // Re-compute secondary layer density at the exclusion intersection height.
        float ExponentSecond = max(-127.0f, secondFogHeightFalloff * (ExclusionIntersectionZ - secondFogBaseHeight));
        RayOriginTermsSecond = secondFogRawDensity * exp2(-ExponentSecond);
    }

    // ── Step 9: Line integral (primary + secondary layers) ───────────────
    /// Calculate shared line integral — used for both base fog and directional inscattering.
    float ExponentialHeightLineIntegralShared = IntegrateExponentialFogRay(
        fogHeightFalloff, RayDirectionZ, RayOriginTerms
    );

    /// Add second layer's contribution. Guarded by density > 0 check
    /// (runtime equivalent of a compile-time permutation for two-layer fog).
    if (secondFogRawDensity > 0.0f || secondFogDensity > 0.0f)
    {
        ExponentialHeightLineIntegralShared += IntegrateExponentialFogRay(
            secondFogHeightFalloff, RayDirectionZ, RayOriginTermsSecond
        );
    }

    /// Scale by ray length to get total optical depth (tau).
    float ExponentialHeightLineIntegral = ExponentialHeightLineIntegralShared * RayLength;

    // ── Step 10: Inscattering color ──────────────────────────────────────
    /// Base inscattering color from the fog component.
    float3 InscatteringColor = fogColor.xyz;

    /// Sky atmosphere ambient injection: add the sky ambient contribution.
    ///   Inscattering += ColorScale * Contribution * SkyLightColor
    /// This creates the blue/white horizon haze characteristic of atmospheric scattering.
    float3 SkyAmbient = skyAtmContribColorScale.xyz
                      * heightFogContribution
                      * float3(skyLightColorR, skyLightColorG, skyLightColorB);
    InscatteringColor += SkyAmbient;

    // ── Step 11: Directional inscattering ────────────────────────────────
    /// Directional inscattering simulates the sun glow visible through fog.
    /// Uses a cosine-power phase function directed toward the sun, with its
    /// own line integral that can have a different start distance.
    float3 DirectionalInscattering = float3(0.0f, 0.0f, 0.0f);

    {
        float3 sunDirection = float3(sunDirX, sunDirY, sunDirZ);

        /// Build directional inscattering base color.
        /// Adds atmosphere-filtered sun illuminance to the artistic directional color:
        ///   DirectionalColor = DirectionalInscatteringColor
        ///     + HeightFogContribution * PostTransmittanceSunIlluminance
        float3 DirectionalInscatteringBaseColor = directionalInscatteringColor.xyz;
        if (sunIllumPostTransA > 0.0f)
        {
            // Add atmosphere-filtered post-transmittance sun illuminance.
            DirectionalInscatteringBaseColor += heightFogContribution
                * float3(sunIllumPostTransR, sunIllumPostTransG, sunIllumPostTransB);
        }

        /// Power phase function: pow(saturate(dot(viewDir, lightDir)), exponent) * UniformPhase
        /// CameraToReceiverNormalized (not negated) is dotted with the light direction
        /// (which points TOWARD the light). Maximum inscattering occurs when the
        /// camera looks toward the sun.
        float CosAngle = saturate(dot(CameraToReceiverNormalized, sunDirection));
        float Phase = pow(CosAngle, directionalInscatteringExponent) * UNIFORM_PHASE_FUNCTION;

        float3 DirectionalLightInscattering = DirectionalInscatteringBaseColor * Phase;

        /// Directional inscattering uses the shared line integral but with a reduced
        /// ray length (subtracting its own start distance).
        float DirectionalInscatteringStartDist = directionalInscatteringStartDistance;
        float DirExponentialHeightLineIntegral = ExponentialHeightLineIntegralShared
            * max(RayLength - DirectionalInscatteringStartDist, 0.0f);

        /// Transmittance for directional inscattering.
        float DirectionalInscatteringFogFactor = saturate(exp2(-DirExponentialHeightLineIntegral));

        /// Final directional inscattering = color * (1 - transmittance).
        DirectionalInscattering = DirectionalLightInscattering * (1.0f - DirectionalInscatteringFogFactor);
    }

    // ── Step 12: Final fog factor (transmittance) ────────────────────────
    /// ExpFogFactor = max(saturate(exp2(-tau)), MinFogOpacity)
    /// This is the fraction of the scene color that survives through the fog.
    /// MinFogOpacity prevents the fog from becoming fully opaque.
    float ExpFogFactor = max(saturate(exp2(-ExponentialHeightLineIntegral)), MinFogOpacity);

    // ── Step 13: Cutoff distance ─────────────────────────────────────────
    /// Beyond FogCutoffDistance, disable fog entirely (transmittance = 1, no inscattering).
    /// Uses CameraToReceiverLength (before start-distance exclusion) for the distance test.
    [flatten]
    if (fogCutoffDistance > 0.0f && CameraToReceiverLength > fogCutoffDistance)
    {
        ExpFogFactor = 1.0f;
        DirectionalInscattering = float3(0.0f, 0.0f, 0.0f);
    }

    // ── Step 14: Compose final color ─────────────────────────────────────
    /// FogColor = InscatteringColor * (1 - ExpFogFactor) + DirectionalInscattering
    ///
    /// This is the standard inscattering formula:
    ///   inscatter = base_color * opacity + directional_contribution
    /// where opacity = (1 - transmittance).
    float3 FogColor = InscatteringColor * (1.0f - ExpFogFactor) + DirectionalInscattering;

    // ── Step 15: Compute fog opacity for blending ────────────────────────
    /// The output is blended with:
    ///   SrcAlpha (fog opacity) * SrcColor + InvSrcAlpha * DstColor
    /// Which gives:
    ///   result = FogColor * fogOpacity + sceneColor * ExpFogFactor
    ///
    /// But we need FogColor to already include the (1-ExpFogFactor) factor
    /// in its inscattering term. To make this work with SrcAlpha/InvSrcAlpha:
    ///   fogOpacity = 1 - ExpFogFactor
    ///   output.rgb = FogColor / fogOpacity (undo the opacity baked into inscattering)
    ///              = InscatteringColor + DirectionalInscattering / fogOpacity
    ///
    /// Note: some engines output transmittance directly with a One, SrcAlpha blend.
    /// We use SrcAlpha/InvSrcAlpha instead, so we must restructure the output
    /// to divide inscattering by opacity.
    float fogOpacity = 1.0f - ExpFogFactor;

    // Early discard for fully transparent pixels.
    if (fogOpacity < 0.001f)
        discard;

    // For SrcAlpha/InvSrcAlpha blending:
    //   finalColor = output.rgb * output.a + sceneColor * (1 - output.a)
    //   We want: finalColor = FogColor + sceneColor * ExpFogFactor
    //                       = FogColor + sceneColor * (1 - fogOpacity)
    //   So: output.rgb = FogColor / fogOpacity, output.a = fogOpacity
    //   Then: FogColor/fogOpacity * fogOpacity + sceneColor * (1-fogOpacity) = FogColor + sceneColor * ExpFogFactor
    float3 outputColor = FogColor / max(fogOpacity, 0.001f);

    return float4(outputColor, fogOpacity);
}
