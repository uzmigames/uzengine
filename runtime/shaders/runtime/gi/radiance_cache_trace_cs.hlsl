/// UzEngine Radiance Cache Trace — SDF-based probe update.
///
/// Updates one L2 SH probe per thread. For every probe in the grid we shoot
/// `raysPerProbe` uniform full-sphere rays, ray-march them through the
/// global distance field clipmap hierarchy, and accumulate the resulting
/// radiance into the probe's L2 SH coefficients (7 float4 packing). Hit points
/// receive sun lighting and diffuse feedback using the surface reflectance cache.
/// Missed rays sample the sky color. The new SH is EMA-blended with the prior
/// frame to suppress single-frame noise.
///
/// @ref Sloan 2008 "Stupid Spherical Harmonics (SH) Tricks" — L2 projection
/// @ref Wright 2015 "Dynamic Occlusion with Signed Distance Fields"
/// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced
///                     Irradiance Fields" — temporal hysteresis blending

#include "../common/constants.hlsli"
#include "../lighting/ibl.hlsli"

SamplerState linearClamp : register(s2);

// ─── Root constants at b0 ──────────────────────────────────────────────────

cbuffer RootConstants : register(b0) {
    uint g_shBufferUavIndex;   // Bindless UAV index of the probe SH buffer.
    uint g_shBufferSrvIndex;   // Previous completed SH snapshot; never the active UAV.
    uint g_frameIndex;         // Frame counter for temporal jitter.
    uint g_probeOffset;        // First probe index updated this frame.
    uint g_probeBudget;        // Number of probes to update this frame (0 = all).
    uint g_skyRadianceSrv;
    uint g_diagnosticUav;
    uint g_sequenceMode;       ///< Diagnostic: 0 = this writer's per-frame + per-probe rotation;
                               ///< 1 = the DDGI writer's index/count perturbation (same directions).
    uint g_knownRadianceBits;  ///< Diagnostic (followup-01 consumer stage): float bits of a constant
                               ///< radiance every ray returns instead of tracing; 0 = off.
    uint g_knownRadianceMode;  ///< 0 = constant; 1 = directional L * (0.5 + 0.5 dot(dir, d0)).
    uint g_probeSideSrv;       ///< Per probe (probe_side_cs.hlsl): nearest face, back-face fraction; sentinel = not computed.
    uint g_classification;     ///< 0 = biased field < kSurfaceEpsilon at the probe is inside (current rule);
                               ///< 1 = followup-01 candidate: back-face fraction, relocation, guard;
                               ///< 2 = as 1, plus inside when most rays exit some mesh more often
                               ///< than they enter it (embedded faces, followup-01 R3).
    // Phase D stage 3 (the SH writer reads the surface radiance cache): the cache's binding record
    // (sentinel = no cache, every hit shades as before) and the raw (unexpanded) distance clipmaps
    // of the same four cascades, for the snap of a hit back to the raw surface (sentinel = no snap).
    uint g_surfaceCacheParamsSrv;
    uint g_rawClipmapSrv0;
    uint g_rawClipmapSrv1;
    uint g_rawClipmapSrv2;
    uint g_rawClipmapSrv3;
};

// ─── Trace constants at b2 (matches RadianceCacheTraceConstants) ──────────

cbuffer TraceConstants : register(b2) {
    float gridOriginX;
    float gridOriginY;
    float gridOriginZ;
    float probeSpacing;

    uint  probesPerAxis;
    uint  totalProbes;
    uint  raysPerProbe;
    uint  frameIndex;

    float maxTraceDistance;
    float temporalBlendAlpha;
    uint  shBufferUav;          // Same as g_shBufferUavIndex (cb mirror).
    uint  firstWrite;           // 1 = buffer never written: replace instead of blend.

    float gdfCenter0X, gdfCenter0Y, gdfCenter0Z, gdfExtent0;
    float gdfCenter1X, gdfCenter1Y, gdfCenter1Z, gdfExtent1;
    float gdfCenter2X, gdfCenter2Y, gdfCenter2Z, gdfExtent2;
    float gdfCenter3X, gdfCenter3Y, gdfCenter3Z, gdfExtent3;

    uint  gdfClipmapSrv0;
    uint  gdfClipmapSrv1;
    uint  gdfClipmapSrv2;
    uint  gdfClipmapSrv3;

    uint  numGdfClipmaps;
    float skyColorR;
    float skyColorG;
    float skyColorB;

    float lightDirX;
    float lightDirY;
    float lightDirZ;
    float lightIntensity;

    float lightColorR;
    float lightColorG;
    float lightColorB;
    float groundAlbedo;

    // ── VFX → DDGI emissive injection (phase16 hardware-validation) ──────
    // Trailing fields appended in `RadianceCacheTraceConstants` so the C++
    // host can write a single struct to either compute pipeline. The radiance
    // cache trace shader does not consume the VFX feed (only the DDGI atlas
    // writer in `ddgi_probe_update_cs.hlsl` does); we declare them only so
    // the cbuffer layout matches the CPU mirror (464 bytes since block B-02).
    uint  vfxEmissiveSrvIndex;
    uint  vfxEmissiveSampleCount;
    float regionBlendAlpha;    ///< Blend weight of a probe inside a change box (1 = replace, block B.2 phase 4)
    float _vfxEmissivePad1;

    // Grid recentre: the history buffer was traced with the grid at its previous
    // origin. history coordinate = probe coordinate + shift (whole probes); zero
    // unless the grid moved this frame.
    int   historyShiftX;
    int   historyShiftY;
    int   historyShiftZ;
    uint  regionCount;         ///< Change boxes (block B.2): probes inside replace their history
    float4 regionLo[8];        ///< lo.xyz of each box
    float4 regionHi[8];        ///< hi.xyz of each box
};

