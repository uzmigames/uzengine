# Design — rough-specular reflection temporal reconstruction

## Mechanism (file+symbol, from the diagnosis)
- `runtime/shaders/runtime/reflections/rt_reflection_cs.hlsl` ~246-263: one GGX-VNDF direction per rough texel,
  VNDF-throughput weighted. `ReflectionSample2D` (line 148) <- `ReflectionIGN` pattern advancing by `frame % 64`.
- `runtime/shaders/runtime/postprocess/ssr_temporal_cs.hlsl`: YCoCg neighborhood variance clamp + finite blend.
- `runtime/shaders/runtime/postprocess/ssr_spatial_denoise_cs.hlsl`: `normalCosine^512` edge-stop — on curved
  rough normals the eligible spatial support narrows, leaving a changing specular estimate for TAA.
- Eligibility: `rt_reflection_contract.hlsli` (roughness<=maxRoughness; mirror below ~0.04). DarkMetal
  roughness 0.4 -> rough RT route (cutoff raised by r.RayTracing.Reflections.Rough=1).

## Approach
1. Export rough-reflection guides (roughness, hit-distance, normal) and feed the specular denoise with
   GGX-appropriate reprojection + a hit-distance/roughness-scaled spatial radius (tighter at low roughness to
   protect the mirror, wider at high roughness for enough samples) instead of a flat normal^512 stop.
2. Increase EFFECTIVE sample count via temporal/spatial reuse of the one ray (reservoir/accumulation) rather
   than relying on a single ray + weak reconstruction; keep energy (VNDF throughput) correct.
3. Variance-driven temporal clamp before scene-color TAA: clamp from the estimator variance, not only the
   noisy instantaneous neighborhood, so the rough specular is RECONSTRUCTED pre-TAA, not fed as noise.
4. Evaluate the `frame % 64` IGN advance for a better-distributed sequence.

## Gating + safety
New CVar (e.g. `r.RayTracing.Reflections.RoughReconstruct`, default OFF) so OFF reproduces the current SSR/RT
render byte-identical. MUST NOT regress the mirror path (roughness<0.04) nor change mean specular energy.

## Risks
- Over-blurring glossy detail or darkening specular -> validate mean specular energy (within a few %) + detail.
- Motion/disocclusion ghosting from longer reuse -> keep the variance/confidence gate + validate on motion.

## Validation harness
Reuse `_evidence/gi/sphere-crawl/` capture/analyze + the glossy-vs-mirror + no_stochastic_specular controls.
Fixed camera, frames 90-149 AND 600-659, DarkMetal body + specular-indirect ROIs, fixed exposure.
