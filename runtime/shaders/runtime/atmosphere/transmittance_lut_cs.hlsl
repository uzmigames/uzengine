/// Transmittance LUT Compute Shader — Precomputed atmosphere transmittance.
///
/// Encodes: for each (camera_height, sun_zenith_angle) pair, the optical
/// transmittance from that point to the top of the atmosphere along a ray.
///
/// This replaces the per-pixel secondary ray march toward the sun in the sky
/// atmosphere pixel shader with a single texture lookup, dramatically reducing
/// ALU cost for atmospheric rendering.
///
/// # LUT Parameterization (Bruneton 2017)
/// Uses non-linear UV mapping that concentrates precision near the horizon
/// where transmittance changes most rapidly:
///   U = f(ray distance to atmosphere top)
///   V = f(distance from planet center above surface)
///
/// # Integration Model
/// Same density model as sky_atmosphere_ps.hlsl:
///   - Rayleigh: exponential density with betaR (km^-1) and scaleHR (km)
///   - Mie: exponential density with betaM (km^-1) and scaleHM (km)
///   - Mie extinction = 1.11 * Mie scattering coefficient
///
/// # Resources
/// - b0: AtmosphereLutCB (atmosphere parameters, LUT dimensions)
/// - u0: RWTexture2D<float4> output (256x64 RGBA16F)
///
/// # Dispatch
/// [8,8,1] thread groups, dispatch [32,8,1] = 256x64 total threads.

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffer
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants at b0 — 8 DWORDs (32 bytes), matching the root signature
/// slot 0 which is typed as 8x uint32 root constants.
/// No CBV needed — data fits in root constants like the IBL baker.
cbuffer AtmosphereLutParams : register(b0) {
    float3 rayleighBeta;     // Rayleigh scattering coefficients (km^-1)
    float  rayleighScaleH;   // Rayleigh scale height (km)
    float  mieBeta;          // Mie scattering coefficient (km^-1)
    float  mieScaleH;        // Mie scale height (km)
    float  bottomRadiusKm;   // Planet surface radius (km)
    float  topRadiusKm;      // Top of atmosphere radius (km)
};

/// Ozone absorption parameters at b2 — CBV (256-byte aligned).
/// @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — ozone tent distribution
/// Tent distribution for ozone density converted to piecewise-linear coefficients.
cbuffer OzoneParams : register(b2) {
    float3 ozoneAbsorption;  // Ozone absorption extinction (km^-1)
    float  ozoneLayerWidth;  // Boundary altitude between two linear segments (km) = TipAltitude
    float  ozoneLinTerm0;    // Linear coefficient for h < layerWidth
    float  ozoneConstTerm0;  // Constant term for h < layerWidth
    float  ozoneLinTerm1;    // Linear coefficient for h >= layerWidth
    float  ozoneConstTerm1;  // Constant term for h >= layerWidth
};

/// LUT dimensions — hardcoded to match the texture allocation in scene_renderer.cpp.
static const uint LUT_WIDTH  = 256;
static const uint LUT_HEIGHT = 64;

// ─────────────────────────────────────────────────────────────────────────────
// Output UAV
// ─────────────────────────────────────────────────────────────────────────────

RWTexture2D<float4> transmittanceLut : register(u0);

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Number of integration steps along the ray to the atmosphere boundary.
/// 40 steps provides <0.5% error vs reference (Bruneton uses 40-500).
static const int NUM_TRANSMITTANCE_STEPS = 40;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Ray-sphere intersection. Returns (tNear, tFar).
/// tNear < 0 means ray origin is inside sphere.
/// Returns (-1,-1) if no intersection.
float2 RaySphereIntersect(float3 origin, float3 dir, float3 center, float radius)
{
    float3 oc   = origin - center;
    float  b    = dot(oc, dir);
    float  c    = dot(oc, oc) - radius * radius;
    float  disc = b * b - c;
    if (disc < 0.0f) return float2(-1.0f, -1.0f);
    float sq = sqrt(disc);
    return float2(-b - sq, -b + sq);
}

/// Maps UV coordinates back to physical (viewHeight, viewZenithCosAngle) parameters.
///
/// Uses Bruneton 2017 non-linear parameterization that concentrates precision
/// near the horizon where transmittance gradients are steepest.
///
/// # Arguments
/// * `uv` — Normalized UV coordinates [0,1]^2.
/// * `bRadius` — Planet surface radius (km).
/// * `tRadius` — Top-of-atmosphere radius (km).
///
/// # Outputs
/// * `viewHeight` — Distance from planet center (km), in [bottomRadius, topRadius].
/// * `viewZenithCosAngle` — Cosine of zenith angle, in [-1, 1].
void UvToTransmittanceLutParams(float2 uv, float bRadius, float tRadius,
    out float viewHeight, out float viewZenithCosAngle)
{
    // H = distance from ground tangent point to atmosphere top (geometric horizon)
    float H   = sqrt(max(0.0f, tRadius * tRadius - bRadius * bRadius));
    // rho = distance from camera to the ground tangent point
    float rho = H * uv.y;
    viewHeight = sqrt(rho * rho + bRadius * bRadius);

    // Recover the ray distance to atmosphere boundary from U
    float dMin = tRadius - viewHeight;
    float dMax = rho + H;
    float d    = dMin + uv.x * (dMax - dMin);

    // Reconstruct cosine of zenith angle from the quadratic solution
    viewZenithCosAngle = (d == 0.0f)
        ? 1.0f
        : (H * H - rho * rho - d * d) / (2.0f * viewHeight * d);
    viewZenithCosAngle = clamp(viewZenithCosAngle, -1.0f, 1.0f);
}