/// True inside one of this frame's change boxes: the distance field changed there, so the
/// probe's history was traced against geometry that no longer exists and it replaces instead of
/// blending (block B.2 phase 4).
bool InChangedRegion(float3 p) {
    [loop] for (uint r = 0; r < min(regionCount, 8u); ++r) {
        if (all(p >= regionLo[r].xyz) && all(p <= regionHi[r].xyz)) return true;
    }
    return false;
}

// ─── History grid (previous origin) ───────────────────────────────────────
//
// Probes are snapped to the spacing, so after a recentre the probe at coordinate
// c is the same world-space probe as c + shift in the history buffer. Reading
// history through these helpers keeps temporal hysteresis and the multi-bounce
// feedback alive across a recentre; only the slab that entered the grid has no
// history. Identity when the grid did not move.

int3 HistoryShift() { return int3(historyShiftX, historyShiftY, historyShiftZ); }

float3 HistoryGridOrigin() {
    return float3(gridOriginX, gridOriginY, gridOriginZ) - float3(HistoryShift()) * probeSpacing;
}

// ─── Constants ────────────────────────────────────────────────────────────

/// @constants Pi normalizes sphere integration; 5 cm hit tolerance, 2 cm minimum
/// step, 96 trace steps and 64 rays are this implementation's quality budgets.
static const float kPI            = 3.14159265358979323846;
static const float kSurfaceEpsilon = 5.0;     // cm — accept hits below this distance
static const float kMinStep        = 2.0;     // cm — never advance less than this
static const uint  kMaxStepsPerRay = 96;
static const uint  kMaxRaysPerProbe = 64;

// Known-radiance diagnostic (followup-01): mode 0 = constant L, mode 1 = L * (0.5 + 0.5 dot(dir, d0))
// with d0 = normalize(0.3, 0.5, 0.8), a smooth non-negative function of direction that crosses every
// octahedral tile edge and corner — a misplaced or inverted border copy shows against it.
static const float3 kKnownDir = float3(0.30305, 0.50508, 0.80812);
float3 KnownRadiance(float L, float3 dir, uint mode) {
    // mode 2 (per-probe parity) is applied by the caller through L itself; here it is a constant.
    const float v = (mode == 1u) ? L * (0.5 + 0.5 * dot(dir, kKnownDir)) : L;
    return float3(v, v, v);
}

// ─── L2 SH basis (Sloan 2008 packing — pre-cosine-convolved on accumulate) ──
//
// We store 9 raw L2 coefficients per channel. They are written into the 7
// float4 packing consumed by `EvalSH_L2` in deferred lighting:
//   coeffs[0] = (sh1.r, sh2.r, sh3.r, sh0.r)   ← UE5/Three.js L0+L1 packing
//   coeffs[1] = (sh1.g, sh2.g, sh3.g, sh0.g)
//   coeffs[2] = (sh1.b, sh2.b, sh3.b, sh0.b)
//   coeffs[3] = (sh4.r, sh5.r, sh6.r, sh7.r)
//   coeffs[4] = (sh4.g, sh5.g, sh6.g, sh7.g)
//   coeffs[5] = (sh4.b, sh5.b, sh6.b, sh7.b)
//   coeffs[6] = (sh8.r, sh8.g, sh8.b, 1)
//
// Basis values (Ramamoorthi & Hanrahan 2001, Sloan 2008):
//   Y0   =  0.282095
//   Y1.x = -0.488603 * x
//   Y1.y =  0.488603 * y
//   Y1.z = -0.488603 * z
//   Y2.xy =  1.092548 * x * y
//   Y2.yz = -1.092548 * y * z
//   Y2.zz =  0.315392 * (3*z*z - 1)
//   Y2.xz = -1.092548 * x * z
//   Y2.xx =  0.546274 * (x*x - y*y)

void SHEvaluateL2(float3 dir, out float basis[9]) {
    basis[0] =  0.282095;
    basis[1] = -0.488603 * dir.x;
    basis[2] =  0.488603 * dir.y;
    basis[3] = -0.488603 * dir.z;
    basis[4] =  1.092548 * dir.x * dir.y;
    basis[5] = -1.092548 * dir.y * dir.z;
    basis[6] =  0.315392 * (3.0 * dir.z * dir.z - 1.0);
    basis[7] = -1.092548 * dir.x * dir.z;
    basis[8] =  0.546274 * (dir.x * dir.x - dir.y * dir.y);
}

