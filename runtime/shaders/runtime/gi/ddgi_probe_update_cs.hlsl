/// UzEngine DDGI Probe Update — DXR ray-traced octahedral atlas writer.
///
/// Updates the irradiance + depth atlases owned by `DDGISystem`. Each thread
/// updates one texel of one probe in either atlas (irradiance or depth — the
/// shader is dispatched twice per frame, once per atlas, with `g_atlasMode`
/// switching the write target). For each output texel:
///
///   1. Decode the (probe, octahedral xy) coordinates from the dispatch.
///   2. Decode the octahedral xy back into a unit direction.
///   3. Trace `raysPerProbe` rays at that direction (cone-blurred via the
///      basis dot weighting in Majercik 2019 §5).
///   4. Accumulate either irradiance (cosine-weighted radiance) or depth
///      statistics (mean distance, mean distance²).
///   5. EMA-blend with the previous frame for temporal stability.
///
/// Both atlases use a 1-texel border so bilinear lookups in deferred lighting
/// wrap around at octahedral seams.
///
/// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced
///                     Irradiance Probes" — atlas layout, Chebyshev visibility
/// @ref Cigolle et al. 2014 "A Survey of Efficient Representations for
///                           Independent Unit Vectors" — octahedral encoding
/// @ref Microsoft DirectX-Specs "Inline Raytracing (DXR 1.1)" — RayQuery<>

#include "../common/constants.hlsli"

// ─── Root constants at b0 ──────────────────────────────────────────────────

SamplerState linearClamp : register(s2);

cbuffer RootConstants : register(b0) {
    uint g_irradAtlasUav;      ///< Bindless UAV index — irradiance atlas (RGBA16F).
    uint g_depthAtlasUav;      ///< Bindless UAV index — depth atlas (RG16F).
    uint g_tlasSrvIndex;       ///< Bindless SRV index of the scene TLAS.
    uint g_atlasMode;          ///< 0 = irradiance pass, 1 = depth pass.
    uint g_frameIndex;         ///< Frame counter for temporal jitter.
    uint g_probesPerAxis;      ///< Probes per axis (cube grid).
    uint g_probeOffset;        ///< Reserved for temporal probe spreading.
    uint g_skyRadianceSrv;     ///< Sky radiance cubemap, the same one the SDF radiance cache
                               ///< samples for escaping rays (kSentinelSrvIndex = flat sky colour).
    uint g_diagnosticUav;      ///< Irradiance pass only: bindless UAV of the transport
                               ///< diagnostic (float2 per atlas texel: mean ray radiance
                               ///< luminance, sky fraction); 0xffffffff = none. Mirrors the
                               ///< SDF writer's `traceStats` so both writers can be compared
                               ///< probe by probe on the same scene (test_gi_energy_gpu.ps1).
    uint g_knownRadianceBits;  ///< Diagnostic (followup-01 consumer stage): float bits of a constant
                               ///< radiance every ray returns instead of what it traced; 0 = off.
    uint g_knownKeepDepth;     ///< With the above: 1 = keep the real hit distance (Chebyshev acts),
                               ///< 0 = report the far distance (visibility 1).
    uint g_knownRadianceMode;  ///< 0 = constant; 1 = directional L * (0.5 + 0.5 dot(dir, d0)); 2 = per-probe parity.
    uint g_knownDistanceBits;  ///< Diagnostic: float bits of a known hit distance every ray reports; 0 = off.
    uint g_knownDistanceMode;  ///< 0 = constant D; 1 = D * (0.75 + 0.25 dot(dir, d0)).
    uint g_farDepth;           ///< Diagnostic: 1 = the DEPTH pass stores the far distance for every ray
                               ///< (visibility 1 at the consumer) while the radiance stays real.
};

