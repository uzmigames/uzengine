/// Volumetric Cloud Ray March Compute Shader — Main cloud density/lighting integration.
///
/// Traces a ray through the cloud layer volume for each pixel at half-resolution,
/// sampling 3D shape and detail noise textures modulated by a 2D weather map.
/// Light contributions use Beer-Lambert transmittance with dual-lobe Henyey-Greenstein
/// phase function and Frostbite-style analytical scattering integration.
///
/// @source UE5 VolumetricCloud.usf:231-253 — RayIntersectSphereSolution
/// @source UE5 VolumetricCloud.usf:329-338 — SamplePhaseFunction (dual-lobe HG + blend)
/// @source UE5 VolumetricCloud.usf:376-404 — SetupParticipatingMediaContext (multi-scatter octaves)
/// @source UE5 VolumetricCloud.usf:840-1455 — MainCommon tracing loop
/// @source UE5 VolumetricCloud.usf:1402-1410 — Frostbite scattering integration (slide 28)
/// @source UE5 VolumetricCloud.usf:1449 — StopTracingTransmittanceThreshold early exit
/// @source UE5 ParticipatingMediaCommon.ush:85-88 — IsotropicPhase = 1/(4*PI)
/// @source UE5 ParticipatingMediaCommon.ush:91-98 — HenyeyGreensteinPhase (PBRT reference)
/// @source UE5 VolumetricCloudRendering.cpp:60-63 — ViewRaySampleMaxCount default=768
/// @source UE5 VolumetricCloudRendering.cpp:50-53 — SampleMinCount default=2
/// @source UE5 VolumetricCloudRendering.cpp:1692 — StopTracingTransmittanceThreshold from proxy
/// @source UE5 VolumetricCloudRendering.cpp:1679 — SkyLightCloudBottomVisibility = 1 - occlusion
/// @source Schneider & Vos 2015 — "Real-Time Volumetric Cloudscapes of Horizon Zero Dawn"
///         slides 23-26 (noise channel layout), slide 70 (density remapping)
/// @source Hillaire 2016 — "Physically Based and Unified Volumetric Rendering in Frostbite"
///         slide 28 (analytical scattering integration formula)
///
/// @constants
///   IsotropicPhase = 1/(4*PI)             (UE5 ParticipatingMediaCommon.ush:87)
///   HG denom = 1+G^2+2*G*cos             (UE5 ParticipatingMediaCommon.ush:96)
///   PhaseG clamp = -0.999..0.999          (UE5 VolumetricCloud.usf:331)
///   PhaseBlend clamp = 0..1               (UE5 VolumetricCloud.usf:333)
///   SafeExtinctionThreshold = 0.000001    (UE5 VolumetricCloud.usf:1405)
///   ViewRaySampleMaxCount = 768           (UE5 VolumetricCloudRendering.cpp:61)
///   Shape noise FBM weights 0.625/0.25/0.125 (Schneider 2015 slide 26)
///   Detail noise FBM weights 0.625/0.25/0.125 (Schneider 2015 slide 26)
///   Detail erosion factor 0.35            (Schneider 2015 slide 70)
///
/// @deviations
///   - UE5 evaluates cloud density through a full material graph; we use procedural
///     noise sampling (shape + detail + weather map) following Schneider 2015.
///   - UE5 default max sample count is 768; we allow runtime configuration via CB
///     (typical real-time value: 64-128 for 60fps).
///   - UE5 uses per-sample atmosphere transmittance LUT; we omit that for simplicity.
///   - UE5 supports multi-scattering octaves (MSCOUNT); we use a single-octave
///     approximation with ambient term following Schneider 2015 depth probability.
///   - Wind/noise UV scaling uses artist-friendly km-based parameters rather than
///     UE5's material graph driven transforms.


// ─── Samplers ───────────────────────────────────────────────────────────────
SamplerState LinearClampSampler : register(s0);
SamplerState LinearWrapSampler  : register(s1);


// ─── Constant Buffer ────────────────────────────────────────────────────────

