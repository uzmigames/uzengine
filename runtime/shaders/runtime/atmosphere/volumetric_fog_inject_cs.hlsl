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
    uint   integratedSrvIdx;
    uint   depthSrvIdx;
    uint   frameIndex;
    uint   gridX;
    uint   gridY;
    uint   gridZ;
    float  extinctionScale;
    float3 emissive;
};

// ─── Depth slicing — exponential distribution ──────────────────────────────

/// @ref Hillaire 2015 — exponential depth distribution
/// slice_depth = nearZ * (farZ/nearZ)^((slice+0.5)/numSlices)
/// More resolution near camera where fog detail matters most.
float SliceToDepth(uint slice, uint numSlices)
{
    float t = (float(slice) + 0.5) / float(numSlices);
    return nearZ * pow(farZ / max(nearZ, 0.01), t);
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

        // ── In-scattering: sun light × phase × density ──────────────
        // Compute view direction for this froxel
        float3 viewDir = normalize(worldPos - cameraPos);
        float cosTheta = dot(viewDir, sunDir);
        float phase = HenyeyGreenstein(cosTheta, phaseG);

        // Inscattering = albedo × extinction × phase × sunRadiance + emissive
        float3 inscatter = albedo * extinction * phase * sunColor * sunIntensity
                         + emissive * density;

        // ── Write to 3D volume ──────────────────────────────────────
        // RGB = inscattering (additive light), A = extinction (for transmittance calc)
        inscatteringVol[uint3(DTid.xy, z)] = float4(inscatter, extinction);
    }
}