/// Maps physical (viewHeight, viewZenithCosAngle) to UV coordinates.
///
/// Inverse of UvToTransmittanceLutParams. Used when sampling the LUT
/// from the sky atmosphere pixel shader.
///
/// # Arguments
/// * `viewHeight` — Distance from planet center (km).
/// * `viewZenithCosAngle` — Cosine of zenith angle [-1, 1].
/// * `bRadius` — Planet surface radius (km).
/// * `tRadius` — Top-of-atmosphere radius (km).
///
/// # Outputs
/// * `uv` — Normalized UV coordinates [0,1]^2 for LUT sampling.
void TransmittanceLutParamsToUv(float viewHeight, float viewZenithCosAngle,
    float bRadius, float tRadius, out float2 uv)
{
    float H   = sqrt(max(0.0f, tRadius * tRadius - bRadius * bRadius));
    float rho = sqrt(max(0.0f, viewHeight * viewHeight - bRadius * bRadius));

    // Solve for ray distance to atmosphere top along the zenith direction
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

/// Computes ozone density at a given altitude using the reference tent distribution.
/// @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — ozone piecewise-linear density
/// @constants ozoneLayerWidth=25.0, ozoneLinTerm0=1/15
/// @deviations NONE
float OzoneDensity(float altKm)
{
    return saturate(altKm < ozoneLayerWidth
        ? ozoneLinTerm0 * altKm + ozoneConstTerm0
        : ozoneLinTerm1 * altKm + ozoneConstTerm1);
}

/// Computes optical transmittance from a point to the top of the atmosphere.
///
/// Integrates extinction (Rayleigh + Mie + Ozone) along a ray from `pos`
/// in direction `dir` until it exits the atmosphere sphere or hits the planet.
/// Uses midpoint quadrature with NUM_TRANSMITTANCE_STEPS samples.
///
/// @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — ComputeTransmittance
/// @constants Mie extinction ratio = 1.11 (MieExtinction/MieScattering, standard convention)
/// @deviations NONE
///
/// # Arguments
/// * `pos` — Start position in sky-space (km), relative to planet center at origin.
/// * `dir` — Normalized ray direction.
/// * `bRadius` — Planet surface radius (km).
/// * `tRadius` — Top-of-atmosphere radius (km).
/// * `betaR` — Rayleigh scattering coefficients (km^-1).
/// * `scaleHR` — Rayleigh scale height (km).
/// * `betaM_` — Mie scattering coefficient (km^-1).
/// * `scaleHM` — Mie scale height (km).
///
/// # Returns
/// RGB transmittance in [0,1]^3. Returns (1,1,1) if the ray has zero length.
float3 ComputeTransmittanceToTopAtmosphere(float3 pos, float3 dir,
    float bRadius, float tRadius,
    float3 betaR, float scaleHR, float betaM_, float scaleHM)
{
    // Find intersection with top-of-atmosphere sphere
    float2 hit = RaySphereIntersect(pos, dir, float3(0, 0, 0), tRadius);
    float tMax = max(0.0f, hit.y);

    // Clip to planet surface if the ray hits the ground
    float2 groundHit = RaySphereIntersect(pos, dir, float3(0, 0, 0), bRadius);
    if (groundHit.x > 0.0f) tMax = min(tMax, groundHit.x);

    // No atmosphere to traverse
    if (tMax <= 0.0f) return float3(1, 1, 1);

    float dt = tMax / float(NUM_TRANSMITTANCE_STEPS);
    float3 opticalDepth = float3(0, 0, 0);

    // Midpoint quadrature: sample at center of each interval
    for (int i = 0; i < NUM_TRANSMITTANCE_STEPS; ++i) {
        float t   = (float(i) + 0.5f) * dt;
        float3 p  = pos + dir * t;
        float altKm = max(0.0f, length(p) - bRadius);

        float densR = exp(-altKm / scaleHR);
        float densM = exp(-altKm / scaleHM);

        // @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — ozone density (tent distribution)
        float densO = OzoneDensity(altKm);

        // @ref Bruneton 2008 "Precomputed Atmospheric Scattering"
        // Total extinction = Rayleigh + Mie (1.11x for absorption) + Ozone absorption
        // Mie extinction = 1.11 * Mie scattering (same ratio as sky_atmosphere_ps.hlsl)
        opticalDepth += (betaR * densR + betaM_ * 1.11f * densM + ozoneAbsorption * densO) * dt;
    }

    return exp(-opticalDepth);
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 coord = dispatchThreadId.xy;

    // Out-of-bounds guard for non-power-of-two LUT dimensions
    if (coord.x >= LUT_WIDTH || coord.y >= LUT_HEIGHT) return;

    // Map texel center to normalized UV
    float2 uv = (float2(coord) + 0.5f) / float2(LUT_WIDTH, LUT_HEIGHT);

    // Decode UV into physical atmosphere parameters
    float viewHeight, viewZenithCosAngle;
    UvToTransmittanceLutParams(uv, bottomRadiusKm, topRadiusKm,
        viewHeight, viewZenithCosAngle);

    // Construct ray origin and direction in sky-space
    // UzEngine is Z-up: camera is along the Z axis at viewHeight from planet center.
    // Direction is in the Z-X plane (azimuthally symmetric).
    float3 pos = float3(0.0f, 0.0f, viewHeight);
    float sinTheta = sqrt(max(0.0f, 1.0f - viewZenithCosAngle * viewZenithCosAngle));
    float3 dir = float3(sinTheta, 0.0f, viewZenithCosAngle);

    float3 transmittance = ComputeTransmittanceToTopAtmosphere(
        pos, dir, bottomRadiusKm, topRadiusKm,
        rayleighBeta, rayleighScaleH, mieBeta, mieScaleH);

    transmittanceLut[coord] = float4(transmittance, 1.0f);
}
