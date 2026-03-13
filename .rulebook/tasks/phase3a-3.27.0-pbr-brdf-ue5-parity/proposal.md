# PBR BRDF — UE5 Parity

## Priority: P0 — Core visual quality (every pixel is affected)

## Summary

Upgrade UzEngine's PBR BRDF to match UE5's exact formulation. Three critical differences: (1) replace Lambertian diffuse with Burley (Disney) diffuse for angle-dependent response, (2) replace Schlick-GGX geometry with Smith Joint GGX for proper height-correlated masking, (3) add multiscatter energy compensation (Kulla-Conty) to prevent energy loss at high roughness.

## Current UzEngine BRDF vs UE5

| Component | UzEngine (Current) | UE5 (Target) | Visual Impact |
|-----------|-------------------|---------------|---------------|
| **NDF** | GGX ✅ | GGX | None — already matched |
| **Geometry** | Schlick-GGX (k=α/2) | **Smith Joint GGX** (height-correlated) | Medium — grazing angle accuracy |
| **Fresnel** | Schlick ✅ | Schlick | None — already matched |
| **Diffuse** | **Lambertian** (albedo/π) | **Burley (Disney)** | High — diffuse looks flat at grazing |
| **Multiscatter** | ❌ None | **Kulla-Conty compensation** | Medium — high roughness too dark |
| **Min roughness** | 0.04 ✅ | 0.045 | Negligible |
| **F0 dielectric** | 0.04 ✅ | 0.04 | None — already matched |

## UE5 Reference Formulas

### Burley Diffuse (UE5 ShadingModels.ush)
```hlsl
float3 Diffuse_Burley(float3 albedo, float roughness, float NdotV, float NdotL, float VdotH) {
    float fd90 = 0.5 + 2.0 * VdotH * VdotH * roughness;
    float lightScatter = F_Schlick(1.0, fd90, NdotL);  // 1 + (fd90-1)*(1-NdotL)^5
    float viewScatter  = F_Schlick(1.0, fd90, NdotV);
    return albedo * INV_PI * lightScatter * viewScatter;
}
```
**Why**: Lambertian is constant regardless of angle. Burley brightens at grazing angles (retro-reflection) and darkens at normal incidence for rough surfaces. Matches real-world cloth, wood, concrete.

### Smith Joint GGX (UE5 BRDF.ush, Vis_SmithJointApprox)
```hlsl
float Vis_SmithJointApprox(float a2, float NdotV, float NdotL) {
    float Vis_SmithV = NdotL * (NdotV * (1 - a) + a);
    float Vis_SmithL = NdotV * (NdotL * (1 - a) + a);
    return 0.5 * rcp(Vis_SmithV + Vis_SmithL);
}
```
**Why**: Schlick-GGX separates masking/shadowing as independent (`G1(V) * G1(L)`). Smith Joint properly correlates them — the same microfacet that shadows one direction is more likely to shadow the other. More physically accurate, especially at grazing angles.

### Multiscatter Energy Compensation (UE5 — Kulla-Conty via preintegration)
At high roughness, single-scatter BRDF loses energy (light bounces between microfacets aren't counted). UE5 compensates via:
```hlsl
// BRDF LUT stores: x=scale, y=bias for single-scatter
// Multiscatter factor:
float energyCompensation = 1.0 / (brdfLUT.x + brdfLUT.y) - 1.0;
specular *= (1.0 + F0 * energyCompensation);
```
**Why**: Without this, roughness=1.0 surfaces appear ~30% too dark. Most visible on rough metals.

### ACES Tonemap Upgrade (UE5 — 3-segment piecewise vs Narkowicz simple)
UE5 uses a fitted 3-segment curve with separate toe/mid/shoulder:
- Toe: `(x * ToeNum) / (x * ToeDen + 1)` — controls shadow rolloff
- Mid: `x * MidNum / (x * MidDen + 1)` — linear-ish
- Shoulder: `(x * ShoulderNum) / (x * ShoulderDen + 1)` — highlight compression
- Default params: Slope=0.91, Toe=0.53, Shoulder=0.23, BlackClip=0, WhiteClip=0.04

UzEngine currently uses Narkowicz 2015 (single rational function) which is a close approximation but lacks per-segment control.

## Impact

- **Files changed**: `pbr_common.hlsli` (BRDF functions), `mesh_ps.hlsl`, `deferred_lighting_ps.hlsl`
- **Performance**: ~0 cost difference (same instruction count, different math)
- **Visual**: Every pixel changes subtly — diffuse more realistic, specular at high roughness brighter
- **Breaking**: Visual output changes everywhere (intentional — closer to UE5 reference)

## Acceptance Criteria

1. Burley diffuse: rough sphere at grazing angle shows brighter rim than Lambertian
2. Smith Joint: specular highlight shape matches UE5 at various roughness + angles
3. Multiscatter: roughness=1.0 metal sphere is visibly brighter than before (energy conserved)
4. Side-by-side demo: old BRDF vs new BRDF on material ball with roughness gradient
5. PBR test demo produces output matching UE5 default lit material at equivalent params
