/// UzEngine Froxel Volumetric Fog — Inject phase (compute).
///
/// Evaluates density + inscattering at each froxel of the 160×90×64 grid, one thread per froxel.
/// The froxel sits on its pixel's camera ray at the slice's view depth; the sun in-scatter is
/// shadowed by the cascaded shadow maps (one comparison tap per froxel, block B.4).
///
/// @ref Hillaire 2015 "Physically Based & Unified Volumetric Rendering in Frostbite"
///      Section 5 — Light injection into froxel grid
/// @constants The sun colour carries the intensity (sunIntensity is 1): the sun is applied once.
/// @constants Shared constant buffer: scalars only (an HLSL float3 at a 4-byte-aligned offset
///            moves to the next 16-byte row; scripts/rendering/test_cbuffer_layouts.py checks the offsets).

#include "../common/constants.hlsli"

/// shadow.hlsli reads the PCF penumbra radius; the volume helper takes one tap and ignores it.
static const float shadowPCFPenumbra = 2.0;
#include "../lighting/shadow.hlsli"

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
};

// ─── Depth slicing — exponential distribution ──────────────────────────────

/// @ref Hillaire 2015 — exponential depth distribution
/// @ref UE5 GetVolumetricFogGridZParams — start offset pushes grid away from camera
/// More resolution near camera where fog detail matters most.
float SliceToDepth(uint slice, uint numSlices)
{
    float startZ = max(nearZ, fogStartDistance); // UE5: push start past near clip
    float t = (float(slice) + 0.5) / float(numSlices);
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

/// The froxel of column xy at view depth `viewDepth`: on the pixel's camera ray, at the distance
/// where its view-space depth equals the slice depth (what the composite looks up).
/// The previous form fed 1 - depth / farZ to the finite reversed-Z inverse projection, which put
/// every froxel 5-60 cm from the camera (block B.4).
float3 FroxelWorldPos(uint2 xy, float viewDepth)
{
    float2 uv = (float2(xy) + 0.5) / float2(gridX, gridY);
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float3 ray     = normalize(UnprojectFar(ndc) - cameraPos);
    float3 forward = normalize(UnprojectFar(float2(0.0, 0.0)) - cameraPos);
    return cameraPos + ray * (viewDepth / max(dot(ray, forward), 1e-3));
}

// ─── Main compute — one thread per froxel ──────────────────────────────────

[numthreads(8, 8, 4)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= gridX || DTid.y >= gridY || DTid.z >= gridZ) return;

    RWTexture3D<float4> inscatteringVol = ResourceDescriptorHeap[inscatteringUavIdx];

    const uint z = DTid.z;
    // Froxel center depth (exponential distribution) and its world position on the camera ray.
    float depth = SliceToDepth(z, gridZ);
    float3 worldPos = FroxelWorldPos(DTid.xy, depth);

    // ── Density evaluation: exponential height fog ──────────────
    // @ref UE5/Frostbite — density decreases exponentially with altitude
    float altitude = worldPos.z - heightOffset;  // Z-up
    float density = globalDensity * exp(-heightFalloff * max(altitude, 0.0) * kKmPerCm);

    // Extinction coefficient
    float extinction = density * extinctionScale;

    // ── In-scattering: sun + ambient ────────────────────────────
    // @ref Hillaire 2015 — single-scattering from directional light
    float3 toFroxel = worldPos - cameraPos;
    float3 viewDir = normalize(toFroxel);
    float cosTheta = dot(viewDir, sunDir);
    float phase = HenyeyGreenstein(cosTheta, phaseG);

    // Sun contribution: directional light × HG phase × cascaded-shadow visibility. The colour
    // already carries the intensity (it was multiplied by |colour| again before block B.4).
    float sunVisibility = (sunShadowEnabled != 0u) ? CascadeShadowVolume(worldPos, length(toFroxel)) : 1.0;
    float3 sunInscatter = albedo * extinction * phase * sunColor * sunVisibility;

    // Ambient contribution: isotropic sky light (1/4π phase = ~0.08)
    // This prevents fog from just darkening the scene — it adds the
    // characteristic bright haze that makes fog look like fog.
    // @ref Frostbite — ambient fog = skyColor × extinction × isotropicPhase
    float3 ambientFogColor = albedo * 0.3;  // ~30% of albedo as ambient sky
    float3 ambientInscatter = ambientFogColor * extinction * M_1_PI * 0.25;

    float3 emissive = float3(emissiveR, emissiveG, emissiveB);
    float3 inscatter = sunInscatter + ambientInscatter + emissive * density;

    // ── Write to 3D volume ──────────────────────────────────────
    // RGB = inscattering (additive light), A = extinction (for transmittance calc)
    inscatteringVol[uint3(DTid.xy, z)] = float4(inscatter, extinction);
}
