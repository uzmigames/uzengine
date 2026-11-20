/// UzEngine Froxel Volumetric Fog — Inject phase (compute).
///
/// Evaluates density + in-scattering at each froxel of the 160×90×64 grid, one thread per froxel.
/// The froxel sits on its pixel's camera ray at the slice's view depth (the sample position is
/// jittered inside the froxel per frame when the temporal filter integrates it). Lights:
///   - the sun, shadowed by the cascaded shadow maps (one comparison tap per froxel),
///   - the point and spot lights of the frame (LightingCB at b1), shadowed by the local shadow
///     atlas (one linear-distance comparison tap per froxel),
///   - an isotropic sky ambient (the sky irradiance SH L0).
/// Each light is weighted by the Henyey-Greenstein phase of the angle between the view ray and
/// the light direction. Output: (in-scatter rgb, extinction) per froxel, both per unit length.
///
/// @ref Hillaire 2015 "Physically Based & Unified Volumetric Rendering in Frostbite"
///      Section 5 — Light injection into froxel grid; temporal jitter of the sample position
/// @ref Wronski 2014 "Volumetric Fog: Unified Compute Shader Based Solution to Atmospheric Scattering"
/// @ref Henyey & Greenstein 1941 — single-term phase function
/// @constants The sun colour carries the intensity (sunIntensity is 1): the sun is applied once.
/// @constants Shared constant buffer: scalars only (an HLSL float3 at a 4-byte-aligned offset
///            moves to the next 16-byte row; scripts/rendering/test_cbuffer_layouts.py checks the offsets).

#include "../common/constants.hlsli"

/// shadow.hlsli reads the PCF penumbra radius; the volume helper takes one tap and ignores it.
static const float shadowPCFPenumbra = 2.0;
#include "../lighting/shadow.hlsli"

// The light structs only; the frame's LightingCB is bound at b1 here (b2 holds VolumetricFogCB).
#define UZ_LIGHT_TYPES_NO_CB
#include "../lighting/light_types.hlsli"

/// A prefix of LightingCB (constant_buffers.h): the point and spot light arrays and their counts.
cbuffer LightingCB : register(b1) {
    PointLightData pointLights[kMaxPointLights]; // 32768 bytes
    uint pointLightCount;                        // 32768
    uint spotLightCount;                         // 32772
    uint2 _lightPad;
    SpotLightData spotLights[kMaxSpotLights];    // 32784, 10240 bytes
};

cbuffer VolumetricFogCB : register(b2) {
    row_major float4x4 invViewProj;   // unjittered inverse view-projection
    float3 cameraPos;
    float  nearZ;
    float  farZ;
    float  globalDensity;
    float  heightFalloff;
    float  heightOffset;
    float3 albedo;
    float  phaseG;
    float3 sunDir;
    float  sunIntensity;
    float3 sunColor;
    float  temporalAlpha;
    uint   inscatteringUavIdx;
    uint   inscatteringSrvIdx;
    uint   integratedUavIdx;
    uint   integratedSrvIdx;
    uint   depthSrvIdx;
    uint   frameIndex;
    uint   gridX;
    uint   gridY;
    uint   gridZ;
    float  extinctionScale;
    float  emissiveR;                 // scalars: a float3 here would be padded to offset 192 (C++ has 184)
    float  emissiveG;
    float  emissiveB;
    float  fogStartDistance;
    float  nearFadeInDistance;
    float  cameraFarZ;                // camera far plane (cm) for the composite's depth linearisation
    uint   sunShadowEnabled;          // r.VolumetricFog.SunShadow
    uint   debugMode;                 // r.VolumetricFog.Debug (composite)
    uint   historySrvIdx;             // temporal history (scatter)
    uint   historyUavIdx;
    uint   temporalEnabled;
    uint   localLightsEnabled;        // r.VolumetricFog.LocalLights
    uint   localShadowEnabled;        // r.VolumetricFog.LocalShadow
    uint   jitterEnabled;             // r.VolumetricFog.Jitter (only with the temporal filter)
    uint   debugStage;                // r.VolumetricFog.Stage: 0 full, 1 solid, 2 density, 3 + sun, 4 + shadows, 5 + temporal
    float  temporalClamp;
    float  ambientR;                  // sky ambient radiance (scalars, offset 248)
    float  ambientG;
    float  ambientB;
    float  localLightScale;           // multiplier on the point / spot in-scatter (offset 260)
    float  _fogPad0b;
    float  _fogPad0c;
    row_major float4x4 prevViewProj;  // offset 272
};

