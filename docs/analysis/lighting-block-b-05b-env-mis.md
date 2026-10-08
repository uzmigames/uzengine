# Lighting block B, item 5b — the environment-MIS specular that was black

Rulebook task `phase0_lighting-block-b-05b-env-mis` (proposal, design, spec `env-mis`). Branch
`feat/sponza-gi-parity`, 2026-10-08. Block B.5's side finding ("the environment-MIS specular is black
on smooth metals in every scene without an HDRI"), taken after the block closed.

## Before

- The pass (`EnvMIS_Temporal`) estimates the specular environment term of smooth metals and the
  lighting replaces the split-sum specular with it wherever the SDF reflections are off (block A.3).
  Reading it for this item, and measuring it against references, found six defects, each enough on
  its own:
  1. the compute dispatch never bound the per-frame constants at b1 (root constants and the UAV table
     only), so the shader's camera position and sky light tint were whatever the previous compute
     pass left in that root slot;
  2. the estimator drew environment samples only but weighted them with the balance heuristic
     `p_env / (p_env + p_brdf)` as if a BRDF strategy existed, so it converged to the environment half
     of the sum: on a mirror the environment strategy hits the lobe with probability ~1e-6 per sample,
     and the estimate is zero; on rough metals it is a fraction;
  3. the pdf lacked the solid angle's sin(theta) (the pyramid stores luminance × sin(theta) and the
     texel's solid angle carries the same factor), so the estimate was too bright by 1 / sin(theta)
     towards the zenith and the nadir;
  4. the pass wrote alpha 1 on every lit pixel and the lighting replaced the split-sum specular
     wherever alpha > 0, so every dielectric and rough metal lost its environment specular in every
     scene with `r.GI.Reflections 0`;
  5. the sampler's directions were in the cube's frame: the engine's cubemaps follow the DX convention
     with +Y up (every consumer converts with `WorldToCubemap`, y and z swapped), the sampler's
     equirect treated the cube as Z-up and the pass used its directions as world directions (found by
     dumping the environment: its luminance varied with the azimuth and was symmetric about the
     horizon; the mirrored view's sky matched the dump only after the swap);
  6. the alias table's accept test took the fractional part of `u.x × texelCount` with a 16-bit `u.x`
     and 131072 entries, so the fraction was always 0: every sample accepted its own even texel and the
     alias was never taken; the environment was sampled uniformly while the estimator divided by the
     luminance pdf (+11 % at roughness 0.3 and +25 % at 0.5 against the CPU integral, with the mirror
     exact; a numpy replica of that sampling reproduces 1.12 / 1.27).
- Measured on build 5470d37f (`test-env-mis/before`, `scripts/test_env_mis.py --before`, 1080p):
  the chrome floor's sky class against F × the mirrored view reads a median relative error of
  1.000 (ratio 0.000: black) over 1097887 px; the dielectric boxes' specular
  luminance is 0.0000 (replaced by zero); the chrome sphere's specular is
  0.0000; the pass costs 0.206 ms.

## What changed

- `env_mis_temporal_cs.hlsl`: two strategies combined by the balance heuristic — 4 environment
  samples through the alias table and 4 BRDF samples from the visible GGX normals (`SampleGGX_VNDF`
  in the tangent frame of `BuildOrthonormalBasis`), both weighted with the visible-normal density
  `G1(V) · D(H) / (4 N.V)` and the pyramid's pdf; the sky light tint applied; the EMA (alpha 0.05,
  ping-pong, no reprojection) as before; the output alpha is the coverage (1 on metals below the
  roughness threshold, fading to 0 across metallic 0.3-0.5 and roughness 0.55-0.7).
- `envmap_sampling.hlsli`: `EnvTexelPdf` divides by sin(theta) (sample and evaluation paths);
  `EnvWorldToCubemap` / `EnvCubemapToWorld` convert at the interface, so `SampleEnvironment` returns
  world directions and `EvaluateEnvironmentPDF` takes them; the pass samples the cube through the
  conversion; `SampleAliasTable` accepts with the second uniform and the pass's randoms carry 32-bit
  floats.
