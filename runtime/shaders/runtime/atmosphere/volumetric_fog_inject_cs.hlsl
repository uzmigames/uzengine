/// UzEngine Froxel Volumetric Fog — Inject phase (compute).
///
/// Evaluates density + inscattering at each froxel in the 160×90×64 grid.
/// Each thread handles one XY column and loops over Z slices.
///
/// @ref Hillaire 2015 "Physically Based & Unified Volumetric Rendering in Frostbite"
///      Section 5 — Light injection into froxel grid

#include "../common/constants.hlsli"

cbuffer VolumetricFogCB : register(b2) {
    row_major float4x4 invViewProj;
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
    float3 emissive;
    float  fogStartDistance;
    float  nearFadeInDistance;
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

/// Reconstruct world position from NDC + depth.
float3 FroxelWorldPos(uint2 xy, float depth)
{
    float2 uv = (float2(xy) + 0.5) / float2(gridX, gridY);
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;

    // Map depth to NDC Z (reversed-Z: near=1, far=0)
    // For volumetric fog, we use a simplified linear mapping
    float ndcZ = 1.0 - saturate(depth / farZ);

    float4 worldH = mul(float4(ndc, ndcZ, 1.0), invViewProj);
    return worldH.xyz / worldH.w;
}

// ─── Main compute — each thread covers one XY column, loops Z ──────────────

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= gridX || DTid.y >= gridY) return;

    RWTexture3D<float4> inscatteringVol = ResourceDescriptorHeap[inscatteringUavIdx];

    for (uint z = 0; z < gridZ; ++z) {
        // Compute froxel center depth (exponential distribution)
        float depth = SliceToDepth(z, gridZ);

        // Reconstruct world position at froxel center
        float3 worldPos = FroxelWorldPos(DTid.xy, depth);

        // ── Density evaluation: exponential height fog ──────────────
        // @ref UE5/Frostbite — density decreases exponentially with altitude
        float altitude = worldPos.z - heightOffset;  // Z-up
        float density = globalDensity * exp(-heightFalloff * max(altitude, 0.0) * kKmPerCm);

        // Extinction coefficient
        float extinction = density * extinctionScale;

        // ── In-scattering: sun + ambient ────────────────────────────
        // @ref Hillaire 2015 — single-scattering from directional light
        float3 viewDir = normalize(worldPos - cameraPos);
        float cosTheta = dot(viewDir, sunDir);
        float phase = HenyeyGreenstein(cosTheta, phaseG);

        // Sun contribution: directional light × HG phase
        float3 sunInscatter = albedo * extinction * phase * sunColor * sunIntensity;

        // Ambient contribution: isotropic sky light (1/4π phase = ~0.08)
        // This prevents fog from just darkening the scene — it adds the
        // characteristic bright haze that makes fog look like fog.
        // @ref Frostbite — ambient fog = skyColor × extinction × isotropicPhase
        float3 ambientFogColor = albedo * 0.3;  // ~30% of albedo as ambient sky
        float3 ambientInscatter = ambientFogColor * extinction * M_1_PI * 0.25;

        float3 inscatter = sunInscatter + ambientInscatter + emissive * density;

        // ── Write to 3D volume ──────────────────────────────────────
        // RGB = inscattering (additive light), A = extinction (for transmittance calc)
        inscatteringVol[uint3(DTid.xy, z)] = float4(inscatter, extinction);
    }
}
