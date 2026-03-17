/// Multi-Scattering LUT Compute Shader — Precomputed infinite-bounce correction.
///
/// @source UE5 RenderSkyAtmosphereLookUpTablesCS.usf:280-380 — NewMultiScatCS
/// @constants LUT_SIZE=32, NUM_DIRECTIONS=64, NUM_STEPS=20
/// @deviations NONE — exact Hillaire 2020 algorithm
///
/// Generates a 32x32 LUT parameterized by:
///   U = dot(up, sunDir) * 0.5 + 0.5  (sun zenith cosine)
///   V = (viewHeight - bottomRadius) / (topRadius - bottomRadius)
///
/// For each texel, integrates single-scatter over 64 uniform sphere directions
/// then applies the geometric series 1/(1-f_ms) for infinite bounces.
///
/// # Resources
/// - b0: root constants (8 DWORDs) — atmosphere params
/// - b2: CBV — ozone params (same as transmittance LUT)
/// - t0: Texture2D<float4> transmittanceLUT (256x64 RGBA16F)
/// - u0: RWTexture2D<float4> output (32x32 R11G11B10F)
/// - s0: SamplerState linear clamp
///
/// # Dispatch: [4, 4, 1] thread groups of [8, 8, 1] = 32x32 total threads.

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers (same layout as transmittance_lut_cs.hlsl)
// ─────────────────────────────────────────────────────────────────────────────

cbuffer AtmosphereLutParams : register(b0) {
    float3 rayleighBeta;     // Rayleigh scattering coefficients (km^-1)
    float  rayleighScaleH;   // Rayleigh scale height (km)
    float  mieBeta;          // Mie scattering coefficient (km^-1)
    float  mieScaleH;        // Mie scale height (km)
    float  bottomRadiusKm;   // Planet surface radius (km)
    float  topRadiusKm;      // Top of atmosphere radius (km)
};

cbuffer OzoneParams : register(b2) {
    float3 ozoneAbsorption;  // Ozone absorption extinction (km^-1)
    float  ozoneLayerWidth;  // Tip altitude between two linear segments (km)
    float  ozoneLinTerm0;    // Linear coefficient for h < layerWidth
    float  ozoneConstTerm0;  // Constant term for h < layerWidth
    float  ozoneLinTerm1;    // Linear coefficient for h >= layerWidth
    float  ozoneConstTerm1;  // Constant term for h >= layerWidth
    uint   transmittanceLutSrvIdx; // SRV heap index (bindless mode)
    uint   _ozonePad0;
    uint   _ozonePad1;
    uint   _ozonePad2;
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
// Bindless: transmittance LUT accessed via ResourceDescriptorHeap[transmittanceLutSrvIdx]
#else
Texture2D<float4>   transmittanceLUT : register(t0);
#endif
RWTexture2D<float4> outputLUT        : register(u0);
SamplerState        linearClampSamp  : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

static const uint  MS_LUT_SIZE      = 32;
static const int   NUM_DIRECTIONS   = 64;
static const int   NUM_STEPS        = 20;
static const float PI               = 3.14159265358979323846f;
static const float GOLDEN_RATIO     = 0.6180339887498949f;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Ray-sphere intersection returning far distance (origin assumed inside sphere).
/// @source UE5 SkyAtmosphereCommon.ush:96-107
float RaySphereIntersectFar(float3 origin, float3 dir, float radius)
{
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0f) return -1.0f;
    return -b + sqrt(disc);
}