/// @source UE5 VolumetricCloudRendering.cpp:1664-1700 — CloudGlobalShaderParams layout
cbuffer CloudRaymarchCB : register(b2)
{
    float4x4 invViewProj;           ///< Camera inverse view-projection matrix
    float3   cameraPos;             ///< Camera world position (km)
    float    time;                  ///< Elapsed time for wind animation (seconds)
    float3   sunDir;                ///< Normalized sun direction (world space)
    float    layerBottomKm;         ///< Cloud layer bottom altitude (km above planet center)
    float3   sunColor;              ///< Sun color * intensity (pre-exposed luminance)
    float    layerHeightKm;         ///< Cloud layer thickness (km)
    float3   windVelocity;          ///< Wind direction * speed (km/s)
    float    coverage;              ///< Global cloud coverage [0,1]
    float    density;               ///< Cloud density multiplier
    float    extinction;            ///< Extinction coefficient (1/km)
    float    scatteringAlbedo;      ///< Single-scattering albedo [0,1]
    float    phaseG;                ///< HG phase eccentricity (forward lobe, e.g. 0.8)
    float    phaseG2;               ///< HG phase eccentricity (back lobe, e.g. -0.3)
    float    phaseBlend;            ///< Blend factor between HG lobes [0,1]
    float    detailWindScale;       ///< Detail noise wind speed multiplier
    float    ambientMultiplier;     ///< Multi-scatter ambient intensity
    uint     primarySteps;          ///< Number of primary ray march steps (e.g. 64)
    uint     lightSteps;            ///< Number of light march steps per sample (e.g. 6)
    uint     shapeNoiseSrv;         ///< Bindless SRV index: 3D shape noise (128^3 RGBA8)
    uint     detailNoiseSrv;        ///< Bindless SRV index: 3D detail noise (32^3 RGBA8)
    uint     weatherMapSrv;         ///< Bindless SRV index: 2D weather map (512x512 RGBA8)
    uint     depthSrv;              ///< Bindless SRV index: depth buffer
    float    outputWidth;           ///< Output texture width (half-res)
    float    outputHeight;          ///< Output texture height (half-res)
    float3   ambientColor;          ///< Sky ambient color for multi-scatter approximation
    float    _pad;
};


// ─── Output ─────────────────────────────────────────────────────────────────
RWTexture2D<float4> cloudOutput : register(u0); ///< RGBA16F half-res cloud color + alpha


// ═══════════════════════════════════════════════════════════════════════════
// Utility Functions
// ═══════════════════════════════════════════════════════════════════════════

/// Remap a value from one range to another.
/// @source Schneider & Vos 2015 slide 70 — cloud density remapping function
/// @constants NONE (pure math)
/// @deviations NONE
float Remap(float value, float oldMin, float oldMax, float newMin, float newMax)
{
    return newMin + (saturate((value - oldMin) / (oldMax - oldMin))) * (newMax - newMin);
}


/// Ray-sphere intersection returning entry/exit distances along the ray.
/// Returns false if the ray misses the sphere entirely.
///
/// @source UE5 VolumetricCloud.usf:231-253 — RayIntersectSphereSolution
/// @constants QuadraticCoef.x = dot(d,d), .y = 2*dot(d,o-c), .z = dot(o-c,o-c) - r^2
///            discriminant = b^2 - 4ac, solutions = (-b +/- sqrt(disc)) / 2a
/// @deviations NONE — exact same quadratic solver
bool RaySphereIntersect(float3 rayOrigin, float3 rayDir, float3 sphereCenter, float radius,
                        out float tMin, out float tMax)
{
    tMin = -1.0;
    tMax = -1.0;

    float3 localPos = rayOrigin - sphereCenter;
    float localPosSqr = dot(localPos, localPos);

    /// Quadratic coefficients: at^2 + bt + c = 0
    float a = dot(rayDir, rayDir);
    float b = 2.0 * dot(rayDir, localPos);
    float c = localPosSqr - radius * radius;

    float discriminant = b * b - 4.0 * a * c;

    if (discriminant < 0.0)
        return false;

    float sqrtDisc = sqrt(discriminant);
    float invTwoA = 1.0 / (2.0 * a);
    tMin = (-b - sqrtDisc) * invTwoA;
    tMax = (-b + sqrtDisc) * invTwoA;
    return true;
}