// ─── Trace constants at b2 (matches DDGI cb layout) ───────────────────────

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
    uint  shBufferUav;          // Unused by atlas writer.
    uint  firstWrite;           // Unused by atlas writer; the radiance cache uses
                                // it to replace instead of blend on the first pass.

    // GDF clipmap parameters. Unused by the DXR atlas writer, but they MUST stay
    // scalar to mirror RadianceCacheTraceConstants (208 bytes here: 176 shared +
    // the 16-byte VFX emissive tail + the 16-byte grid-recentre row): HLSL gives every array element its own
    // 16-byte register, so `float x[4]` occupies 64 bytes instead of 16. The four
    // arrays took 256 bytes where the C++ struct has 64, shifting every field
    // below by 192 bytes past the uploaded struct and feeding garbage sky/sun
    // colors to every probe ray.
    // @ref Microsoft HLSL "Packing Rules for Constant Variables"
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
    // Mirrors the trailing fields of `uz::RadianceCacheTraceConstants` in
    // `engine/renderer/gi/radiance_cache.h`. When `vfxEmissiveSampleCount`
    // is 0 (CVar off / no alive emitters / radiance-cache shader path) the
    // accumulation loop below is skipped at runtime and DXC dead-code
    // strips the entire branch on shader-stage scheduling.
    uint  vfxEmissiveSrvIndex;
    uint  vfxEmissiveSampleCount;
    float regionBlendAlpha;    ///< radiance cache only (block B.2 phase 4); layout mirror
    float _vfxEmissivePad1;

    // Radiance-cache grid-recentre row (history scroll); mirrored for layout
    // parity with `RadianceCacheTraceConstants`, not consumed by the DDGI writer.
    int   historyShiftX;
    int   historyShiftY;
    int   historyShiftZ;
    uint  _historyShiftPad;
};

// ─── VFX emissive sample (mirrors `uz::VfxEmissiveSample`) ───────────────
//
// Layout MUST match the CPU `struct VfxEmissiveSample` in
// `engine/vfx/lighting/vfx_ddgi_injection.h` byte-for-byte. The CPU side is:
//   Vec3  position;        // 12 bytes (3 floats, NOT a float4 — Vec3 is
//                          //          tightly packed with no trailing pad)
//   Vec3  emissive;        // 12 bytes
//   float influenceRadius; //  4 bytes
//   ───── total = 28 bytes ─────
//
// The structured buffer is created with `desc.stride = sizeof(VfxEmissiveSample)`
// = 28 in `vfx_ddgi_emissive_feed.cpp:53`. We must therefore declare the HLSL
// struct as scalars-only so DXC packs it tightly (28 bytes); a `float3 position`
// followed by `float3 emissive` would force `emissive` to offset 16 (HLSL
// disallows float3 straddling a 16-byte boundary), bloating the stride to 32
// and corrupting buffer reads.
struct VfxDDGIEmissiveSample {
    float positionX;        // offset 0
    float positionY;        // offset 4
    float positionZ;        // offset 8
    float emissiveX;        // offset 12
    float emissiveY;        // offset 16
    float emissiveZ;        // offset 20
    float influenceRadius;  // offset 24
};

// ─── Constants ────────────────────────────────────────────────────────────

static const float kPI            = 3.14159265358979323846;
static const float kRayTMin       = 1.0;          // cm — avoid self-hit
static const float kSurfaceBias   = 2.0;          // cm — shadow ray origin offset
static const uint  kIrradProbeSize = 6;           // matches DDGISystem::kIrradianceProbeSize
static const uint  kDepthProbeSize = 16;          // matches DDGISystem::kDepthProbeSize
static const uint  kMaxRaysPerProbe = 64;
static const float kSurfaceEpsilon = 5.0;         // cm — GDF normal central-difference offset

// Shared GDF surface sampling — supplies the hit normal and albedo that an
// inline RayQuery cannot report. Needs linearClamp + the trace cbuffer above.
#include "gdf_surface.hlsli"

// ─── Hammersley + spherical sample (shared with SDF shader) ──────────────

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

float3 SphericalSample(float2 xi) {
    float z = 1.0 - 2.0 * xi.x;
    float r = sqrt(max(0.0, 1.0 - z * z));
    float phi = 2.0 * kPI * xi.y;
    return float3(r * cos(phi), r * sin(phi), z);
}

// ─── Octahedral encode/decode (Cigolle 2014) ─────────────────────────────
//
// Maps a unit direction <-> [-1,1]^2. The decode used by the atlas reader
// must exactly mirror this so the per-texel direction is consistent.