// ─── Depth slicing — exponential distribution ──────────────────────────────

/// @ref Hillaire 2015 — exponential depth distribution
/// @ref UE5 GetVolumetricFogGridZParams — start offset pushes grid away from camera
/// More resolution near camera where fog detail matters most. `t` in [0, 1] is the continuous
/// slice coordinate ((slice + 0.5) / numSlices at a froxel centre).
float SliceCoordToDepth(float t)
{
    float startZ = max(nearZ, fogStartDistance); // UE5: push start past near clip
    return startZ * pow(farZ / max(startZ, 0.01), t);
}

/// Henyey-Greenstein phase function.
/// @ref Henyey & Greenstein 1941 — single-term HG phase
/// @constants HG normalization = 1 / (4 * PI)
float HenyeyGreenstein(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 / (4.0 * PI)) * (1.0 - g2) / (denom * sqrt(max(denom, 1e-6)));
}

/// World position of the far-plane point behind an NDC xy (reversed-Z: the far plane is z = 0).
float3 UnprojectFar(float2 ndc)
{
    float4 worldH = mul(float4(ndc, 0.0, 1.0), invViewProj);
    return worldH.xyz / worldH.w;
}

/// The point of column uv (grid-normalised, [0, 1]) at view depth `viewDepth`: on the camera ray
/// through it, at the distance where its view-space depth equals the slice depth (what the
/// composite looks up).
float3 FroxelWorldPos(float2 uv, float viewDepth)
{
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float3 ray     = normalize(UnprojectFar(ndc) - cameraPos);
    float3 forward = normalize(UnprojectFar(float2(0.0, 0.0)) - cameraPos);
    return cameraPos + ray * (viewDepth / max(dot(ray, forward), 1e-3));
}

// ─── Per-frame sample jitter inside the froxel ─────────────────────────────

/// Hash of the froxel coordinate, so neighbours do not share their offset.
/// @ref Jarzynski & Olano 2020 "Hash Functions for GPU Rendering" (pcg3d)
uint3 Pcg3d(uint3 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return v;
}

/// Offset in [-0.5, 0.5]^3 froxel units: the froxel hash plus the frame's R3 sequence point, so
/// the temporal average integrates the froxel's volume.
/// @ref Roberts 2018 "The Unreasonable Effectiveness of Quasirandom Sequences" (R3)
float3 FroxelJitter(uint3 froxel)
{
    const float3 r3 = float3(0.8191725134, 0.6710436067, 0.5497004779);
    float3 seq = frac(float(frameIndex % 1024u) * r3);
    float3 hash = float3(Pcg3d(froxel) & 0xFFFFu) / 65535.0;
    return frac(hash + seq) - 0.5;
}

// ─── Local light helpers (the deferred pass's attenuation and atlas lookup, without a normal) ───

/// @ref Karis 2013 — windowed inverse-square falloff; UE5 RadialAttenuation for the custom exponent
float LocalLightAttenuation(float distSq, float radius, float falloffExponent, uint flags)
{
    float invRadSq = 1.0 / (radius * radius);
    float f = distSq * invRadSq;
    if ((flags & kLightFlag_InverseSquared) != 0u) {
        float s = saturate(1.0 - f * f);
        return (s * s) / max(distSq, 0.01);
    }
    return pow(saturate(1.0 - f), max(falloffExponent, 1e-3));
}

