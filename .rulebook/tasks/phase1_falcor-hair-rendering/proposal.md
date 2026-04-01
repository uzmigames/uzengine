# Proposal: phase1_falcor-hair-rendering

## Why
UzEngine has zero hair rendering support. Falcor ships the Chiang 2016 multi-lobe hair BSDF and dedicated hair geometry processing. Hair is the defining visual element for character rendering — no hair rendering means no believable human or animal characters. The Chiang 2016 BSDF (R/TT/TRT lobes with modified Gaussian longitudinal scattering and azimuthal scattering with attenuation) is the industry standard used by every major film and game engine for realistic hair. Requires: shader variant system (for `materialType=3` Hair dispatch path).

## What Changes
- `HairStrandAsset` class: loads strand data (positions, widths, UVs per control point) from `.hair` or `.abc` format; uploads as GPU vertex buffer (line strip with adjacency)
- Vertex shader `hair_vs.hlsl`: expands line strips to camera-facing quads (billboard geometry) using strand tangent and camera forward; width interpolated root-to-tip from strand data
- Hair BSDF in `include/hair_bsdf.hlsli`: complete Chiang 2016 implementation — `Mp(thetaI, thetaR, roughness, p)` longitudinal scattering (modified Gaussian), `Np(phi, p, refractiveIndex, absorption)` azimuthal scattering, `Ap(f, p)` attenuation factors (Fresnel terms for each lobe); 3 lobes: R (surface reflection), TT (direct transmission), TRT (internal reflection). @ref Chiang et al. 2016 "A Practical and Controllable Hair and Fur Model for Production Path Tracing"
- `hair_ps.hlsl`: pixel shader using hair BSDF with tangent-space lighting (strand tangent replaces surface normal in BRDF), opacity from strand width/camera distance, per-strand color from root/tip color gradient
- Deep Opacity Maps (DOM) for hair self-shadowing: 8 depth layers at shadow map resolution, opaque transmittance fraction per layer
- Hair LOD: reduce strand count at distance via `HairStrandAsset::GetLODLevel(distance)`
- GBuffer integration: `materialType=3` dispatches the hair shading path in `deferred_lighting_ps.hlsl` (requires shader variant system)
- Affected files: new `hair_strand_asset.h/.cpp`, new `hair_vs.hlsl`, new `hair_ps.hlsl`, new `include/hair_bsdf.hlsli`, new `hair_shadow_pass.h/.cpp`, `deferred_lighting_ps.hlsl`

## Impact
- Affected specs: new `docs/specs/rendering/hair-rendering.md`
- Affected code: new hair asset/shader/pass files, `deferred_lighting_ps.hlsl`, GBuffer layout (spare channel for strand tangent)
- Breaking change: NO — hair is a new material type; existing meshes unaffected
- User benefit: Believable human and animal character hair; Chiang 2016 is the same BSDF used in Disney/Pixar production rendering; real-time performance via deep opacity maps for self-shadowing