float2 OctWrap(float2 v) {
    return (1.0 - abs(v.yx)) * float2(v.x >= 0.0 ? 1.0 : -1.0,
                                      v.y >= 0.0 ? 1.0 : -1.0);
}

float2 OctEncodeDir(float3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 e = (n.z >= 0.0) ? n.xy : OctWrap(n.xy);
    return e * 0.5 + 0.5; // map to [0,1]
}

float3 OctDecodeDir(float2 f) {
    f = f * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.x += (n.x >= 0.0) ? -t : t;
    n.y += (n.y >= 0.0) ? -t : t;
    return normalize(n);
}

// ─── Atlas tile layout helpers ───────────────────────────────────────────

uint3 ProbeIndexToCell(uint linearIdx) {
    uint x = linearIdx % g_probesPerAxis;
    uint y = (linearIdx / g_probesPerAxis) % g_probesPerAxis;
    uint z = linearIdx / (g_probesPerAxis * g_probesPerAxis);
    return uint3(x, y, z);
}

float3 ProbePosition(uint linearIdx) {
    uint3 c = ProbeIndexToCell(linearIdx);
    return float3(gridOriginX, gridOriginY, gridOriginZ)
         + float3(float(c.x), float(c.y), float(c.z)) * probeSpacing;
}

// Atlas tile = (probesPerAxis * probesPerAxis) tiles laid out across X,
// stacked probesPerAxis tiles along Y. Each tile is `(size+2)` to include
// the 1-texel border. Returns the texel coordinate of the probe's (0,0)
// corner inside the atlas, EXCLUDING the border (i.e. the first interior
// texel).
uint2 ProbeAtlasOrigin(uint linearIdx, uint tileSize) {
    uint stride = tileSize + 2u;
    uint tileX  = linearIdx % (g_probesPerAxis * g_probesPerAxis);
    uint tileY  = linearIdx / (g_probesPerAxis * g_probesPerAxis);
    return uint2(tileX * stride + 1u,
                 tileY * stride + 1u);
}

// ─── Sky radiance for a ray that escapes the scene ──────────────────────
//
// The SDF radiance cache samples the sky cubemap in the ray direction and multiplies by the sky
// colour (radiance_cache_trace_cs.hlsl). This path used a flat colour, which cost the DXR probes
// the cubemap's directional variation and made the two paths disagree (followup-01). Same
// cubemap, same Z-up to Y-up swizzle, same multiply.
float3 SkyRadiance(float3 dir) {
    const float3 tint = float3(skyColorR, skyColorG, skyColorB);
    if (g_skyRadianceSrv == kSentinelSrvIndex) {
        return tint;
    }
    TextureCube<float4> sky = ResourceDescriptorHeap[g_skyRadianceSrv];
    return sky.SampleLevel(linearClamp, float3(dir.x, dir.z, dir.y), 0).rgb * tint;
}

// ─── Sample radiance toward a single direction ──────────────────────────

// Known-radiance diagnostic (followup-01): mode 0 = constant L, mode 1 = L * (0.5 + 0.5 dot(dir, d0))
// with d0 = normalize(0.3, 0.5, 0.8), a smooth non-negative function of direction that crosses every
// octahedral tile edge and corner — a misplaced or inverted border copy shows against it.
static const float3 kKnownDir = float3(0.30305, 0.50508, 0.80812);
float3 KnownRadiance(float L, float3 dir, uint mode) {
    // mode 2 (per-probe parity) is applied by the caller through L itself; here it is a constant.
    const float v = (mode == 1u) ? L * (0.5 + 0.5 * dot(dir, kKnownDir)) : L;
    return float3(v, v, v);
}