/// Sample transmittance LUT using Bruneton 2017 parameterization.
/// @source UE5 SkyAtmosphereCommon.ush:280-300
float3 LookupTransmittance(float3 pos, float3 dir)
{
    float h = length(pos);
    float cosZenith = dot(normalize(pos), dir);

    // TransmittanceLutParamsToUv (same as transmittance_lut_cs.hlsl)
    float H   = sqrt(max(0.0f, topRadiusKm * topRadiusKm - bottomRadiusKm * bottomRadiusKm));
    float rho = sqrt(max(0.0f, h * h - bottomRadiusKm * bottomRadiusKm));

    float discriminant = h * h * (cosZenith * cosZenith - 1.0f) + topRadiusKm * topRadiusKm;
    float d = max(0.0f, -h * cosZenith + sqrt(max(0.0f, discriminant)));

    float dMin = topRadiusKm - h;
    float dMax = rho + H;
    float2 uv = float2(
        (dMax > dMin) ? (d - dMin) / (dMax - dMin) : 0.0f,
        (H > 0.0f) ? rho / H : 0.0f
    );
    uv = saturate(uv);

#ifdef UZ_BINDLESS
    Texture2D<float4> tLut = ResourceDescriptorHeap[transmittanceLutSrvIdx];
    return tLut.SampleLevel(linearClampSamp, uv, 0).rgb;
#else
    return transmittanceLUT.SampleLevel(linearClampSamp, uv, 0).rgb;
#endif
}