/// Henyey-Greenstein phase function.
/// Follows PBRT convention: CosTheta = dot(wi, wo) where wi is incoming light dir.
///
/// @source UE5 ParticipatingMediaCommon.ush:91-98 — HenyeyGreensteinPhase
/// @constants Numer = 1 - G^2, Denom = 1 + G^2 + 2*G*CosTheta
///            result = Numer / (4 * PI * Denom * sqrt(Denom))
/// @deviations NONE — exact UE5 formula
float HenyeyGreenstein(float G, float cosTheta)
{
    float numer = 1.0 - G * G;
    float denom = 1.0 + G * G + 2.0 * G * cosTheta;
    return numer / (4.0 * 3.14159265358979 * denom * sqrt(denom));
}


/// Dual-lobe phase function blending two HG lobes, matching UE5's cloud phase.
///
/// @source UE5 VolumetricCloud.usf:329-338 — SamplePhaseFunction
/// @constants PhaseG clamped to [-0.999, 0.999] (line 331)
///            PhaseG2 clamped to [-0.999, 0.999] (line 332)
///            PhaseBlend clamped to [0, 1] (line 333)
///            cosTheta negated (line 334 comment: "negate due to WorldDir being 'in' direction")
///            result = HG(G, -cos) + blend * (HG(G2, -cos) - HG(G, -cos))
/// @deviations NONE — exact UE5 formula
float SamplePhaseFunction(float phaseCosTheta, float g1, float g2, float blend)
{
    g1    = clamp(g1,    -0.999, 0.999);
    g2    = clamp(g2,    -0.999, 0.999);
    blend = clamp(blend,  0.0,   1.0);

    /// UE5 negates cosTheta because ray direction is an "incoming" direction
    float hg0 = HenyeyGreenstein(g1, -phaseCosTheta);
    float hg1 = HenyeyGreenstein(g2, -phaseCosTheta);
    return hg0 + blend * (hg1 - hg0);
}


// ═══════════════════════════════════════════════════════════════════════════
// Cloud Density Sampling
// ═══════════════════════════════════════════════════════════════════════════