float4 TraceProbeRay(float3 origin, float3 dir, out float4 hitInfo) {
    hitInfo = float4(0.0, 0.0, 0.0, 0.0);
    if (g_tlasSrvIndex == kSentinelSrvIndex) {
        return float4(SkyRadiance(dir), maxTraceDistance);
    }
    RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[g_tlasSrvIndex];

    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = dir;
    ray.TMin      = kRayTMin;
    ray.TMax      = maxTraceDistance;

    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
    q.Proceed();

    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        // Single-bounce sun lighting at the (unknown-normal) hit point.
        // Use the negated ray direction as a conservative outward normal —
        // accurate per-vertex normals require an SBT/hit-group pipeline.
        const float  hitT = q.CommittedRayT();
        const float3 hitP = origin + dir * hitT;

        // Surface attributes come from the global distance field, the same
        // source the SDF writer shades with. Inline RayQuery reports the hit
        // distance but no attributes, and `-dir` (the previous conservative
        // normal) makes every hit face the incoming ray, so an interior wall
        // was lit as if it were turned toward the sun. Fall back to `-dir`
        // only where the field has no gradient (outside every cascade).
        float3 N = GdfNormal(hitP);
        if (dot(N, N) < 0.5) N = -dir;
        float3 L    = normalize(float3(lightDirX, lightDirY, lightDirZ));
        float  NdL  = max(dot(N, L), 0.0);

        // Shadow ray. Without it every hit point is lit as if the sun were
        // unoccluded, so interior walls inside the atrium fed the probes with
        // full sunlight. The SDF writer has always done this test
        // (`TraceGDF(shadowOrigin, L, ...) == -1` in radiance_cache_trace_cs).
        float visibility = 0.0;
        if (NdL > 0.0) {
            RayDesc shadowRay;
            shadowRay.Origin    = hitP + N * kSurfaceBias;
            shadowRay.Direction = L;
            shadowRay.TMin      = kRayTMin;
            shadowRay.TMax      = maxTraceDistance;

            RayQuery<RAY_FLAG_FORCE_OPAQUE
                   | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> sq;
            sq.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, shadowRay);
            sq.Proceed();
            visibility = (sq.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0 : 1.0;
        }

        float3 sunRad = float3(lightColorR, lightColorG, lightColorB)
                      * lightIntensity * NdL * visibility;

        // Lambertian BRDF: the radiance leaving a diffuse surface is
        // `albedo/pi * E`, not `albedo * E`. The missing 1/pi made every
        // surface bounce pi times too bright, which is why the DXR path
        // measured 1.49x the SDF path's HDR mean on Sponza Camera A
        // (12.1 vs 8.1 at 1080p, auto exposure off, identical CVars).
        // The SDF writer already carries this term:
        //   `radiance = albedo * (sunRad / kPI + indirect)`.
        // @ref Majercik 2019 — probe atlases store E/pi (radiance-like).
        //
        // Albedo also comes from the field (GBA = diffuse reflectance), so both
        // writers bounce the same energy off the same surface; `groundAlbedo`
        // stays as the fallback for points outside every cascade.
        // Validity comes from the field, not from the colour: a point inside a
        // clipmap returns its distance (< 1e8) with its diffuse reflectance, and
        // black is a legitimate reflectance (dark dirt; the composite writes
        // zero for fully metallic surfaces on purpose). Only a hit outside every
        // cascade (distance sentinel 1e9) falls back to the average albedo.
        const float4 surface = SampleGdfSurface(hitP);
        const float3 albedo = surface.x < 1e8 ? saturate(surface.gba)
                                              : float3(groundAlbedo, groundAlbedo, groundAlbedo);
        // z: 1 when the committed triangle faced the ray (diagnostic, H-63 follow-on): whether the
        // rays of a probe the field calls "inside" find back faces, as a probe inside a closed
        // volume would, or front faces, as a probe in free space would. The trace culls nothing.
        hitInfo = float4((NdL * visibility) > 0.0 ? 1.0 : 0.0,
                         dot(albedo, float3(0.2126, 0.7152, 0.0722)),
                         q.CommittedTriangleFrontFace() ? 1.0 : 0.0, visibility);
        return float4(albedo * (sunRad / kPI), hitT);
    }
    return float4(SkyRadiance(dir), maxTraceDistance);
}