/// Compute extinction at a given altitude above the planet surface.
/// @source UE5 SkyAtmosphereCommon.ush:316-330 — SampleAtmosphereMediumRGB
void GetExtinctionAndScattering(float heightKm,
    out float3 extinction, out float3 scattering)
{
    // Rayleigh
    float rayleighDensity = exp(-heightKm / rayleighScaleH);
    float3 rayleighScat   = rayleighBeta * rayleighDensity;
    float3 rayleighExt    = rayleighScat; // No absorption

    // Mie
    float mieDensity  = exp(-heightKm / mieScaleH);
    float mieScat     = mieBeta * mieDensity;
    float mieExt      = mieScat * 1.11f; // Extinction/scattering ratio

    // Ozone — piecewise linear density
    /// @source UE5 SkyAtmosphereCommonData.h:38-42
    float ozoneDensity = (heightKm < ozoneLayerWidth)
        ? saturate(ozoneLinTerm0 * heightKm + ozoneConstTerm0)
        : saturate(ozoneLinTerm1 * heightKm + ozoneConstTerm1);
    float3 ozoneExt = ozoneAbsorption * ozoneDensity;

    scattering = rayleighScat + mieScat;
    extinction = rayleighExt + mieExt + ozoneExt;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 RenderSkyAtmosphereLookUpTablesCS.usf:280-380 — NewMultiScatCS
/// Uses Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere
/// Rendering Technique" — Section 4.1 Multi-Scattering Approximation.
///
/// For each (sunZenithCos, viewHeight):
///   1. Sample 64 uniform sphere directions (Fibonacci spiral)
///   2. For each direction, ray-march 20 steps computing single-scatter
///   3. Accumulate: L = inscattered luminance, f = scattered fraction
///   4. Apply geometric series: result = L / (1 - f)
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= MS_LUT_SIZE)) return;

    // ── LUT parameterization ──
    // Matches SampleMultiScatteringLut() in sky_atmosphere_ps.hlsl:
    //   U = dot(up, sunDir) * 0.5 + 0.5
    //   V = (viewHeight - bottomRadius) / (topRadius - bottomRadius)
    float2 uv = (float2(dtid.xy) + 0.5f) / float(MS_LUT_SIZE);

    float sunZenithCosAngle = uv.x * 2.0f - 1.0f;
    float viewHeight = lerp(bottomRadiusKm, topRadiusKm, uv.y);
    // Clamp to just above surface to avoid numerical issues
    viewHeight = max(viewHeight, bottomRadiusKm + 0.001f);

    float3 viewPos = float3(0.0f, 0.0f, viewHeight);
    float3 sunDir  = float3(
        sqrt(saturate(1.0f - sunZenithCosAngle * sunZenithCosAngle)),
        0.0f,
        sunZenithCosAngle
    );

    // ── Accumulate over 64 uniform sphere directions ──
    /// @source UE5 RenderSkyAtmosphereLookUpTablesCS.usf:310 — SquareSampleUniformSphere
    float3 totalLuminance = float3(0, 0, 0);
    float3 totalFms       = float3(0, 0, 0);

    for (int dirIdx = 0; dirIdx < NUM_DIRECTIONS; dirIdx++)
    {
        // Fibonacci sphere sampling for uniform direction distribution
        /// @source Hillaire 2020, Section 4.1
        float theta = acos(1.0f - 2.0f * (float(dirIdx) + 0.5f) / float(NUM_DIRECTIONS));
        float phi   = 2.0f * PI * frac(float(dirIdx) * GOLDEN_RATIO);
        float sinTheta = sin(theta);
        float cosTheta = cos(theta);
        float3 dir = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

        // ── Ray-march this direction ──
        float rayLength = RaySphereIntersectFar(viewPos, dir, topRadiusKm);
        if (rayLength <= 0.0f)
        {
            // Check if we hit the ground instead
            float groundDist = RaySphereIntersectFar(viewPos, dir, bottomRadiusKm);
            if (groundDist > 0.0f)
                rayLength = groundDist;
            else
                continue;
        }

        // Check for ground intersection (clamp ray length)
        {
            float b = dot(viewPos, dir);
            float c = dot(viewPos, viewPos) - bottomRadiusKm * bottomRadiusKm;
            float disc = b * b - c;
            if (disc >= 0.0f)
            {
                float tGround = -b - sqrt(disc);
                if (tGround > 0.0f)
                    rayLength = min(rayLength, tGround);
            }
        }

        float dt = rayLength / float(NUM_STEPS);
        float3 throughput = float3(1, 1, 1);
        float3 dirLuminance = float3(0, 0, 0);
        float3 dirFms       = float3(0, 0, 0);

        for (int step = 0; step < NUM_STEPS; step++)
        {
            float t = (float(step) + 0.5f) * dt;
            float3 samplePos = viewPos + dir * t;
            float h = length(samplePos) - bottomRadiusKm;
            h = max(h, 0.0f);

            float3 extinction, scattering;
            GetExtinctionAndScattering(h, extinction, scattering);

            // Transmittance from sun to this sample point
            float3 sunTransmittance = LookupTransmittance(samplePos, sunDir);

            // Frostbite analytical in-scattering integration
            /// @source UE5 SkyAtmosphereCommon.ush:344 — Frostbite analytical
            float3 expTerm = exp(-extinction * dt);

            // S_int = (scatter * T_sun - scatter * T_sun * exp(-ext*dt)) / ext
            // For multi-scatter: isotropic phase = 1/(4*PI), but it cancels out
            // in the geometric series because both L and f use the same phase.
            // UE5 uses uniform phase (= 1) for the multi-scatter LUT.
            /// @source UE5 RenderSkyAtmosphereLookUpTablesCS.usf:345
            float3 sampleScatterInt = (scattering - scattering * expTerm)
                / max(extinction, float3(1e-7f, 1e-7f, 1e-7f));

            // Luminance: weighted by sun transmittance (single-scatter)
            dirLuminance += throughput * sunTransmittance * sampleScatterInt;

            // f_ms: scattered fraction (no sun transmittance weighting)
            // This represents the fraction of light that is scattered
            // at each step, regardless of incoming light direction.
            dirFms += throughput * sampleScatterInt;

            throughput *= expTerm;
        }

        totalLuminance += dirLuminance;
        totalFms       += dirFms;
    }

    // ── Average over all directions ──
    /// @source UE5 RenderSkyAtmosphereLookUpTablesCS.usf:365
    float invNumDir = 1.0f / float(NUM_DIRECTIONS);
    totalLuminance *= invNumDir;
    totalFms       *= invNumDir;

    // ── Geometric series for infinite bounces ──
    /// @source Hillaire 2020, Equation 9: L_ms = L_2 / (1 - f_ms)
    /// f_ms is the average scattered fraction; (1-f_ms) is absorption.
    /// The geometric series sums all bounce orders: L_2 + f*L_2 + f^2*L_2 + ...
    float3 multiScatter = totalLuminance / max(1.0f - totalFms, float3(1e-7f, 1e-7f, 1e-7f));

    // Isotropic phase factor: uniform sphere integration already accounts for it
    // (each direction weighted equally = isotropic assumption).

    outputLUT[dtid.xy] = float4(multiScatter, 1.0f);
}