/// Sample cloud density at a world position using shape noise, weather map, and detail erosion.
///
/// @source Schneider & Vos 2015 slides 23-26 — noise channel layout and FBM weights
/// @source Schneider & Vos 2015 slide 70 — density remapping
/// @constants
///   Shape FBM weights: G*0.625 + B*0.25 + A*0.125 (Schneider 2015 slide 26)
///   Detail FBM weights: R*0.625 + G*0.25 + B*0.125 (Schneider 2015 slide 26)
///   Detail erosion scale: 0.35 (Schneider 2015 slide 70)
///   Shape noise UV scale: 0.0002 per km (artist-tunable, converts km->tiling space)
///   Weather UV scale: 0.00005 per km (artist-tunable, large-scale coverage)
///   Detail UV scale: 0.001 per km (artist-tunable, fine erosion)
/// @deviations
///   UV scale factors are engine-specific (UE5 uses material graph transforms).
///   Noise textures are generated procedurally; UE5 uses pre-baked assets.
float SampleCloudDensity(float3 posKm, float heightFraction)
{
    /// Bindless texture access
    Texture3D<float4> shapeNoiseTex  = ResourceDescriptorHeap[shapeNoiseSrv];
    Texture2D<float4> weatherMapTex  = ResourceDescriptorHeap[weatherMapSrv];
    Texture3D<float4> detailNoiseTex = ResourceDescriptorHeap[detailNoiseSrv];

    /// Shape noise: sample at world position with wind animation
    /// UV scale 0.0002 converts km coordinates to [0..1] tiling range for a 128^3 volume
    float3 windOffset = windVelocity * time;
    float3 shapeUV = (posKm + windOffset) * 0.0002;
    float4 shapeNoise = shapeNoiseTex.SampleLevel(LinearWrapSampler, shapeUV, 0);

    /// Perlin-Worley composite: remap R channel using GBA FBM
    /// @source Schneider & Vos 2015 slide 25 — remap(perlinWorley, worleyFBM - 1, 1, 0, 1)
    /// FBM weights: 0.625, 0.25, 0.125 sum to 1.0
    float worleyFBM = shapeNoise.g * 0.625 + shapeNoise.b * 0.25 + shapeNoise.a * 0.125;
    float shapeSample = Remap(shapeNoise.r, worleyFBM - 1.0, 1.0, 0.0, 1.0);

    /// Weather map: large-scale coverage and cloud type
    /// UV scale 0.00005 gives ~20km weather tile period
    float2 weatherUV = (posKm.xy + windOffset.xy) * 0.00005;
    float4 weather = weatherMapTex.SampleLevel(LinearWrapSampler, weatherUV, 0);
    float weatherCoverage = weather.r;

    /// Apply global coverage control
    /// Total coverage is weather map modulated by global coverage slider
    float totalCoverage = weatherCoverage * coverage;

    /// Density remapping: erode shape by coverage
    /// @source Schneider 2015 slide 70 — density = remap(shape, 1-coverage, 1, 0, 1)
    float coveredDensity = Remap(shapeSample, 1.0 - totalCoverage, 1.0, 0.0, 1.0);

    /// Height-based density gradient: reduce density at layer edges
    /// Simple parabolic falloff: thicker in middle, thinner at top/bottom
    float heightGradient = saturate(Remap(heightFraction, 0.0, 0.1, 0.0, 1.0))
                         * saturate(Remap(heightFraction, 0.9, 1.0, 1.0, 0.0));
    coveredDensity *= heightGradient;

    /// Detail erosion: only computed when base density is positive (performance)
    if (coveredDensity > 0.0)
    {
        /// UV scale 0.001 gives finer detail tiling (1km period)
        float3 detailUV = (posKm + windOffset * detailWindScale) * 0.001;
        float4 detailNoise = detailNoiseTex.SampleLevel(LinearWrapSampler, detailUV, 0);

        /// Detail FBM: same 0.625/0.25/0.125 weights as shape
        float detailFBM = detailNoise.r * 0.625 + detailNoise.g * 0.25 + detailNoise.b * 0.125;

        /// Erode edges: 0.35 erosion factor from Schneider 2015 slide 70
        coveredDensity = Remap(coveredDensity, detailFBM * 0.35, 1.0, 0.0, 1.0);
    }

    return max(0.0, coveredDensity * density);
}


// ═══════════════════════════════════════════════════════════════════════════
// Light March
// ═══════════════════════════════════════════════════════════════════════════