/// @ref Sloan 2008 "Stupid Spherical Harmonics (SH) Tricks"
/// @constants Cosine convolution divided by pi: A0=1, A1=2/3, A2=1/4.
// EvalSH_L2 evaluates a polynomial, so bake both basis and convolution here.
void PackL2(float shR[9], float shG[9], float shB[9], out float4 packed[7]) {
    const float c0 = 0.282094791773878;
    const float c1 = 0.32573500793528;
    const float c2 = 0.27313710764802;
    const float c3 = 0.07884789131313;
    const float c4 = 0.13656855382401;
    packed[0] = float4(-c1*shR[1], c1*shR[2], -c1*shR[3], c0*shR[0]-c3*shR[6]);
    packed[1] = float4(-c1*shG[1], c1*shG[2], -c1*shG[3], c0*shG[0]-c3*shG[6]);
    packed[2] = float4(-c1*shB[1], c1*shB[2], -c1*shB[3], c0*shB[0]-c3*shB[6]);
    packed[3] = float4(c2*shR[4], -c2*shR[5], 3.0*c3*shR[6], -c2*shR[7]);
    packed[4] = float4(c2*shG[4], -c2*shG[5], 3.0*c3*shG[6], -c2*shG[7]);
    packed[5] = float4(c2*shB[4], -c2*shB[5], 3.0*c3*shB[6], -c2*shB[7]);
    packed[6] = float4(c4*shR[8], c4*shG[8], c4*shB[8], 1.0);
}

void UnpackL2(float4 packed[7], out float shR[9], out float shG[9], out float shB[9]) {
    shR[0] = packed[0].w; shG[0] = packed[1].w; shB[0] = packed[2].w;
    shR[1] = packed[0].x; shG[1] = packed[1].x; shB[1] = packed[2].x;
    shR[2] = packed[0].y; shG[2] = packed[1].y; shB[2] = packed[2].y;
    shR[3] = packed[0].z; shG[3] = packed[1].z; shB[3] = packed[2].z;
    shR[4] = packed[3].x; shG[4] = packed[4].x; shB[4] = packed[5].x;
    shR[5] = packed[3].y; shG[5] = packed[4].y; shB[5] = packed[5].y;
    shR[6] = packed[3].z; shG[6] = packed[4].z; shB[6] = packed[5].z;
    shR[7] = packed[3].w; shG[7] = packed[4].w; shB[7] = packed[5].w;
    shR[8] = packed[6].x; shG[8] = packed[6].y; shB[8] = packed[6].z;
}

// Shared GDF surface sampling (needs linearClamp, the trace cbuffer and
// kSurfaceEpsilon, all declared above).
#include "gdf_surface.hlsli"
#include "rc_probe_guard.hlsli"
#include "surface_cache.hlsli"      // Phase D stage 3: SurfaceCacheQuerySdf at a hit

// ─── Phase D stage 3: the raw (unexpanded) distance at a hit ──────────────────────────
//
// The b2 clipmaps are the expanded field (`TraceGDF` registers a hit where it drops below
// kSurfaceEpsilon, in front of the raw surface by the composite's expansion). The raw R16F
// volumes share the cascades' centres, extents and toroidal layout (`SampleGdfClipmap`), so the
// distance to the raw surface at the hit is one more tap; 1e9 where no raw cascade covers it.
float SampleRawClipmapDistance(uint srvIdx, float3 worldPos, float3 center, float extent) {
    if (srvIdx == kSentinelSrvIndex || extent <= 0.0) return 1e9;
    Texture3D<float> rawTex = ResourceDescriptorHeap[srvIdx];
    uint resX, resY, resZ;
    rawTex.GetDimensions(resX, resY, resZ);
    float3 local = (worldPos - center) / extent;
    if (any(abs(local) > 1.0 - 2.0 / float(resX))) return 1e9;
    return rawTex.SampleLevel(UZ_GDF_WRAP_SAMPLER, frac(worldPos / (2.0 * extent)), 0);
}

float SampleRawDistance(float3 worldPos) {
    float d = SampleRawClipmapDistance(g_rawClipmapSrv0, worldPos, float3(gdfCenter0X, gdfCenter0Y, gdfCenter0Z), gdfExtent0);
    if (d < 1e8) return d;
    d = SampleRawClipmapDistance(g_rawClipmapSrv1, worldPos, float3(gdfCenter1X, gdfCenter1Y, gdfCenter1Z), gdfExtent1);
    if (d < 1e8) return d;
    d = SampleRawClipmapDistance(g_rawClipmapSrv2, worldPos, float3(gdfCenter2X, gdfCenter2Y, gdfCenter2Z), gdfExtent2);
    if (d < 1e8) return d;
    return SampleRawClipmapDistance(g_rawClipmapSrv3, worldPos, float3(gdfCenter3X, gdfCenter3Y, gdfCenter3Z), gdfExtent3);
}