/// One comparison tap in a local light's atlas tile for a point in mid-air: the tile stores
/// distance / radius (shadow_depth_linear_ps.hlsl), the reference is the froxel's own distance
/// minus the tile's bias. No normal offset (a volume has no surface).
float LocalShadowVolumeTap(uint entry, float3 worldPos)
{
    if (localShadowAtlasIndex == kSentinelSrvIndex) return 1.0;
    Texture2D<float> localAtlas = ResourceDescriptorHeap[localShadowAtlasIndex];
    const uint base = entry * 6u;
    const float4x4 localVP = float4x4(localShadowRaw[base + 0], localShadowRaw[base + 1],
                                      localShadowRaw[base + 2], localShadowRaw[base + 3]);
    const float4 atlasParams = localShadowRaw[base + 4];
    const float4 lightParams = localShadowRaw[base + 5];
    const float4 lsPos = mul(float4(worldPos, 1.0), localVP);
    if (lsPos.w <= 0.0) return 1.0;
    const float2 ndc = lsPos.xy / lsPos.w;
    if (any(abs(ndc) > 1.0)) return 1.0;
    const float2 uv = ndc * float2(0.5, -0.5) + 0.5;           // same y flip as the CSM (shadow.hlsli)
    const float2 atlasUV = uv * atlasParams.z + atlasParams.xy;
    const float  radius = max(lightParams.w, 1e-3);
    const float  ref = (length(lightParams.xyz - worldPos) - atlasParams.w) / radius;
    return localAtlas.SampleCmpLevelZero(shadowSampler, atlasUV, saturate(ref));
}

/// Cube face of a point light's six consecutive tiles (+X, -X, +Y, -Y, +Z, -Z, the order
/// frame_constants.cpp allocates them) for the light -> point direction.
uint PointLightFace(float3 d)
{
    const float3 a = abs(d);
    return (a.x >= a.y && a.x >= a.z) ? (d.x > 0.0 ? 0u : 1u)
         : (a.y >= a.z)               ? (d.y > 0.0 ? 2u : 3u)
                                      : (d.z > 0.0 ? 4u : 5u);
}

/// In-scattered radiance (per unit length, before albedo x extinction) from the frame's point and
/// spot lights at `worldPos`, seen along `viewDir` (camera -> froxel).
float3 LocalLightsInscatter(float3 worldPos, float3 viewDir, bool shadowed)
{
    float3 sum = float3(0.0, 0.0, 0.0);

    for (uint pi = 0; pi < pointLightCount; ++pi) {
        PointLightData pl = pointLights[pi];
        float3 toLight = pl.position - worldPos;
        float  distSq  = dot(toLight, toLight);
        if (distSq > pl.radius * pl.radius) continue;
        float3 L = toLight * rsqrt(max(distSq, 1e-4));
        float atten = LocalLightAttenuation(distSq, pl.radius, pl.falloffExponent, pl.flags);
        if (shadowed && pl.shadowIndex != 0xFFFFFFFFu && pl.shadowIndex + 5u < localShadowCount)
            atten *= LocalShadowVolumeTap(pl.shadowIndex + PointLightFace(-toLight), worldPos);
        sum += pl.color * (atten * HenyeyGreenstein(dot(viewDir, L), phaseG));
    }

    for (uint si = 0; si < spotLightCount; ++si) {
        SpotLightData sl = spotLights[si];
        float3 toLight = sl.position - worldPos;
        float  distSq  = dot(toLight, toLight);
        if (distSq > sl.radius * sl.radius) continue;
        float3 L = toLight * rsqrt(max(distSq, 1e-4));
        float cosA = dot(L, -sl.direction);
        float cone = saturate((cosA - sl.cosOuterAngle) * sl.invCosConeDifference);
        float atten = LocalLightAttenuation(distSq, sl.radius, sl.falloffExponent, sl.flags) * cone * cone;
        if (atten <= 0.0) continue;
        if (shadowed && sl.shadowIndex != 0xFFFFFFFFu && sl.shadowIndex < localShadowCount)
            atten *= LocalShadowVolumeTap(sl.shadowIndex, worldPos);
        sum += sl.color * (atten * HenyeyGreenstein(dot(viewDir, L), phaseG));
    }
    return sum;
}

