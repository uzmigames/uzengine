# Unified real-time denoiser: stage 1

Default-off `r.Denoiser.Unified` requires standalone RT reflections and the existing
`r.RayTracing.Reflections.Denoise` switch. Mixed SSR/RT, diffuse, ReSTIR and shadows
keep their existing paths. No default is promoted.

- [x] Share receiver decoding, pixel motion conversion, per-tap geometry/roughness
  validation, temporal statistics/clamp and B3 spatial reconstruction.
- [x] Own pre-spatial signal/moment histories separately from producer alpha.
- [x] Import histories and declare trace, temporal, spatial and lighting accesses.
- [x] Keep root constants at 17 DWORDs; policy/jitter use a 32-byte b2 buffer.
- [x] Add shared C++/HLSL math tests and layout contracts.
- [x] Pinned Zig ReleaseFast build: exit 0, 165 shaders.
- [x] Layout inventory: exit 0, 185 declarations, no unmapped declarations.
- [x] Fresh e340b552 OFF identity: frame 95 max code delta 0 in
  `reflection-reference` and standalone glossy RT with legacy denoise enabled.
- [x] Measure static/motion/energy/performance evidence; acceptance fails below.
- [x] Candidate regression suite: 8,157 tests, 8,137 passed, 17 skipped,
  exactly the three pre-existing VfxEditorSlate failures; all five new tests pass.
  Fresh baseline test comparison is blocked by committed tests requiring the
  forbidden untracked `pathtracer_furnace_test.h`; that file was not read/copied.
- [x] Remove detached baseline and stop engine/test/Zig processes.

Memory: full-float RGB/count, moments and receiver geometry avoid half-float
radiance/squared-moment overflow. Two sets use 112 bytes/pixel; RG transient
reconstruction and clamp/rejected-tap diagnostics use another 24 bytes/pixel.
At 1920x1080 this is 221.48 MiB persistent and 47.46 MiB transient; at 2560x1440
it is 393.75 MiB persistent and 84.38 MiB transient. No allocation or shader
creation occurs while the switch or client route is off.

The fresh detached baseline used unchanged e340b552 renderer sources. Native
dependencies were mirrored into it. A temporary engine-only build target avoided
the unrelated missing test header; no test result is claimed for that baseline.
The temporary target was reverted and the worktree removed with `git worktree remove`.

The checked-in fixture/harness provides a deterministic 50 cm camera translation
at frame 64 and fixed-pose PT references. Its PNG metrics are explicitly display
metrics, not linear-HDR acceptance. The harness also captures stamped pre-TAA
scene HDR on a purely metallic receiver ROI with TAA enabled and jitter disabled.
PT does not run deferred lighting: its TAA stamp is validated independently, with
exactly the expected absent lighting stamp and no other missing/mismatched stamp.
This is composed receiver HDR, not an isolated RT-radiance export.

Existing run manifests export GPU means, not GPU median/p95; wall-frame p95 must
not be relabeled as GPU p95. Measured denoise means (legacy / unified):
640x360: 0.01554 / 0.06164 ms; 1920x1080: 0.10233 / 0.65246 ms;
2560x1440: 0.25146 / 1.14882 ms. The performance gate fails clearly.
Converged pre-TAA receiver-HDR ROI `(35,85)-(205,270)`, 640x360, glossy
standalone one-ray route, four PT bounces/four NEE samples, no NRD:

| Measure | Legacy | Unified | PT |
| --- | ---: | ---: | ---: |
| Static HDR RMSE | 0.0229124 | 0.0197823 | reference |
| First 8 post-translation HDR MSE | 0.00145584 | 0.000404058 | reference |
| Static mean luminance | 0.169128 | 0.175992 | 0.178831 |
| Static luminance p99 | 0.282537 | 0.287209 | 0.320649 |
| Static mean per-pixel temporal std | 0.00122926 | 0.00308751 | not measured |

Motion MSE falls 72.25%. Static mean bias improves from -5.43% to -1.59%;
motion mean bias improves from -9.65% to -3.94%. However, static variance
increases, and unified static p99 is only 89.57% of PT (the gate is 90%).
These failures and the GPU cost mean **this is not a clean stage-1 win**.

PT reference frames 2047/4095 change mean by 0.00692% static and 0.01843%
translated. Doubling RMSE is 0.00367844/0.00368008, below one fifth of the
corresponding candidate error. Frames are recorded, not relabeled as an exact
sample count: PT also accumulates during asset preload.

Evidence: `E:/UzmiEngine/unified-denoiser-stage1-proof/metrics.json`,
`motion-montage.png`, case manifests/logs, `build.log`, and `gtests.log`.
Normal ON/OFF captures have no shader/device/validation failures. PT's `taa`
preset logs the expected missing deferred-lighting record; the independently
current TAA stamp is checked by the harness, but those logs are not claimed clean.

The complete spec matrix is not certified: moving reflected objects, explicit
receiver-depth/roughness discontinuities, no-DXR identity, the full multi-frame
OFF matrix, GPU median/p95 and edge-spread gates remain unverified. The existing
camera-translation fixture already fails static-variance/highlight/performance
gates, so do not promote the feature or call stage 1 acceptance complete.
