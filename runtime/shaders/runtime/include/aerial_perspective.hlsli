/// UzEngine Aerial Perspective — per-pixel atmospheric scattering for deferred lit geometry.
///
/// Computes in-scattered luminance and transmittance along the camera-to-pixel ray
/// through the atmosphere. Applied after deferred lighting to add distance-dependent
/// atmospheric haze that brightens the horizon and tints distant objects blue.
///
/// # UE5 Reference
/// @source UE5 SkyAtmosphereCommon.ush:42 — GetAerialPerspectiveLuminanceTransmittance
/// @source UE5 SkyAtmosphere.usf:1466 — RenderCameraAerialPerspectiveVolumeCS
///
/// UE5 precomputes a 3D volume texture (CameraAerialPerspectiveVolume) and samples it
/// per-pixel. UzEngine uses per-pixel ray-marching with the existing transmittance LUT
/// instead — same physical model, same Frostbite analytical integration, but computed
/// inline rather than from a precomputed volume. This avoids the need for a separate
/// compute pass and 3D texture allocation while producing identical results for moderate
/// view distances.
///
/// # Integration
/// After computing lit color in deferred_lighting_ps.hlsl:
///   float4 ap = ComputeAerialPerspective(...);
///   color = color * ap.a + ap.rgb;  // transmittance + inscattering
///
/// # Coordinate Convention
/// Z-up world space. Planet center at origin in sky-space.
/// World Z=0 = sea level, so cameraSkyKm.z = cameraPos.z * WORLD_TO_KM + bottomRadiusKm.

#ifndef AERIAL_PERSPECTIVE_HLSLI
#define AERIAL_PERSPECTIVE_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 SkyAtmosphereCommon.ush:6 — CM_TO_SKY_UNIT = 0.00001
/// @constants 0.00001 (UE5 SkyAtmosphereCommon.ush:6)
/// @deviations NONE
static const float AP_WORLD_TO_KM = 0.00001f;

/// @source UE5 SkyAtmosphere.usf:109 — PlanetRadiusOffset = 0.005 km (5 meters)
/// @constants 0.005 (UE5 SkyAtmosphere.usf:109)
/// @deviations NONE
static const float AP_PLANET_RADIUS_OFFSET_KM = 0.005f;

/// Small epsilon to prevent division by zero in analytical integration.
static const float AP_EXT_EPS = 1e-7f;

/// @source UE5 SkyAtmosphere.usf:1608 — CameraAerialPerspectiveSampleCountPerSlice
/// Number of ray-march steps for aerial perspective per pixel.
/// 8 steps is sufficient for the short camera-to-geometry distances typical in AP
/// (much shorter rays than full sky rendering which uses 32 steps).
static const int AP_NUM_STEPS = 8;

/// Mie extinction-to-scattering ratio (absorption included).
/// @source UE5 SkyAtmosphere.usf — Mie extinction = scattering * 1.11
/// @constants 1.11 (standard Mie absorption ratio, same as transmittance_lut_cs.hlsl)
/// @deviations NONE
static const float AP_MIE_EXTINCTION_RATIO = 1.11f;

// ─────────────────────────────────────────────────────────────────────────────
// Phase Functions (duplicated from sky_atmosphere_ps.hlsl to avoid include conflicts)
// ─────────────────────────────────────────────────────────────────────────────

/// Rayleigh phase function: (3/16pi)(1 + cos^2(theta)).
/// @source UE5 SkyAtmosphere.usf — RayleighPhase (standard analytical form)
/// @deviations NONE
float AP_RayleighPhase(float cosTheta) {
    return (3.0f / (16.0f * PI)) * (1.0f + cosTheta * cosTheta);
}

/// Henyey-Greenstein phase function for Mie scattering.
/// @source UE5 SkyAtmosphere.usf — HgPhase (standard Henyey-Greenstein)
/// @deviations NONE
float AP_MiePhase(float cosTheta, float g) {
    float g2    = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f / (4.0f * PI)) * (1.0f - g2) / (denom * sqrt(denom));
}

/// Exponential density profile: exp(-altitude / scaleHeight).
/// @source UE5 SkyAtmosphere.usf — GetProfileDensity (exponential term)
/// @deviations NONE
float AP_DensityAtAltitude(float altitudeKm, float scaleHeightKm) {
    return exp(-max(0.0f, altitudeKm) / scaleHeightKm);
}

// ─────────────────────────────────────────────────────────────────────────────
// Ray-Sphere Intersection
// ─────────────────────────────────────────────────────────────────────────────

