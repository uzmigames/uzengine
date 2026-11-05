# Rough-specular reflection temporal reconstruction — kill the glossy crawl

## Why
With all GI/Indirect enabled, rough metal (material_showcase Sphere_DarkMetal, metallic=1 roughness=0.4) shows
reflected lighting that "crawls" over time. Diffuse is exactly zero on it (metallic=1), so the crawl is the
stochastic ROUGH-SPECULAR reflection and its reconstruction, NOT the diffuse gather. Diagnosis (2026-10-29,
`_evidence/gi/sphere-crawl/`, [[reference/rendering/sphere-crawl-material-showcase-2026-10-29]]):
- `rt_reflection_cs.hlsl` draws ONE GGX-VNDF ray per rough texel; `ReflectionSample2D`/`ReflectionIGN` advance
  the sample pattern via `frame % 64`, sampling the non-uniform HDRI (and cached surfaces on hits).
- The SSR temporal + `ssr_spatial_denoise_cs.hlsl` (a very strong `normalCosine^512` spatial edge-stop) cannot
  reconstruct that one changing ray on curved rough normals -> a changing specular estimate feeds TAA as noise.
- PROOF: removing stochastic specular (keeping diffuse GI) drops DarkMetal body std 89.7%; a matched
  glossy(1)-vs-mirror(0) control drops it 70% at equal mean energy (only the lobe sampling changes). TAA
  REDUCES the variation (off makes it worse); the mirror path (roughness<0.04) is deterministic and stable.

## What changes
Reconstruct the stochastic rough-specular signal BEFORE scene-color TAA with GGX-appropriate guides
(roughness, hit distance, normal), enough effective samples, and a variance-driven temporal clamp — instead of
one ray with a curvature-starved spatial filter. Evaluate the 64-frame IGN advance and the normal^512 support
for decorrelation/effective-sample-count. Gate OFF byte-identical; do NOT regress the mirror path. Do NOT ship
by mirror-replacing rough rays, freezing the signal, or blurring the final image.