// ─── Entry point — one thread per atlas texel of one probe ───────────────
//
// Dispatch X = probesPerAxis * probesPerAxis * tileSize
// Dispatch Y = probesPerAxis * tileSize
// Dispatch Z = 1
//
// We hand-decode the probe index from the dispatch coordinates so the same
// shader works for both atlas resolutions (6 vs 16) without needing a
// permutation define — `g_atlasMode` selects the active path.

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint tileSize = (g_atlasMode == 0u) ? kIrradProbeSize : kDepthProbeSize;
    uint stride   = tileSize + 2u;

    // Which probe tile does this texel belong to?
    uint tileX = DTid.x / stride;
    uint tileY = DTid.y / stride;
    if (tileX >= g_probesPerAxis * g_probesPerAxis) return;
    if (tileY >= g_probesPerAxis) return;

    // Position inside the tile (skip the 1-texel border).
    uint localX = DTid.x % stride;
    uint localY = DTid.y % stride;
    if (localX == 0u || localX > tileSize) return;
    if (localY == 0u || localY > tileSize) return;

    // Linear probe index (matches `ProbeAtlasOrigin` packing).
    uint probeIdx = tileY * (g_probesPerAxis * g_probesPerAxis) + tileX;
    if (probeIdx >= totalProbes) return;

    // Octahedral coordinate inside the tile, [0,1).
    float2 octUV = (float2(float(localX - 1u) + 0.5,
                           float(localY - 1u) + 0.5)) / float(tileSize);
    float3 sampleDir = OctDecodeDir(octUV);

    float3 probePos = ProbePosition(probeIdx);

    // Trace `raysPerProbe` directions, weight by basis dot product so the
    // texel only integrates radiance from rays near `sampleDir`. This is
    // Majercik 2019 §5 — the per-texel cosine integration that gives DDGI
    // its smooth probe filtering without resampling at lookup time.
    uint nRays = min(raysPerProbe, kMaxRaysPerProbe);
    uint frameJitter = (g_frameIndex * 1664525u + 1013904223u);

    float3 accumIrrad = float3(0, 0, 0);
    float  accumWeight = 0.0;

    // Depth moments are stored normalised — see the accumulation below.
    const float invMaxTraceDistance = 1.0 / max(maxTraceDistance, 1e-4);

    float  accumDepth  = 0.0;
    float  accumDepth2 = 0.0;
    float  accumDepthW = 0.0;

    // Transport diagnostic: unweighted mean of the ray radiance luminance and the
    // fraction of rays that escaped to the sky — the same two statistics the SDF
    // writer records in `traceStats` (.w and .x), so the two writers can be compared
    // in linear radiance rather than through the tonemapped frame.
    float diagLum = 0.0;
    float diagSky = 0.0;
    float diagSkyLum = 0.0;   // luminance carried by the sky rays alone
    // Hit side (followup-01 stage 3): what the rays that did NOT escape found.
    float diagHitCount  = 0.0;
    float diagHitDist   = 0.0;
    float diagHitLit    = 0.0;
    float diagHitAlbedo = 0.0;
    float diagHitBack   = 0.0;   // hit rays whose committed triangle was back-facing

    [loop]
    for (uint i = 0; i < nRays; ++i) {
        // A full stratified set, rotated per frame and per probe (Cranley-Patterson), the
        // same construction as radiance_cache_trace_cs.hlsl. The previous
        // `Hammersley(i + j, nRays + (jitter & 0xFF))` was a contiguous window into a longer
        // sequence: its polar coordinate spanned ~32/N of [0,1), so each frame's rays covered a
        // band of the sphere (followup-01, H-57: mean 0.373 of a field whose mean is 0.5).
        float2 xi = frac(Hammersley(i, nRays)
                         + float2(RadicalInverseVdC(frameJitter),
                                  RadicalInverseVdC(frameJitter ^ probeIdx)));
        float3 rayDir = SphericalSample(xi);

        float4 hitInfo;
        float4 rt = TraceProbeRay(probePos, rayDir, hitInfo);
        float3 radiance = rt.rgb;
        float  hitT     = rt.a;
        // Diagnostic (followup-01, H-60 follow-on): real radiance, far depth — separates the visibility
        // test from the writer-side difference under real lighting. Depth pass only.
        if (g_farDepth != 0u && g_atlasMode == 1u) hitT = maxTraceDistance;
        {
            const float knownL = asfloat(g_knownRadianceBits);
            if (knownL > 0.0) {
                float knownLeff = knownL;
                if (g_knownRadianceMode == 2u) {
                    // Per-probe parity: odd cells L, even cells L/4 (followup-01 step 4).
                    const uint3 cell = ProbeIndexToCell(probeIdx);
                    knownLeff = ((cell.x + cell.y + cell.z) & 1u) != 0u ? knownL : 0.25 * knownL;
                }
                radiance = KnownRadiance(knownLeff, rayDir, g_knownRadianceMode);
                if (g_knownKeepDepth == 0u) hitT = maxTraceDistance;
                const float knownD = asfloat(g_knownDistanceBits);
                if (knownD > 0.0) {
                    // Known hit distance for the depth atlas: constant, or varying with direction.
                    hitT = (g_knownDistanceMode == 1u) ? knownD * (0.75 + 0.25 * dot(rayDir, kKnownDir)) : knownD;
                }
            }
        }
        if (hitT < maxTraceDistance) {
            diagHitCount  += 1.0;
            diagHitDist   += hitT;
            diagHitLit    += hitInfo.x;
            diagHitAlbedo += hitInfo.y;
            diagHitBack   += 1.0 - hitInfo.z;
        }
        diagLum += dot(radiance, float3(0.2126, 0.7152, 0.0722));
        diagSky += (hitT >= maxTraceDistance) ? 1.0 : 0.0;
        diagSkyLum += (hitT >= maxTraceDistance) ? dot(radiance, float3(0.2126, 0.7152, 0.0722)) : 0.0;

        // Weight by max(0, dot(sampleDir, rayDir)) — cosine of the angle
        // between the texel direction and the traced ray. This is the
        // per-texel integral that produces a smooth octahedral atlas.
        float w = max(dot(sampleDir, rayDir), 0.0);
        if (g_atlasMode == 0u) {
            accumIrrad  += radiance * w;
            accumWeight += w;
        } else {
            // Depth atlas uses a sharper cone (square the weight) to
            // preserve depth discontinuities for the Chebyshev test.
            //
            // Store the distance NORMALISED by maxTraceDistance. The atlas is
            // RG16F and the second moment is a squared distance: at Sponza's
            // scale (cm, maxTraceDistance = probeSpacing*16*2 ~ 4000) d^2 is
            // ~1.6e7 and saturates FP16's 65504 limit. A saturated second
            // moment makes `variance = max(meanD2 - meanD*meanD, 1e-4)`
            // collapse to 1e-4 and, because `distToProbe < meanD` then holds
            // everywhere, Chebyshev returned visibility = 1 for every probe —
            // DDGI had no occlusion at all and lit through walls.
            // Measured before this change: gi.ddgi_depth max = 65504 (FP16 max).
            float dw = w * w;
            float hitNorm = hitT * invMaxTraceDistance;
            accumDepth  += hitNorm * dw;
            accumDepth2 += hitNorm * hitNorm * dw;
            accumDepthW += dw;
        }
    }

    if (g_atlasMode == 0u) {
        if (g_diagnosticUav != 0xffffffffu) {
            RWStructuredBuffer<float4> diagnostic = ResourceDescriptorHeap[g_diagnosticUav];
            const uint texel = (localY - 1u) * tileSize + (localX - 1u);
            const float invRays = 1.0 / float(max(nRays, 1u));
            const float invHits = 1.0 / max(diagHitCount, 1.0);
            const uint  rec = (probeIdx * (tileSize * tileSize) + texel) * 2u;
            // w: back-face fraction over the HIT rays (H-63 follow-on); zero without hits.
            diagnostic[rec] = float4(diagLum * invRays, diagSky * invRays,
                                     diagSkyLum * invRays, diagHitBack * invHits);
            // Hit side: mean distance, lit fraction and mean albedo over the HIT rays only
            // (zero when the probe has none), and the hit count itself.
            diagnostic[rec + 1] = float4(diagHitDist * invHits, diagHitLit * invHits,
                                         diagHitAlbedo * invHits, diagHitCount);
        }
        float3 irrad = (accumWeight > 1e-6) ? (accumIrrad / accumWeight)
                                            : float3(0, 0, 0);

        // ── VFX emissive injection (band-0 / omnidirectional) ───────────────
        //
        // For each VFX emissive sample, compute the Karis attenuation between
        // probe centre and sample position and add the resulting radiance
        // uniformly to every octahedral texel of this probe. The contribution
        // is direction-independent (band-0 / SH[0]) — a fireball located near
        // a probe lights every direction equally, modulo the smooth radius
        // cutoff. This matches the proposal's "omnidirectional irradiance"
        // semantics for the atlas variant of the DDGI update.
        //
        // CPU mirror — `VfxDDGIAttenuation` /  `VfxDDGIProbeContribution` in
        //   runtime/engine/vfx/lighting/vfx_ddgi_injection.cpp:21-47
        //
        //   float dOverR = d / r;
        //   float s      = max(0, 1 - dOverR^4);
        //   float s2     = s * s;
        //   float invSq  = 1 / (d*d + 1);
        //   return s2 * invSq;
        //
        // The HLSL below reproduces this byte-for-byte (same operands, same
        // ordering, same denominator constant). Verified item 1.4.
        //
        // The gate is doubly-conservative: the runtime sample count must be
        // non-zero AND the SRV index must not be sentinel. When the
        // `r.VFX.DDGIInjection` CVar is off, `GatherAndUploadVfxEmissiveSamples`
        // calls `m_vfxDDGIEmissiveFeed.Reset()` which forces `sampleCount == 0`,
        // skipping the loop entirely.
        if (vfxEmissiveSampleCount > 0u && vfxEmissiveSrvIndex != 0xFFFFFFFFu) {
            StructuredBuffer<VfxDDGIEmissiveSample> emissiveSamples =
                ResourceDescriptorHeap[NonUniformResourceIndex(vfxEmissiveSrvIndex)];

            [loop]
            for (uint vi = 0u; vi < vfxEmissiveSampleCount; ++vi) {
                VfxDDGIEmissiveSample s = emissiveSamples[vi];

                if (s.influenceRadius <= 0.0f) continue;

                // probePos - samplePos: distance vector from sample to probe.
                // CPU helper uses `sample.position - probePosition` then takes
                // its magnitude — only `d` is used so sign doesn't matter.
                float dx = probePos.x - s.positionX;
                float dy = probePos.y - s.positionY;
                float dz = probePos.z - s.positionZ;
                float distSq = dx * dx + dy * dy + dz * dz;
                float d      = sqrt(distSq);

                // Smooth radius cutoff: matches CPU early-out `d >= radius`.
                if (d >= s.influenceRadius) continue;

                float dOverR = d / s.influenceRadius;
                float dOverR2 = dOverR * dOverR;
                float dOverR4 = dOverR2 * dOverR2;
                float sCutoff = max(0.0f, 1.0f - dOverR4);
                float s2 = sCutoff * sCutoff;
                float invSq = 1.0f / (distSq + 1.0f);
                float falloff = s2 * invSq;

                irrad.x += s.emissiveX * falloff;
                irrad.y += s.emissiveY * falloff;
                irrad.z += s.emissiveZ * falloff;
            }
        }

        // EMA hysteresis blend with the previous frame.
        RWTexture2D<float4> irradTex = ResourceDescriptorHeap[g_irradAtlasUav];
        uint2 atlasOrigin = ProbeAtlasOrigin(probeIdx, kIrradProbeSize);
        uint2 atlasXY     = atlasOrigin + uint2(localX - 1u, localY - 1u);

        float4 prev = irradTex[atlasXY];
        float a = saturate(temporalBlendAlpha);
        float4 blended = lerp(prev, float4(irrad, 1.0), a);
        irradTex[atlasXY] = blended;
    } else {
        // Both moments are in normalised units [0,1]; the consumer rescales
        // by maxTraceDistance (derived from probe spacing) before the
        // Chebyshev comparison.
        float meanD  = (accumDepthW > 1e-6) ? (accumDepth  / accumDepthW) : 1.0;
        float meanD2 = (accumDepthW > 1e-6) ? (accumDepth2 / accumDepthW) : 1.0;

        RWTexture2D<float2> depthTex = ResourceDescriptorHeap[g_depthAtlasUav];
        uint2 atlasOrigin = ProbeAtlasOrigin(probeIdx, kDepthProbeSize);
        uint2 atlasXY     = atlasOrigin + uint2(localX - 1u, localY - 1u);

        float2 prev = depthTex[atlasXY];
        float a = saturate(temporalBlendAlpha);
        float2 blended = lerp(prev, float2(meanD, meanD2), a);
        depthTex[atlasXY] = blended;
    }
}