/// March toward the sun to compute transmittance (shadow) at a cloud sample point.
/// Uses non-linear sample distribution (exponential spacing) to concentrate
/// samples near the sample point where density variation matters most.
///
/// @source UE5 VolumetricCloud.usf:1068-1154 — shadow ray march (non-linear t^2 distribution)
/// @source UE5 VolumetricCloud.usf:1101-1113 — non-linear shadow sample distribution
/// @constants
///   Shadow march uses t^2 distribution: CurrentNormT = ShadowT * ShadowT
///   (UE5 VolumetricCloud.usf:1106)
/// @deviations
///   UE5 shadow march length is driven by ShadowTracingMaxDistance parameter;
///   we use a fixed 1km reach appropriate for our layer height scale.
///   UE5 multi-scatters across MSCOUNT octaves; we accumulate single extinction.
float LightMarch(float3 posKm, float heightFraction)
{
    /// Shadow march distance: reach across a significant portion of the cloud layer
    float shadowLengthKm = min(1.0, layerHeightKm);
    float opticalDepth = 0.0;

    /// Non-linear sample distribution (t^2) concentrates samples near origin
    /// @source UE5 VolumetricCloud.usf:1101-1106
    float invStepCount = 1.0 / float(lightSteps);
    float previousNormT = 0.0;

    for (uint i = 0; i < lightSteps; ++i)
    {
        float shadowT = float(i + 1) * invStepCount;

        /// Non-linear distribution: t^2
        /// @source UE5 VolumetricCloud.usf:1106 — CurrentNormT = ShadowT * ShadowT
        float currentNormT = shadowT * shadowT;
        float deltaNormT = currentNormT - previousNormT;

        /// Sample at midpoint of this non-linear segment
        float sampleDist = shadowLengthKm * (previousNormT + deltaNormT * 0.5);
        float3 samplePos = posKm + sunDir * sampleDist;
        previousNormT = currentNormT;

        /// Compute height fraction at shadow sample
        float sampleAlt = length(samplePos) - layerBottomKm;
        float sampleHeightFrac = saturate(sampleAlt / layerHeightKm);

        /// Skip if outside cloud layer
        if (sampleHeightFrac <= 0.0 || sampleHeightFrac >= 1.0)
            break;

        float sampleDensity = SampleCloudDensity(samplePos, sampleHeightFrac);

        /// Accumulate optical depth with non-linear segment weight
        /// @source UE5 VolumetricCloud.usf:1146 — ExtinctionAcc[ms] += ... * ExtinctionFactor
        opticalDepth += sampleDensity * extinction * shadowLengthKm * deltaNormT;
    }

    /// Beer-Lambert transmittance
    return exp(-opticalDepth);
}


// ═══════════════════════════════════════════════════════════════════════════
// Main Compute Shader
// ═══════════════════════════════════════════════════════════════════════════