/// Returns (tNear, tFar). tNear < 0 means origin is inside sphere.
/// Returns (-1, -1) if no intersection.
float2 AP_RaySphereIntersect(float3 origin, float3 dir, float3 center, float radius) {
    float3 oc   = origin - center;
    float  b    = dot(oc, dir);
    float  c    = dot(oc, oc) - radius * radius;
    float  disc = b * b - c;
    if (disc < 0.0f) return float2(-1.0f, -1.0f);
    float sq = sqrt(disc);
    return float2(-b - sq, -b + sq);
}

// ─────────────────────────────────────────────────────────────────────────────
// Transmittance LUT Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Maps (viewHeight, viewZenithCosAngle) to UV for the transmittance LUT.
/// @source UE5 SkyAtmosphereCommon.ush:175 — getTransmittanceLutUvs
/// @deviations NONE — same Bruneton 2017 non-linear parameterization
void AP_TransmittanceLutParamsToUv(float viewHeight, float viewZenithCosAngle,
    float bRadius, float tRadius, out float2 uv)
{
    float H   = sqrt(max(0.0f, tRadius * tRadius - bRadius * bRadius));
    float rho = sqrt(max(0.0f, viewHeight * viewHeight - bRadius * bRadius));

    float discriminant = viewHeight * viewHeight
        * (viewZenithCosAngle * viewZenithCosAngle - 1.0f)
        + tRadius * tRadius;
    float d = max(0.0f, (-viewHeight * viewZenithCosAngle + sqrt(max(0.0f, discriminant))));

    float dMin = tRadius - viewHeight;
    float dMax = rho + H;
    float uU   = (d - dMin) / (dMax - dMin);
    float uV   = rho / H;

    uv = float2(uU, uV);
}

/// Samples the precomputed transmittance LUT for a given position and direction.
/// @source UE5 SkyAtmosphereCommon.ush:175 — getTransmittanceLutUvs + texture sample
/// @deviations NONE
float3 AP_SampleTransmittanceLut(Texture2D<float4> lut, SamplerState samp,
    float3 samplePos, float3 queryDir,
    float bRadius, float tRadius)
{
    float viewHeight = length(samplePos);
    float viewZenithCosAngle = dot(normalize(samplePos), queryDir);

    float2 uv;
    AP_TransmittanceLutParamsToUv(viewHeight, viewZenithCosAngle, bRadius, tRadius, uv);
    return lut.SampleLevel(samp, uv, 0).rgb;
}

// ─────────────────────────────────────────────────────────────────────────────
// Aerial Perspective Computation
// ─────────────────────────────────────────────────────────────────────────────