// ─── GDF clipmap sampling ─────────────────────────────────────────────────
//
// Each clipmap stores distance in R and diffuse reflectance in GBA, with a
// half-extent in cm. Sampling returns the distance to the nearest
// surface in cm; values outside the clipmap return a large positive number
// (the next cascade is tried automatically).

// SampleGdfClipmap / SampleGdfSurface / SampleGdfDistance / GdfNormal now
// live in gdf_surface.hlsli, included above — the DDGI atlas writer needs
// the same surface attributes and inline RayQuery cannot provide them.

// Sphere-tracing returns a hit (>=0), distance escape (-1), or exhausted work (-2).
// `endT` reports where the march stopped whatever the outcome: the hit distance, the distance
// at which the ray left the trace range, or the distance reached when the budget ran out. The
// -1/-2 sentinels discard that, and an escape distance is what says whether a ray left through
// an opening or simply ran past the end of the field (followup-01 stage 3).
float TraceGDF(float3 origin, float3 dir, float maxT, out float endT, out float minD) {
    float t = kMinStep;
    endT = 0.0;
    minD = 1e30;
    [loop]
    for (uint i = 0; i < kMaxStepsPerRay; ++i) {
        float3 p = origin + dir * t;
        float d = SampleGdfDistance(p);
        minD = min(minD, d);
        if (d < kSurfaceEpsilon) {
            endT = t;
            return t;
        }
        t += max(d, kMinStep);
        if (t >= maxT) {
            endT = t;
            return -1.0;
        }
    }
    endT = t;
    return -2.0;
}

float TraceGDF(float3 origin, float3 dir, float maxT, out float endT) {
    float unusedMinD;
    return TraceGDF(origin, dir, maxT, endT, unusedMinD);
}

float TraceGDF(float3 origin, float3 dir, float maxT) {
    float unusedEndT;
    return TraceGDF(origin, dir, maxT, unusedEndT);
}

// ─── Candidate classification (followup-01, g_classification = 1) ────────
//
// The biased field is unsigned and conservative, so "field < kSurfaceEpsilon at the probe" holds
// within a band around every surface, on both sides. The side of the nearest mesh face tells the
// sides apart; a probe in front of its face but inside the band cannot start a march there (every
// ray would register a hit at the first step), so its trace origin moves off the face first.
/// @constants Relocated origins keep 2 x kSurfaceEpsilon of field clearance (the offset the shadow and
/// indirect lookups at hit points already use): with a 1-Lipschitz field and a first step of
/// kMinStep, no ray starts inside the hit tolerance. Half a spacing keeps the origin nearer its own
/// grid point than any neighbour's; eight steps suffice because each step restores the missing
/// clearance along the nearest face's normal.
static const float kRelocationClearance = 2.0 * kSurfaceEpsilon;
static const uint  kRelocationSteps = 8;
/// @ref Majercik 2021 "Scaling Probe-Based Real-Time Dynamic Global Illumination for Production",
///      section 6.1 — inside geometry = too many of the probe's rays see back faces.
/// @constants A quarter: the measured fractions are bimodal (H-64: 13 of 601 Sponza probes between 0.1
/// and 0.5; outside 1.0, enclosed interior 0.0), and a quarter sits in the empty middle.
static const float kBackfaceThreshold = 0.25;
/// @constants Mode 2: a probe is also inside when more than half of the 64 fixed rays leave some mesh
/// more often than they enter it (probe_side_cs.hlsl, record [1].w). From inside a closed solid every
/// ray that reaches its boundary within the trace distance votes, so the fraction is ~1 there; half
/// leaves room for rays through open meshes, which can add unmatched exits from a free probe.
static const float kCrossingThreshold = 0.5;

bool RelocateOrigin(float3 probePos, float3 direction, out float3 origin) {
    origin = probePos;
    [loop] for (uint step = 0; step < kRelocationSteps; ++step) {
        const float clearance = SampleGdfDistance(origin);
        if (clearance >= kRelocationClearance) return true;
        origin += direction * max(kRelocationClearance - clearance, kMinStep);
        if (distance(origin, probePos) > 0.5 * probeSpacing) return false;
    }
    return SampleGdfDistance(origin) >= kRelocationClearance;
}

/// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields", sections 5.2-5.3.
/// @constants Eight trilinear corners; squared normal weight rejects probes behind a surface.
float3 SamplePreviousDiffuse(float3 position, float3 normal) {
    if (firstWrite != 0 || g_shBufferSrvIndex == kSentinelSrvIndex) return 0.0;
    // The history buffer is laid out on the *previous* grid.
    const float3 historyOrigin = HistoryGridOrigin();
    float3 cell = (position - historyOrigin) / probeSpacing;
    if (any(cell < 0.0) || any(cell > float(probesPerAxis - 1))) return 0.0;
    uint3 lower = uint3(floor(cell));
    uint3 upper = min(lower + 1, probesPerAxis - 1);
    float3 fraction = frac(cell);
    StructuredBuffer<float4> history = ResourceDescriptorHeap[g_shBufferSrvIndex];
    float3 irradiance = 0.0;
    float totalWeight = 0.0;
    [unroll]
    for (uint corner = 0; corner < 8; ++corner) {
        uint3 index = uint3((corner & 1) ? upper.x : lower.x,
                            (corner & 2) ? upper.y : lower.y,
                            (corner & 4) ? upper.z : lower.z);
        float weight = ((corner & 1) ? fraction.x : 1.0 - fraction.x)
                     * ((corner & 2) ? fraction.y : 1.0 - fraction.y)
                     * ((corner & 4) ? fraction.z : 1.0 - fraction.z);
        float3 probePosition = historyOrigin + float3(index) * probeSpacing;
        uint probeLinear = index.x + index.y * probesPerAxis + index.z * probesPerAxis * probesPerAxis;
        const float4 guard = history[ProbeGuardIndex(probesPerAxis, probeLinear)];
        float3 toProbe = ProbeEffectivePosition(guard, probePosition) - position;
        float distance = length(toProbe);
        float normalWeight = distance > 0.0 ? saturate(dot(normal, toProbe / distance)) : 1.0;
        weight *= normalWeight * normalWeight;
        uint base = probeLinear * 7;
        if (history[base + 6].w <= 0.0) continue;
        if (ProbeGuardRejects(guard, probePosition, position)) continue;
        float4 coefficients[7];
        [unroll] for (uint k = 0; k < 7; ++k) coefficients[k] = history[base + k];
        irradiance += weight * EvalSH_L2(normal, coefficients);
        totalWeight += weight;
    }
    return totalWeight > 0.0 ? irradiance / totalWeight : 0.0;
}


// ─── Hammersley + uniform sphere sample ─────────────────────────────────

float RadicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N) {
    return float2(float(i) / float(N), RadicalInverseVdC(i));
}

// Maps a [0,1)^2 sample onto a unit sphere — full-sphere distribution so the
// L2 SH bands are excited symmetrically (we don't have a single normal at the
// probe location).
float3 SphericalSample(float2 xi) {
    float z = 1.0 - 2.0 * xi.x;
    float r = sqrt(max(0.0, 1.0 - z * z));
    float phi = 2.0 * kPI * xi.y;
    return float3(r * cos(phi), r * sin(phi), z);
}