- `env_mis_pass.{h,cpp}` / `frame_lighting.cpp`: the frame's PerFrameCB allocation is bound at
  root slot 1 after `SetPipeline` (the DX12 list routes it to the compute root arguments).
- `deferred_lighting_ps.hlsl`: `specular = lerp(splitSum, lerp(mis, ssr · F_ss, ssr.a) ·
  specOcclusion, coverage)`; the dump's `specular_indirect` records the blend. `r.EnvMIS` (default 1,
  one registration in `frame_constants.cpp`, `m_envMisEnabled`) switches the pass and its read off.
- Diagnostics: with a region dump active the sampler writes the environment it builds from as an
  RGBA32F equirect in cubemap space (`env_equirect.f32` + `.json`, through a colour output of the
  pyramid's mip-0 shader); the harness integrates the GGX BRDF over it on the CPU.
- `scripts/reflection_reference.py`: the roughness 0.3 and 0.5 metal floors
  (`reflection-reference-rough30` / `-rough50`) and the chrome floor under the ballroom PMREM
  (`reflection-reference-hdri`); `scripts/test_env_mis.py` (E1-E7).

## Gates (`scripts/test_env_mis.py`)

| Gate | Criterion | Result |
|---|---|---|
| E1 | the chrome floor reflects the sky: floor pixels whose reflected ray sees the sky match F x the mirrored view (median relative error <= 0.15) | FAIL: median error 0.714 (ratio 1.714) over 1097887 px |
| E2 | energy on the roughness 0.3 metal floor: specular_indirect / CPU integral of the dumped environment, median ratio in [0.85, 1.15] | PASS: median ratio 0.991 over 1943 px (p10 0.951, p90 1.032) |
| E2 | energy on the roughness 0.5 metal floor: specular_indirect / CPU integral of the dumped environment, median ratio in [0.85, 1.15] | PASS: median ratio 0.992 over 1943 px (p10 0.942, p90 1.042) |
| E2m | the chrome floor against F x the dumped environment along the reflected ray (median relative error <= 0.15) | PASS: median error 0.008 (ratio 0.992) over 68465 px |
| E3 | coverage: the dielectric boxes keep the split-sum specular (max |diff| to r.EnvMIS 0 <= 1e-4 over the interior) and it is not zero | PASS: interior max |diff| 0.00e+00 over 38418 px; median specular luminance 0.0325; silhouettes: 137 of 42189 px differ (max 6.40e-01; the previous frame's jittered estimate) |
| E4 | a PMREM environment: the chrome floor under the ballroom PMREM, specular_indirect against F x the dumped environment along the reflected ray (median relative error <= 0.2) | PASS: median error 0.029 (ratio 0.985) over 71270 px |
| E5 | the EnvMIS_Temporal pass <= 1.5 ms at 1920x1080 on the reference scene | PASS: 0.218727 ms |
| E7 | temporal stability: mean per-frame |delta luminance| over the roughness 0.5 floor <= 2 % of its median luminance | PASS: 0.00066 against 0.01069 (median luminance 0.5345) |
| E6 | Sponza unchanged: positions and direct_light identical to the capture of the build before the item; colour within the loading-state envelope | PASS: positions 0, direct 0, colour changed 36 px (median rel 0.000); repeat pair 30 px / 0.000; envelope 90656 px / 0.034 |

**Numbers (after, 1080p).** Chrome floor, sky class (1097887 px): median relative error 0.714, ratio 1.714 (p10 1.705, p90 1.726; with the capture's √3 factored out the error is 0.011); the split-sum path (`r.EnvMIS 0`) reads error 0.923, ratio 1.923. Roughness 0.3 floor (1943 px): estimate / ground truth median 0.991 (p10 0.951, p90 1.032), split-sum / ground truth 3.255; roughness 0.5 floor (1943 px): 0.992 (p10 0.942, p90 1.042), split-sum 4.850. Chrome floor against F × the environment along the reflected ray: median error 0.008, ratio 0.992 over 68465 px. Dielectric boxes (42189 px, interior 38418 px): specular median luminance 0.0325 with the estimate on, 0.0325 with it off, interior max |diff| 0.00e+00; 137 silhouette pixels differ (the previous frame's jittered estimate; max 6.40e-01) (before: 0.0000). Chrome floor under the ballroom PMREM (71270 px): median error 0.029, ratio 0.985 (split-sum ratio 1.082). The chrome sphere of `chrome_test` (366005 px) renders inside-out (positions x median -51.577 cm with the camera at x = 400, N·V median -0.698): specular median luminance 0.0028 (before 0.0000), not a measurement of the estimate. Cost: `EnvMIS_Temporal` 0.219 ms on the reference scene (before 0.206 ms), 0.299 ms on the roughness 0.5 floor, 0.129 ms on the chrome sphere; deferred lighting 0.594 ms. Temporal: roughness 0.5 floor mean |Δ| 0.00066 (median luminance 0.5345), chrome floor 0.00009. Sponza against the capture of the build before the item (frame 120): positions 0, direct 0, colour changed 36 px (median relative 0.000; repeat pair 30 px / 0.000; envelope 90656 px / 0.034). Harness 8/9.

## Found on the way

- **The sky cubemap capture is √3 brighter than the sky the frame renders (white sun).** The capture
  pass scales the sun by `skySunColor.Length()` (the length of the illuminance vector: 17.32 for the
  reference's (10, 10, 10)) while the sky pass takes the per-channel illuminance (10) and the aerial
  perspective its x component. Measured with the dumped environment against the mirrored view's sky
  along the same directions: the ratio screen / environment is 0.584 at every elevation (the shape
  agrees; p10-p90 of the reflection's ratio 1.705-1.726). Every image-based term samples this
  environment (the prefiltered specular, the sky SH, this estimate), so E1 reads the factor. Fixing
  the capture changes the ambient of every atmosphere scene, Sponza's calibrated look included:
  recorded in known-issues, left to its own decision. **Taken by the user and fixed in B.5c**
  (`lighting-block-b-05c-sky-capture.md`): E1 reads the fix as S2 there.
- **The sphere of `sample/content/meshes/sphere.uzmesh` renders inside-out.** In `chrome_test` the
  dump's visible positions are the far hemisphere's (x median -51.577 cm with the camera at x = 400) and the
  outward normals face away from the camera (N·V -0.698): back-face culling keeps the far side,
  so the "black disc" of block B.5's finding had this second cause, independent of the estimate. E4
  is measured on the chrome floor under the same PMREM; the mesh (winding or import) is its own item.
- The split-sum path against the same references: ratio 3.255 / 4.850 of the integral on the
  roughness 0.3 / 0.5 floors and 1.082 of F × the PMREM along the reflected ray on the chrome
  floor: the prefiltered path is far from the integral on rough metals (reported, not gated; its own
  item). **Done 2026-10-08** (`lighting-split-sum-mips.md`): the mip curve of a PMREM applied to the
  engine's linear bake, and the sky SH evaluated in the wrong frame.

## Still open

- The lighting reads the previous frame's estimate (the pass and the lighting are not ordered by a
  declared resource; the ping-pong keeps them apart): one frame of latency, and under the TAA jitter
  a silhouette pixel can carry the neighbour's coverage (137 of 42189 box pixels). Importing the
  history into the graph and reading the current frame's texture is the follow-up.
- The history is not reprojected: under camera motion the EMA smears the estimate (alpha 0.05, ~20
  frames); a reprojected history with a velocity-based rejection is the follow-up.
- `EnvMISPass::OnResize` is never called: the history textures keep their creation size.
- A sun disc captured in the sky cubemap is counted by both the analytic sun and the environment term
  (shared with the split-sum path); the harness excludes the glint.
- The sky capture's √3 (above). The split-sum path's own level against the same references is
  reported in the numbers, not gated.
- Coverage is a fixed band (metallic 0.3-0.5, roughness 0.55-0.7); rough metals above it keep the
  split-sum approximation.
