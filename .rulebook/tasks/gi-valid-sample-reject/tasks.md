# Diffuse gather sample validity (2026-10-22)

## DXR final gather (2026-10-26, default OFF)

- [x] Add r.GI.FinalGather.DXR: 0 retains SDF, 1 traces gather and sun
  visibility against the existing TLAS, 2 isolates sun visibility.
- [x] Reuse exact triangle material and emission plus existing radiance-cache
  irradiance. Apply the same route to unsupported upsample receivers.
- [x] Keep DXR in lazy shader permutations; missing DXR, empty TLAS, missing
  material table, and ReSTIR use the existing SDF path. Reset history on
  visibility mode changes.
- [x] Test the CPU capability and resource-validity gate.
- [ ] Record fresh-baseline OFF identity, ON/PT comparisons, second scene,
  GPU timing, and build/layout/gtest evidence before enabling by default.

- [x] Preserve explicit origin availability in trace alpha, independent of radiance.
- [x] Exclude unavailable taps from spatial, temporal and full-resolution reconstruction.
- [x] Retry failed two-pixel normals with receiver-plane-checked one-pixel triangles.
- [x] Gate with r.GI.ValidSampleReject; OFF preserves the original arithmetic.
- [x] Share the tap-weight function with a black-invalid versus valid-unlit gtest.
- [x] Default ON (2026-10-26), after the corrected path-tracer oracle made the GI lift measurable.

## Default ON (2026-10-26)

Sponza frame 300 vs path tracer frame 511, fixed exposure, mean ROI
luminance: vault +13%, arcade +21%, arch-ceiling indirect +16%, all still
below the path tracer; left wall and floor unchanged. GPU cost: SSGI
+0.8 ms. With r.GI.ValidSampleReject 0, reflection-reference frame 95 is
byte-identical to a fresh 9fb59b63 build. Evidence:
E:/UzmiEngine/_evidence/gi/valid-sample-default-on.

The existing sdfSampleRotation DWORD carries rotation in its low 31 bits and
the validity flag in bit 31; constant-buffer size and offsets do not change.
Default OFF: the radiance acceptance gate did not pass. Fresh pinned-0.15.2
49de1431 and the fixed OFF binary both produce zero Sponza indirect radiance,
unlike the retained diagnosis executable. Their environment dumps, applied
CVars and projection matrices agree; this transport discrepancy is outside
the receiver-validity fix. Do not enable by default without resolving it.

Measured at frames 296-299, explicit invalid-origin fractions:
left wall 14.4762% -> 3.8095%, right wall 21.5857% -> 6.7429%,
back arch 27.2% -> 4.5333%, floor 0% -> 0%.
All four ON/OFF indirect means are 0, and all raw trace samples are black.
PT8-minus-PT1 means remain 0.0014386925, 0.0042512242, 0.0188856456,
and 0.0091332854 respectively. No toward-PT improvement is claimed.

The OFF reflection-reference frame-95 image matches the fresh baseline
exactly (maximum 8-bit code delta 0). Pinned builds verify 163 shaders.
The shared-weight gtest passes; the filtered suite has 7997 passes and only
the three existing VfxEditorSlate WIP failures. Fresh baseline has 7975
passes and no failures (the main checkout also discovers extra WIP tests).
npm run test:layouts exits 0. The final default-OFF no-region-dump smoke
run.log has no errors; measurement captures retain the pre-existing
TAA-missing flag 0x10 because TAA is deliberately disabled.
Evidence: E:/UzmiEngine/gi-valid-sample-20261022/measurements.json and
off-on-pt-montage.png. The montage cannot establish speckle filling while
the fresh baseline's entire GI contribution is black.

The final default-OFF frame-95 smoke also has max code delta 0 versus
the fresh baseline. The baseline worktree was removed with git worktree
remove; Git's residual junctions were unlinked without recursive deletion.
No engine, test_runtime or zig processes remain. Nothing was committed.

## Path-tracer Frostbite diffuse oracle (2026-10-27, default OFF)

- [x] Add r.PathTracer.FrostbiteDiffuse (read at path-tracer init): every PT
  shader is compiled with UZ_PT_FROSTBITE_DIFFUSE, and EvaluatePBR then uses
  the raster EvalFrostbiteDiffuse(albedo * (1 - metallic), ...) lobe instead
  of the energy-compensated Lambert. The layered coat/stack, ReSTIR DI/GI and
  NEE all call EvaluatePBR; every sampler divides it by its actual mixture
  PDF, so the estimator stays unbiased with the cosine/GGX sampling unchanged.
- [x] OFF is byte-identical to a fresh ad2659b0 build: Sponza --path-trace,
  surface-cache-locallight and Cornell (cb-pt4) frames 255 and 511 have max
  code delta 0, and the Sponza linear taa_raw dump is identical. A runtime
  root-constant flag was tried first and moved 407 Sponza values (DXC codegen),
  hence the init-time define, as for r.PathTracer.GIReSTIR.
- [x] ON, direct only (PT MaxBounces 1 with sky light 0 versus raster
  direct_light at shadow visibility 1, linear HDR, Sponza 960x540): raster/PT
  floor 1.080 -> 1.004, walls 0.678 -> 0.799 (mode 0.75 -> 0.95, least
  decal-darkened quarter 0.87 -> 1.02), all sunlit 0.707 -> 0.818. The wall
  residual is the dirt decals, which the PT lacks.
- [x] ON MaxBounces 4: frames 255 and 511 agree to 1e-6 in the mean, no
  non-finite values; 16-px block residual is lower than OFF.
- NRD's demodulation guide (nrd_signal.hlsli) still assumes Lambert; with
  r.NRD on, the ON diffuse/specular split fed to the denoiser is approximate.

Evidence: E:/UzmiEngine/_evidence/path-tracer/frostbite-diffuse-oracle.