// ─── Entry point — one thread per probe ──────────────────────────────────

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    // Temporal probe spreading: each frame updates `g_probeBudget` probes
    // starting at `g_probeOffset`, wrapping around the grid. Skipping probes
    // outside the budget keeps GPU cost bounded for large grids.
    uint localIdx = DTid.x;
    uint budget = (g_probeBudget == 0) ? totalProbes : g_probeBudget;
    if (localIdx >= budget) return;
    uint probeIdx = (g_probeOffset + localIdx) % totalProbes;
    if (probeIdx >= totalProbes) return;

    // Probe coordinate inside the cube grid.
    uint gx = probeIdx % probesPerAxis;
    uint gy = (probeIdx / probesPerAxis) % probesPerAxis;
    uint gz = probeIdx / (probesPerAxis * probesPerAxis);

    float3 probePos = float3(gridOriginX, gridOriginY, gridOriginZ)
                    + float3(float(gx), float(gy), float(gz)) * probeSpacing;

    /// @ref Majercik 2021 "Scaling Probe-Based Real-Time Dynamic Global Illumination for Production", section 6.1.
    /// @constants SH record6.w stores validity (0=inside geometry,1=active); seven records per probe,
    /// then one guard record per probe (rc_probe_guard.hlsli); six diagnostic records per probe.
    /// Class codes (diagnostic record 4.z): 0 valid and 1 inside under the current rule; 2 valid by
    /// the rays, 3 inside by the rays, 4 relocated, 5 relocation failed under the candidate; 6 inside
    /// by the crossings alone (mode 2: the back-face fraction said free); mode 3: 7 not reachable
    /// from the camera, 8 reachable, 9 reachable and traced from the end of its climb, 10 relocated
    /// into reached air (r.GI.RadianceCache.ReachRelocate; y = distance moved) (w = 1 when
    /// connectivity was used). Record 5.x = the crossing fraction (0 when the classifier did not run),
    /// 5.yzw = trace origin minus grid position (0 unless moved).
    float4 probeSurface = SampleGdfSurface(probePos);
    float3 traceOrigin = probePos;
    float4 guard = 0.0;
    float4 classification = float4(0.0, 0.0, 0.0, 0.0);
    float crossingFraction = 0.0;
    bool inside;
    if (g_classification == 0u || g_probeSideSrv == kSentinelSrvIndex) {
        inside = probeSurface.r < kSurfaceEpsilon;
        classification.z = inside ? 1.0 : 0.0;
    } else if (g_classification == 3u) {
        // Reachability (probe_reach_cs.hlsl): [0] xyz = climb direction, w = guard distance;
        // [1] x = valid, y = climb length. A valid probe inside the band is traced from the end of
        // its climb, which is in air connected to the camera, and guarded by the plane behind it.
        StructuredBuffer<float4> sides = ResourceDescriptorHeap[g_probeSideSrv];
        const float4 climb = sides[probeIdx * 2];
        const float4 reach = sides[probeIdx * 2 + 1];
        inside = reach.x < 0.5;
        classification.y = reach.y;
        classification.w = reach.w;
        classification.z = inside ? 7.0 : 8.0;
        if (reach.x > 1.5) {
            // Relocated: traced at the moved position; readers weight it there and reject receivers
            // behind the surface it was moved across (w < 0 encoding, rc_probe_guard.hlsli).
            traceOrigin = probePos + climb.xyz * reach.y;
            guard = float4(climb.xyz * reach.y, -max(climb.w, 0.01));
            classification.z = 10.0;
        } else if (!inside && probeSurface.r < kSurfaceEpsilon) {
            traceOrigin = probePos + climb.xyz * reach.y;
            // w = 0 would mean "no guard"; a probe at or below the biased surface keeps a plane
            // through itself.
            guard = float4(climb.xyz, max(climb.w, 0.01));
            classification.z = 9.0;
        }
    } else {
        StructuredBuffer<float4> sides = ResourceDescriptorHeap[g_probeSideSrv];
        const float4 nearest = sides[probeIdx * 2];       // direction from the nearest face, distance
        const float4 rays = sides[probeIdx * 2 + 1];      // back-face fraction, hits, nearest-face side
        classification.x = rays.z * nearest.w;
        classification.w = rays.x;
        crossingFraction = rays.w;
        inside = rays.y > 0.0 && rays.x > kBackfaceThreshold;
        classification.z = inside ? 3.0 : 2.0;
        if (!inside && g_classification == 2u && rays.w > kCrossingThreshold) {
            inside = true;
            classification.z = 6.0;
        }
        if (!inside && probeSurface.r < kSurfaceEpsilon) {
            inside = !RelocateOrigin(probePos, nearest.xyz, traceOrigin);
            classification.y = distance(traceOrigin, probePos);
            classification.z = inside ? 5.0 : 4.0;
            if (!inside) guard = float4(nearest.xyz, nearest.w);
        }
    }
    RWStructuredBuffer<float4> guardOut = ResourceDescriptorHeap[g_shBufferUavIndex];
    guardOut[ProbeGuardIndex(probesPerAxis, probeIdx)] = guard;
    if (inside) {
        RWStructuredBuffer<float4> outputSH = ResourceDescriptorHeap[g_shBufferUavIndex];
        [unroll] for (uint coefficient = 0; coefficient < 7; ++coefficient)
            outputSH[probeIdx * 7 + coefficient] = 0.0;
        if (g_diagnosticUav != 0 && g_diagnosticUav != kSentinelSrvIndex) {
            RWStructuredBuffer<float4> diagnostic = ResourceDescriptorHeap[g_diagnosticUav];
            diagnostic[probeIdx * 6] = probeSurface;
            diagnostic[probeIdx * 6 + 1] = 0.0;
            diagnostic[probeIdx * 6 + 2] = 0.0;
            diagnostic[probeIdx * 6 + 3] = 0.0;
            diagnostic[probeIdx * 6 + 4] = classification;
            diagnostic[probeIdx * 6 + 5] = float4(crossingFraction, traceOrigin - probePos);
        }
        return;
    }

    float shR[9] = {0,0,0,0,0,0,0,0,0};
    float shG[9] = {0,0,0,0,0,0,0,0,0};
    float shB[9] = {0,0,0,0,0,0,0,0,0};

    uint nRays = min(raysPerProbe, kMaxRaysPerProbe);
    float invN = 1.0 / float(nRays);

    // Per-frame jitter — rotate the Hammersley sequence so different frames
    // sample different directions, then EMA-blend in time.
    uint frameJitter = (frameIndex * 1664525u + 1013904223u);
    /// @constants Diagnostic luminance uses Rec.709 weights (0.2126,0.7152,0.0722).
    float4 traceStats = 0.0;
    // Sky-only readback: x = mean luminance carried by SKY rays (over all rays), y = sky ray
    // fraction, z = fraction of rays that exhausted the budget, w = mean luminance carried by hits.
    float4 skyStats = 0.0;
    // Hit/escape side (followup-01 stage 3): x = mean of the CLOSEST the ray came to a surface
    // over the ESCAPED rays (the end distance it held before is void: an escape is defined by
    // reaching the end of the range, so it always reported the end of the range),
    // y = mean sun-term luminance over HIT rays, z = mean indirect-term luminance over HIT rays
    // (the DDGI writer has no indirect term), w = hit ray count.
    float4 pathStats = 0.0;
    float  escapeCount = 0.0;

    [loop]
    for (uint i = 0; i < nRays; ++i) {
        // Since H-57 the DDGI writer uses this same construction; the diagnostic mode that once
        // replicated its windowed sequence is therefore the identity and is kept for the record.
        float2 xi = frac(Hammersley(i, nRays)
                         + float2(RadicalInverseVdC(frameJitter), RadicalInverseVdC(frameJitter ^ probeIdx)));
        float3 dir = SphericalSample(xi);

        float endT, minD;
        const float knownL = asfloat(g_knownRadianceBits);
        float t;
        if (knownL > 0.0) {
            // Known radiance: no trace, every ray is an escape carrying the constant.
            t = -1.0; endT = maxTraceDistance; minD = 1e6;
        } else {
            t = TraceGDF(traceOrigin, dir, maxTraceDistance, endT, minD);
        }

        float3 radiance = 0.0;
        if (t == -1.0) {
            pathStats.x += min(minD, 1e6); escapeCount += 1.0;
            traceStats.x += invN;
            radiance = float3(0.0, 0.0, 0.0);
            if (knownL > 0.0) {
                float knownLeff = knownL;
                if (g_knownRadianceMode == 2u) {
                    // Per-probe parity: odd cells L, even cells L/4 (followup-01 step 4).
                    const uint3 cell = uint3(probeIdx % probesPerAxis, (probeIdx / probesPerAxis) % probesPerAxis,
                                             probeIdx / (probesPerAxis * probesPerAxis));
                    knownLeff = ((cell.x + cell.y + cell.z) & 1u) != 0u ? knownL : 0.25 * knownL;
                }
                radiance = KnownRadiance(knownLeff, dir, g_knownRadianceMode);
            } else if (g_skyRadianceSrv != kSentinelSrvIndex) {
                TextureCube<float4> sky = ResourceDescriptorHeap[g_skyRadianceSrv];
                radiance = sky.SampleLevel(linearClamp, float3(dir.x, dir.z, dir.y), 0).rgb
                         * float3(skyColorR, skyColorG, skyColorB);
            }
        } else if (t >= 0.0) {
            float3 hitPosition = traceOrigin + t * dir;
            float3 N    = GdfNormal(hitPosition);
            // Phase D stage 3: the surface radiance cache at the hit (D8). The hit is snapped back
            // to the raw surface along the field normal, lifted by the writers' 2 cm bias and read
            // `sdfLodBias` levels coarser than a TLAS hit; a readable cell (exact material, local
            // lights, emission, the feed's bounce) replaces the GDF-average shading below, so the
            // probe inherits it. Nothing is written here (the SDF consumers are read-only).
            SurfaceCacheParams cache = (SurfaceCacheParams)0;
            float3 cellSun = 0.0, cellOther = 0.0;
            bool cellRead = false;
            if (g_surfaceCacheParamsSrv != kSentinelSrvIndex && dot(N, -dir) > 0.0) {
                StructuredBuffer<SurfaceCacheParams> cacheParams = ResourceDescriptorHeap[g_surfaceCacheParamsSrv];
                cache = cacheParams[0];
                if ((cache.flags & kSurfaceCacheFlagSdfReads) != 0u) {
                    const float3 snapped = SurfaceCacheSdfReadPosition(hitPosition, N, SampleRawDistance(hitPosition));
                    cellRead = SurfaceCacheQuerySdf(cache, snapped, N, cellSun, cellOther);
                }
            }
            if (cellRead && (cache.flags & kSurfaceCacheFlagDirectInline) == 0u) {
                radiance = cellSun + cellOther;
                traceStats.y += invN;
                traceStats.z += dot(cellSun, float3(0.2126, 0.7152, 0.0722)) * invN;
                pathStats.y += dot(cellSun,   float3(0.2126, 0.7152, 0.0722));
                pathStats.z += dot(cellOther, float3(0.2126, 0.7152, 0.0722));
                pathStats.w += 1.0;
            } else {
                float3 L    = normalize(float3(lightDirX, lightDirY, lightDirZ));
                float  NdL  = max(dot(N, L), 0.0);
                float visibility = 0.0;
                if (NdL > 0.0 && dot(N, -dir) > 0.0) {
                    float3 shadowOrigin = hitPosition + N * (2.0 * kSurfaceEpsilon);
                    visibility = TraceGDF(shadowOrigin, L, maxTraceDistance) == -1.0 ? 1.0 : 0.0;
                }
                float3 sunRad = float3(lightColorR, lightColorG, lightColorB)
                              * lightIntensity * NdL * visibility;
                traceStats.y += dot(N, -dir) > 0.0 ? invN : 0.0;
                traceStats.z += dot(sunRad, float3(0.2126, 0.7152, 0.0722)) * invN;
                /// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields"
                /// @constants Direct irradiance divides by pi; packed SH already stores E/pi.
                float3 albedo = saturate(SampleGdfSurface(hitPosition).gba);
                float3 indirect = SamplePreviousDiffuse(hitPosition + N * (2.0 * kSurfaceEpsilon), N);
                radiance = dot(N, -dir) > 0.0 ? albedo * (sunRad / kPI + indirect) : 0.0;
                if (dot(N, -dir) > 0.0) {
                    pathStats.y += dot(albedo * (sunRad / kPI), float3(0.2126, 0.7152, 0.0722));
                    pathStats.z += dot(albedo * indirect,       float3(0.2126, 0.7152, 0.0722));
                }
                pathStats.w += 1.0;
                // DirectMode 1: the inline sun with the cell's `other` layer.
                if (cellRead) radiance = albedo * (sunRad / kPI) + cellOther;
            }
        }
        traceStats.w += dot(radiance, float3(0.2126, 0.7152, 0.0722)) * invN;
        if (t == -1.0)     { skyStats.x += dot(radiance, float3(0.2126, 0.7152, 0.0722)) * invN; skyStats.y += invN; }
        else if (t >= 0.0) { skyStats.w += dot(radiance, float3(0.2126, 0.7152, 0.0722)) * invN; }
        else               { skyStats.z += invN; }

        // Project raw radiance; PackL2 applies the cosine convolution.
        float basis[9];
        SHEvaluateL2(dir, basis);

        [unroll]
        for (uint b = 0; b < 9; ++b) {
            // Monte-Carlo: 4π / N (full-sphere uniform distribution).
            float w = basis[b] * 4.0 * kPI * invN;
            shR[b] += radiance.r * w;
            shG[b] += radiance.g * w;
            shB[b] += radiance.b * w;
        }
    }

    // Optional transport readback: three float4 records per probe, Rec.709 luminance.
    if (g_diagnosticUav != 0 && g_diagnosticUav != kSentinelSrvIndex) {
        RWStructuredBuffer<float4> diagnostic = ResourceDescriptorHeap[g_diagnosticUav];
        diagnostic[probeIdx * 6] = probeSurface;
        diagnostic[probeIdx * 6 + 1] = traceStats;
        diagnostic[probeIdx * 6 + 2] = skyStats;
        diagnostic[probeIdx * 6 + 3] = float4(pathStats.x / max(escapeCount, 1.0),
                                              pathStats.y / max(pathStats.w, 1.0),
                                              pathStats.z / max(pathStats.w, 1.0),
                                              pathStats.w);
        diagnostic[probeIdx * 6 + 4] = classification;
        diagnostic[probeIdx * 6 + 5] = float4(crossingFraction, traceOrigin - probePos);
    }

    // Pack into 7 float4s (matches deferred lighting `EvalSH_L2`).
    float4 newPacked[7];
    PackL2(shR, shG, shB, newPacked);

    // ── Hysteresis: temporal blend with previous SH ────────────────────
    // @ref Majercik 2019 "Dynamic Diffuse Global Illumination" — alpha 0.05–0.15
    RWStructuredBuffer<float4> shBuf = ResourceDescriptorHeap[g_shBufferUavIndex];
    uint base = probeIdx * 7u;

    // This probe's history lives at its coordinate in the *previous* grid. A probe
    // that entered the grid this frame (shifted coordinate outside the grid) or
    // that was inside geometry last time (validity 0) has no history: replace.
    int3 historyCoord = int3(int(gx), int(gy), int(gz)) + HistoryShift();
    bool hasHistory = (firstWrite == 0)
                   && all(historyCoord >= 0) && all(historyCoord < int(probesPerAxis));
    uint historyBase = base;
    if (hasHistory) {
        StructuredBuffer<float4> validity = ResourceDescriptorHeap[g_shBufferSrvIndex];
        historyBase = (uint(historyCoord.x)
                     + uint(historyCoord.y) * probesPerAxis
                     + uint(historyCoord.z) * probesPerAxis * probesPerAxis) * 7u;
        hasHistory = validity[historyBase + 6].w > 0.0;
    }

    // Inside a change box the history was traced against geometry that no longer exists
    // (block B.2 phase 4): it is replaced (regionBlendAlpha 1) or blended out over a few frames.
    // A single-frame trace is noisy, and the clamped SH evaluation of the consumers turns that
    // noise into a positive bias until the blend has averaged it, so a weight below 1 trades a
    // short fade of the stale history for a smaller transient.
    const bool fieldChanged = hasHistory && InChangedRegion(probePos);
    float a = hasHistory ? saturate(fieldChanged ? regionBlendAlpha : temporalBlendAlpha) : 1.0;
    const bool replace = !hasHistory || a >= 1.0;
    [unroll]
    for (uint k = 0; k < 7; ++k) {
        if (replace) {
            shBuf[base + k] = newPacked[k];
        } else {
            StructuredBuffer<float4> history = ResourceDescriptorHeap[g_shBufferSrvIndex];
            shBuf[base + k] = lerp(history[historyBase + k], newPacked[k], a);
        }
    }
    shBuf[base + 6].w = 1.0;
}