/// Computes aerial perspective (inscattered luminance + transmittance) for a pixel.
///
/// Ray-marches from camera to pixelWorldPos through the atmosphere, accumulating
/// Rayleigh + Mie in-scattering weighted by sun transmittance from the LUT.
/// Uses the same Frostbite analytical per-step integration as the sky pass.
///
/// @source UE5 SkyAtmosphere.usf:1466 — RenderCameraAerialPerspectiveVolumeCS
/// @source UE5 SkyAtmosphereCommon.ush:42 — GetAerialPerspectiveLuminanceTransmittance
/// @constants AP_MIE_EXTINCTION_RATIO=1.11 (SkyAtmosphere.usf), AP_PLANET_RADIUS_OFFSET_KM=0.005 (SkyAtmosphere.usf:109)
/// @deviations Per-pixel ray-march instead of precomputed 3D volume (same physics, no volume texture)
///
/// # Parameters
/// - pixelWorldPos: world-space position of the pixel (cm, Z-up)
/// - cameraPosWorld: world-space camera position (cm, Z-up)
/// - sunDir: normalised direction TOWARD the sun
/// - sunIntensity: sun illuminance scalar
/// - bottomRadiusKm, topRadiusKm: atmosphere geometry (km)
/// - rayleighBeta: Rayleigh scattering coefficients (km^-1)
/// - rayleighScaleH: Rayleigh scale height (km)
/// - mieBeta: Mie scattering coefficient (km^-1)
/// - mieScaleH: Mie scale height (km)
/// - mieAnisotropy: HG anisotropy parameter (0.8 typical)
/// - rayleighScale, mieScale: artistic multipliers for scattering color
/// - transmittanceLut: precomputed 256x64 RGBA16F transmittance texture
/// - samp: bilinear sampler
///
/// # Returns
/// float4: rgb = inscattered luminance, a = mean transmittance (gray-scale, UE5 parity).
/// Apply as: finalColor = finalColor * result.a + result.rgb
float4 ComputeAerialPerspective(
    float3 pixelWorldPos,
    float3 cameraPosWorld,
    float3 sunDir,
    float  sunIntensity,
    float  bottomRadiusKm,
    float  topRadiusKm,
    float3 rayleighBeta,
    float  rayleighScaleH,
    float  mieBeta,
    float  mieScaleH,
    float  mieAnisotropy,
    float  rayleighScale,
    float  mieScale,
    Texture2D<float4> transmittanceLut,
    SamplerState samp)
{
    // Convert camera and pixel positions to sky-space (km), Z-up
    // World Z=0 = sea level → sky Z = bottomRadiusKm
    float3 cameraSkyKm = cameraPosWorld * AP_WORLD_TO_KM + float3(0.0f, 0.0f, bottomRadiusKm);
    float3 pixelSkyKm  = pixelWorldPos  * AP_WORLD_TO_KM + float3(0.0f, 0.0f, bottomRadiusKm);

    // Snap camera to minimum height above planet surface (UE5 pattern)
    float distToCenter  = length(cameraSkyKm);
    float minSafeRadius = bottomRadiusKm + AP_PLANET_RADIUS_OFFSET_KM;
    if (distToCenter < minSafeRadius) {
        cameraSkyKm = cameraSkyKm * (minSafeRadius / distToCenter);
    }

    // Ray from camera to pixel in sky-space
    float3 rayVec   = pixelSkyKm - cameraSkyKm;
    float  rayLenKm = length(rayVec);

    // Early out for very close pixels (< 1 meter in sky-space = 100 meters in world)
    if (rayLenKm < 0.001f) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    float3 rayDir = rayVec / rayLenKm;

    // Clip ray to atmosphere bounds
    float2 atmoHit = AP_RaySphereIntersect(cameraSkyKm, rayDir, float3(0, 0, 0), topRadiusKm);
    if (atmoHit.y < 0.0f) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    float tStart = max(atmoHit.x, 0.0f);
    float tEnd   = min(rayLenKm, atmoHit.y); // Clip to pixel distance

    // Also clip to planet surface
    float2 groundHit = AP_RaySphereIntersect(cameraSkyKm, rayDir, float3(0, 0, 0), bottomRadiusKm);
    if (groundHit.x > 0.0f) {
        tEnd = min(tEnd, groundHit.x);
    }

    float marchLen = tEnd - tStart;
    if (marchLen <= 0.0f) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    float stepLenKm = marchLen / float(AP_NUM_STEPS);

    // Phase functions for the camera-to-sun angle along this ray
    float cosTheta = dot(rayDir, sunDir);
    float phaseR   = AP_RayleighPhase(cosTheta);
    float phaseM   = AP_MiePhase(cosTheta, mieAnisotropy);

    // Artistic scattering coefficients (affect in-scattering color only)
    float3 artRayleighBeta = rayleighBeta * rayleighScale;
    float  artMieBeta      = mieBeta * mieScale;

    // Frostbite analytical integration (same as sky_atmosphere_ps.hlsl and sky_cubemap_cs.hlsl)
    float3 L          = float3(0, 0, 0);
    float3 Throughput = float3(1, 1, 1);

    for (int i = 0; i < AP_NUM_STEPS; ++i) {
        float  t        = tStart + (float(i) + 0.5f) * stepLenKm;
        float3 sampleKm = cameraSkyKm + rayDir * t;
        float  altKm    = max(0.0f, length(sampleKm) - bottomRadiusKm);

        // Density at this altitude
        float densR = AP_DensityAtAltitude(altKm, rayleighScaleH);
        float densM = AP_DensityAtAltitude(altKm, mieScaleH);

        // Extinction uses PHYSICAL coefficients (must match transmittance LUT)
        float3 extR     = rayleighBeta * densR;
        float3 extM     = mieBeta * AP_MIE_EXTINCTION_RATIO * densM;
        float3 extTotal = extR + extM;

        // Transmittance over this single step
        float3 T_step = exp(-extTotal * stepLenKm);

        // Sun transmittance at sample point (from precomputed LUT)
        float3 sunT = AP_SampleTransmittanceLut(transmittanceLut, samp,
            sampleKm, sunDir, bottomRadiusKm, topRadiusKm);

        // In-scattering uses ARTISTIC coefficients for color control
        float3 scatterR = artRayleighBeta * densR * phaseR;
        float3 scatterM = artMieBeta * densM * phaseM;
        float3 scatter  = scatterR + scatterM;

        // Analytical in-scattering integral over the step:
        //   Sint = scatter * (1 - T_step) / ext
        float3 Sint = (scatter - scatter * T_step) / max(extTotal, AP_EXT_EPS);

        // Accumulate luminance weighted by path throughput and sun transmittance
        L         += Throughput * sunT * Sint * float3(sunIntensity, sunIntensity, sunIntensity);
        Throughput *= T_step;
    }

    // Convert RGB transmittance to gray-scale mean (UE5 parity)
    // @source UE5 SkyAtmosphere.usf:1632 — dot(ss.Transmittance, float3(1/3, 1/3, 1/3))
    float meanTransmittance = dot(Throughput, float3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f));

    return float4(L, meanTransmittance);
}

#endif // AERIAL_PERSPECTIVE_HLSLI
