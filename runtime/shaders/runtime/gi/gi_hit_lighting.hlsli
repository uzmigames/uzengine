/// UzEngine GI Hit Lighting — shared surface-cache-lite model for probe tracers.
///
/// Consumed by `radiance_cache_trace_cs.hlsl` (GDF sphere-trace backend) and
/// `ddgi_probe_update_cs.hlsl` (DXR RayQuery backend) so both compute the
/// SAME radiance at ray hit points: emissive + Lambertian response to
/// (shadowed sun irradiance + previous-frame probe-grid irradiance). The
/// feedback term is what turns single-bounce probes into converging
/// multi-bounce GI — each frame folds one more bounce into the grid.
/// @ref Majercik et al. 2019 "Dynamic Diffuse Global Illumination with
///      Ray-Traced Irradiance Fields" — §"infinite bounces" feedback
/// @ref Ramamoorthi & Hanrahan 2001 "An Efficient Representation for
///      Irradiance Environment Maps" — L2 SH irradiance
/// @ref Sloan 2008 "Stupid Spherical Harmonics (SH) Tricks" — packing
///
/// # SH conventions (MUST match the CPU exactly)
/// The engine-wide compact 7×float4 SH layout is produced by
/// `sh_projection.cpp::PackSHToCompactFormat` and evaluated by
/// `ibl.hlsli::EvalSH_L2` as plain dot products — the irradiance
/// convolution weights AND basis constants are folded into the packed
/// coefficients at pack time (UE5 GetSkySHDiffuse pattern). Probe tracers
/// therefore MUST accumulate raw radiance-projection coefficients with
/// `GISH_EvaluateBasis` (identical to the CPU `EvaluateSHBasis`) and pack
/// with `GISH_PackCompact` (identical to the CPU pack). Storing raw bands
/// in the compact slots — what this file replaces — evaluated with wrong
/// scale (L0 ~4× hot) and swapped/mis-signed L1/L2 lobes.

#ifndef UZ_GI_HIT_LIGHTING_HLSLI
#define UZ_GI_HIT_LIGHTING_HLSLI

static const float kGIHitPI = 3.14159265358979323846;

// ─── Raw L2 SH basis — EXACT port of sh_projection.cpp EvaluateSHBasis ──────
// Standard (unsigned) real SH basis, CPU ordering:
//   [0] Y0,0   [1] Y1,-1(y)  [2] Y1,0(z)  [3] Y1,1(x)
//   [4] Y2,-2(xy) [5] Y2,-1(yz) [6] Y2,0(3z²-1) [7] Y2,1(xz) [8] Y2,2(x²-y²)
void GISH_EvaluateBasis(float3 d, out float basis[9]) {
    basis[0] = 0.282094791773878;
    basis[1] = 0.488602511902920 * d.y;
    basis[2] = 0.488602511902920 * d.z;
    basis[3] = 0.488602511902920 * d.x;
    basis[4] = 1.092548430592079 * d.x * d.y;
    basis[5] = 1.092548430592079 * d.y * d.z;
    basis[6] = 0.315391565252520 * (3.0 * d.z * d.z - 1.0);
    basis[7] = 1.092548430592079 * d.x * d.z;
    basis[8] = 0.546274215296040 * (d.x * d.x - d.y * d.y);
}

// ─── Compact pack — EXACT port of sh_projection.cpp PackSHToCompactFormat ───
// Folds the cosine-lobe convolution + evaluation constants so that
// `EvalSH_L2` / `GISH_EvalIrradiance` are plain dot products.
// @ref UE5 ReflectionEnvironmentShared.ush — GetSkySHDiffuse coefficients
void GISH_PackCompact(float shR[9], float shG[9], float shB[9],
                      out float4 packed[7]) {
    const float sqrtPI = sqrt(kGIHitPI);
    const float c0 = 1.0 / (2.0 * sqrtPI);              // 0.282095
    const float c1 = sqrt(3.0)  / (3.0 * sqrtPI);       // 0.325735
    const float c2 = sqrt(15.0) / (8.0 * sqrtPI);       // 0.273137
    const float c3 = sqrt(5.0)  / (16.0 * sqrtPI);      // 0.078848
    const float c4 = 0.5 * c2;                          // 0.136569

    packed[0] = float4(-c1 * shR[3], -c1 * shR[1], c1 * shR[2], c0 * shR[0] - c3 * shR[6]);
    packed[1] = float4(-c1 * shG[3], -c1 * shG[1], c1 * shG[2], c0 * shG[0] - c3 * shG[6]);
    packed[2] = float4(-c1 * shB[3], -c1 * shB[1], c1 * shB[2], c0 * shB[0] - c3 * shB[6]);
    packed[3] = float4(c2 * shR[4], -c2 * shR[5], 3.0 * c3 * shR[6], -c2 * shR[7]);
    packed[4] = float4(c2 * shG[4], -c2 * shG[5], 3.0 * c3 * shG[6], -c2 * shG[7]);
    packed[5] = float4(c2 * shB[4], -c2 * shB[5], 3.0 * c3 * shB[6], -c2 * shB[7]);
    packed[6] = float4(c4 * shR[8], c4 * shG[8], c4 * shB[8], 1.0);
}

// ─── Irradiance evaluation — mirror of ibl.hlsli EvalSH_L2 ─────────────────
// Duplicated here (3 dot products + 2) so probe compute shaders can read the
// packed probe buffer without pulling ibl.hlsli's texture dependencies.
float3 GISH_EvalIrradiance(float4 packed[7], float3 N) {
    float4 nv = float4(N, 1.0);
    float3 e;
    e.r = dot(packed[0], nv);
    e.g = dot(packed[1], nv);
    e.b = dot(packed[2], nv);
    float4 vB = nv.xyzz * nv.yzzx;
    e.r += dot(packed[3], vB);
    e.g += dot(packed[4], vB);
    e.b += dot(packed[5], vB);
    e += packed[6].xyz * (N.x * N.x - N.y * N.y);
    return max(e, 0.0);
}

// ─── Hit radiance — the surface-cache-lite shading model ───────────────────
/// Outgoing radiance from a Lambertian hit surface.
///
/// # Arguments
/// * `albedo`     — Surface diffuse albedo (per-instance average or scene average).
/// * `emissive`   — Surface emitted radiance (0 when unknown).
/// * `N`          — Hit surface normal (GDF gradient or triangle geometric normal).
/// * `L`          — Normalized direction TOWARD the sun.
/// * `sunColorIntensity` — Sun irradiance on a perpendicular surface (color × intensity).
/// * `sunVisibility`     — [0,1] occlusion of the sun at the hit (shadow trace).
/// * `bounceIrradiance`  — Irradiance arriving from the previous frame's probe
///                         grid at the hit (multi-bounce feedback), 0 to disable.
///
/// Lambert: outgoing radiance = albedo/π × E. Loop gain of the feedback term
/// is exactly `albedo` (< 1), so iterated bounces form a convergent geometric
/// series — no energy blow-up with EMA-blended probes.
/// @ref Lambert; Majercik 2019 §"infinite bounces"
float3 GIHit_Radiance(float3 albedo, float3 emissive, float3 N,
                      float3 L, float3 sunColorIntensity, float sunVisibility,
                      float3 bounceIrradiance) {
    float  ndl = max(dot(N, L), 0.0);
    float3 e   = sunColorIntensity * ndl * sunVisibility + bounceIrradiance;
    return emissive + albedo * e / kGIHitPI;
}

#endif // UZ_GI_HIT_LIGHTING_HLSLI