[numthreads(8, 8, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    /// Bounds check: skip threads outside output dimensions
    if (DTid.x >= (uint)outputWidth || DTid.y >= (uint)outputHeight)
        return;

    /// ─── Step 1: Reconstruct view ray from screen UV ────────────────────
    float2 uv = (float2(DTid) + 0.5) / float2(outputWidth, outputHeight);
    /// NDC: x[-1,1], y[-1,1] (flip Y for DX convention)
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;

    /// Unproject near/far points to world space
    float4 nearClip = mul(float4(ndc, 0.0, 1.0), invViewProj);
    float4 farClip  = mul(float4(ndc, 1.0, 1.0), invViewProj);
    nearClip.xyz /= nearClip.w;
    farClip.xyz  /= farClip.w;

    float3 rayOrigin = cameraPos;
    float3 rayDir    = normalize(farClip.xyz - nearClip.xyz);

    /// ─── Step 2: Sample depth buffer to limit tracing ───────────────────
    Texture2D<float> depthTex = ResourceDescriptorHeap[depthSrv];
    float deviceZ = depthTex.SampleLevel(LinearClampSampler, uv, 0);

    /// Reconstruct scene world position from depth
    float4 sceneClip = mul(float4(ndc, deviceZ, 1.0), invViewProj);
    sceneClip.xyz /= sceneClip.w;
    float sceneDistKm = length(sceneClip.xyz - cameraPos);

    /// ─── Step 3: Intersect ray with cloud layer spheres ─────────────────
    /// Cloud layer defined as two concentric spheres around planet center (0,0,0)
    /// Bottom sphere: radius = layerBottomKm
    /// Top sphere: radius = layerBottomKm + layerHeightKm
    float3 planetCenter = float3(0.0, 0.0, 0.0);
    float topRadius    = layerBottomKm + layerHeightKm;
    float bottomRadius = layerBottomKm;

    float tTopMin, tTopMax, tBotMin, tBotMax;
    bool hitTop = RaySphereIntersect(rayOrigin, rayDir, planetCenter, topRadius,    tTopMin, tTopMax);
    bool hitBot = RaySphereIntersect(rayOrigin, rayDir, planetCenter, bottomRadius, tBotMin, tBotMax);

    if (!hitTop)
    {
        /// No intersection with cloud volume at all
        cloudOutput[DTid] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    /// Compute entry/exit t values following UE5 logic
    /// @source UE5 VolumetricCloud.usf:536-553
    float tEntry, tExit;
    if (hitBot)
    {
        float tempTop = all(float2(tTopMin, tTopMax) > 0.0)
            ? min(tTopMin, tTopMax) : max(tTopMin, tTopMax);
        float tempBot = all(float2(tBotMin, tBotMax) > 0.0)
            ? min(tBotMin, tBotMax) : max(tBotMin, tBotMax);

        /// If both bottom intersections are in front, we are outside looking at layer
        if (tBotMin > 0.0 && tBotMax > 0.0)
        {
            tempTop = max(0.0, min(tTopMin, tTopMax));
        }

        tEntry = min(tempBot, tempTop);
        tExit  = max(tempBot, tempTop);
    }
    else
    {
        /// Only intersecting top sphere: camera is inside layer or looking through it
        tEntry = tTopMin;
        tExit  = tTopMax;
    }

    tEntry = max(0.0, tEntry);
    tExit  = max(0.0, tExit);

    /// Skip if zero-length segment or cloud is behind depth buffer
    /// @source UE5 VolumetricCloud.usf:578
    if (tExit <= tEntry)
    {
        cloudOutput[DTid] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    /// Clamp exit to depth buffer (don't trace behind opaque geometry)
    /// @source UE5 VolumetricCloud.usf:643
    if (sceneDistKm < tEntry)
    {
        /// Scene is entirely in front of cloud layer
        cloudOutput[DTid] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    tExit = min(tExit, sceneDistKm);

    /// ─── Step 4: Setup march parameters ─────────────────────────────────
    /// @source UE5 VolumetricCloud.usf:716-719 — step count and step size
    uint stepCount = max(2u, primarySteps);
    float stepT = (tExit - tEntry) / float(stepCount);
    float dtKm = stepT; /// step size in kilometers

    /// Phase function: compute once per pixel (per-pixel phase, not per-sample)
    /// @source UE5 VolumetricCloud.usf:771-788
    float phaseCosTheta = dot(sunDir, rayDir);
    float phase = SamplePhaseFunction(phaseCosTheta, phaseG, phaseG2, phaseBlend);

    /// Sky light bottom visibility: reduce ambient at bottom of cloud layer
    /// @source UE5 VolumetricCloud.usf:959 — SkyLightCloudBottomVisibility + NormAltitude
    /// We use 0.2 as a reasonable default for SkyLightCloudBottomVisibility
    /// (UE5 default is 1.0 - CloudProxy.SkyLightCloudBottomOcclusion, typically ~0.3)
    static const float kSkyLightCloudBottomVisibility = 0.3;

    /// Scattering coefficient = albedo * extinction
    /// @source UE5 VolumetricCloud.usf:378 — ScatteringCoefficients = BaseAlbedo * BaseExtinction
    float scatteringCoeff = scatteringAlbedo * extinction;

    /// ─── Step 5: Ray march loop ─────────────────────────────────────────
    float3 luminance = float3(0.0, 0.0, 0.0);
    float transmittanceToView = 1.0;

    /// Start at half-step offset to center samples within their intervals
    /// @source UE5 VolumetricCloud.usf:803 — t = TMin + 0.5 * StepT
    float t = tEntry + 0.5 * stepT;

    for (uint i = 0; i < stepCount; ++i)
    {
        float3 samplePosKm = rayOrigin + t * rayDir;

        /// Compute normalized altitude within cloud layer [0,1]
        float altitudeKm = length(samplePosKm) - layerBottomKm;
        float heightFraction = saturate(altitudeKm / layerHeightKm);

        /// Skip samples outside the cloud layer
        /// @source UE5 VolumetricCloud.usf:898-904
        if (heightFraction <= 0.0 || heightFraction >= 1.0)
        {
            t += stepT;
            continue;
        }

        /// Sample cloud density at this position
        float sampleDensity = SampleCloudDensity(samplePosKm, heightFraction);

        if (sampleDensity > 0.0)
        {
            /// ─── Compute extinction for this step ───────────────────────
            float sampleExtinction = extinction * sampleDensity;
            float sampleScattering = scatteringCoeff * sampleDensity;

            /// ─── Light march: compute transmittance toward sun ──────────
            float transmittanceToSun = LightMarch(samplePosKm, heightFraction);

            /// ─── Scattered luminance computation ────────────────────────

            /// Direct sun illumination through cloud self-shadow
            /// @source UE5 VolumetricCloud.usf:1372 — TransmittanceToLight0 * Light0Illuminance * Phase
            float3 sunScattered = transmittanceToSun * sunColor * phase;

            /// Distant sky light (ambient) with bottom-of-cloud visibility falloff
            /// @source UE5 VolumetricCloud.usf:959
            /// @source UE5 VolumetricCloud.usf:1381 — DistantLightLuminance added for ms==0 only
            float ambientVisibility = saturate(kSkyLightCloudBottomVisibility + heightFraction);
            float3 distantSkyLight = ambientColor * ambientMultiplier * ambientVisibility;

            /// Combined in-scattered luminance
            /// @source UE5 VolumetricCloud.usf:1390
            /// ScatteredLuminance = SunSkyLuminance * ScatteringCoefficients + EmissiveLuminance
            float3 scatteredLuminance = (sunScattered + distantSkyLight) * sampleScattering;

            /// ─── Frostbite analytical scattering integration ────────────
            /// @source UE5 VolumetricCloud.usf:1403-1410
            /// "Improved scattering integration. See slide 28 of
            ///  Physically Based and Unified Volumetric Rendering in Frostbite"
            ///
            /// SafeExtinctionCoefficients = max(threshold, extinction)
            /// SafePathSegmentTransmittance = exp(-safeExtinction * dt)
            /// LuminanceIntegral = (S - S * T) / safeExtinction
            /// LuminanceContribution = TransmittanceToView * LuminanceIntegral

            /// @source UE5 VolumetricCloud.usf:1405 — SafeExtinctionThreshold = 0.000001
            static const float kSafeExtinctionThreshold = 0.000001;
            float safeExtinction = max(kSafeExtinctionThreshold, sampleExtinction);
            float safePathTransmittance = exp(-safeExtinction * dtKm);

            /// Analytical integral: (S - S*T) / sigma_t
            /// This is the exact integral of constant in-scattering over an exponentially
            /// decaying segment, avoiding the dt*S approximation that causes energy loss.
            float3 luminanceIntegral = (scatteredLuminance - scatteredLuminance * safePathTransmittance) / safeExtinction;

            /// Accumulate with current transmittance-to-view
            /// @source UE5 VolumetricCloud.usf:1409-1410
            luminance += transmittanceToView * luminanceIntegral;

            /// Update transmittance
            /// @source UE5 VolumetricCloud.usf:1440
            transmittanceToView *= safePathTransmittance;
        }

        /// Early exit when cloud is nearly opaque
        /// @source UE5 VolumetricCloud.usf:1449 — StopTracingTransmittanceThreshold
        /// UE5 default threshold comes from CloudProxy; typical value is 0.01-0.03
        static const float kStopTracingThreshold = 0.01;
        if (transmittanceToView < kStopTracingThreshold)
            break;

        t += stepT;
    }

    /// ─── Step 6: Output premultiplied alpha ─────────────────────────────
    /// UE5 cloud output format: RGB = luminance (premultiplied), A = transmittance
    /// @source UE5 VolumetricCloud.usf:482 — OutColor0 = float4(0, 0, 0, 1) init
    /// Our convention: A = 1 - transmittance (opacity) for standard over-compositing
    float alpha = 1.0 - transmittanceToView;
    cloudOutput[DTid] = float4(luminance, alpha);
}