// ─── Main compute — one thread per froxel ──────────────────────────────────

[numthreads(8, 8, 4)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= gridX || DTid.y >= gridY || DTid.z >= gridZ) return;

    RWTexture3D<float4> inscatteringVol = ResourceDescriptorHeap[inscatteringUavIdx];

    // ── Stage 1 (r.VolumetricFog.Stage 1): a solid two-tone 8-froxel checkerboard, a constant
    // extinction. Proves the grid addressing and the composite path with nothing else involved.
    if (debugStage == 1u) {
        const bool odd = (((DTid.x >> 3u) + (DTid.y >> 3u) + (DTid.z >> 3u)) & 1u) != 0u;
        const float3 tone = odd ? float3(1.0, 0.45, 0.10) : float3(0.10, 0.45, 1.0);
        const float solidExtinction = 0.0005;                       // 20 m mean free path
        inscatteringVol[DTid] = float4(tone * solidExtinction * 0.5, solidExtinction);
        return;
    }

    // Froxel sample position: the centre, or a per-frame jittered point inside the froxel that the
    // temporal filter averages over (@ref Hillaire 2015 — temporal sample jitter).
    float3 jitter = (jitterEnabled != 0u) ? FroxelJitter(DTid) : float3(0.0, 0.0, 0.0);
    float  sliceT = (float(DTid.z) + 0.5 + jitter.z) / float(gridZ);
    float  depth  = SliceCoordToDepth(sliceT);
    float2 uv     = (float2(DTid.xy) + 0.5 + jitter.xy) / float2(gridX, gridY);
    float3 worldPos = FroxelWorldPos(uv, depth);

    // ── Density evaluation: exponential height fog ──────────────
    // @ref UE5/Frostbite — density decreases exponentially with altitude (heightFalloff per metre)
    float altitude = worldPos.z - heightOffset;  // Z-up, cm
    float density = globalDensity * exp(-heightFalloff * max(altitude, 0.0) * 0.01);

    // Extinction coefficient (per cm)
    float extinction = density * extinctionScale;

    // ── In-scattering ───────────────────────────────────────────
    // @ref Hillaire 2015 — single scattering: sum over lights of L_i * phase * visibility
    float3 toFroxel = worldPos - cameraPos;
    float3 viewDir = normalize(toFroxel);

    // Isotropic sky ambient: the sky's average radiance scattered once (stage 2 = this term only).
    float3 inscatterRadiance = float3(ambientR, ambientG, ambientB);

    if (debugStage == 0u || debugStage >= 3u) {
        // Sun: directional light x HG phase x cascaded-shadow visibility (stages 4+ only; the
        // colour already carries the intensity).
        const bool sunShadowed = sunShadowEnabled != 0u && (debugStage == 0u || debugStage >= 4u);
        float sunVisibility = sunShadowed ? CascadeShadowVolume(worldPos, length(toFroxel)) : 1.0;
        float sunPhase = HenyeyGreenstein(dot(viewDir, sunDir), phaseG);
        inscatterRadiance += sunColor * (sunPhase * sunVisibility);

        // Point and spot lights, local-atlas shadowed at stages 4+.
        if (localLightsEnabled != 0u) {
            const bool localShadowed = localShadowEnabled != 0u && (debugStage == 0u || debugStage >= 4u);
            inscatterRadiance += localLightScale * LocalLightsInscatter(worldPos, viewDir, localShadowed);
        }
    }

    float3 emissive = float3(emissiveR, emissiveG, emissiveB);
    float3 inscatter = albedo * extinction * inscatterRadiance + emissive * density;

    // ── Write to 3D volume ──────────────────────────────────────
    // RGB = in-scatter (radiance per unit length), A = extinction (per unit length)
    inscatteringVol[DTid] = float4(inscatter, extinction);
}
