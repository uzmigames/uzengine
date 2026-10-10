# The cloud noise volumes never reach uzengine.exe

## Why

The clouds rewrite's map (2026-10-08) found that `CloudPass::LoadNoiseTextures` reads `atmosphere/shape.bin` and
`atmosphere/shape_detail.bin` through the project's VFS (rooted at the project's content root, `sample/content`
for every sample project) and then the working directory, while the files live in `runtime/content/atmosphere`.
Nothing has deployed them next to the exe since the pbr_test demo and its copy step were deleted (1bc7c943). So
every project rendered without the detail erosion (`cloud_raymarch_cs.hlsl`), and the load was retried every frame
with a FileSystem warning each time. The four three-geospatial data files were also copied (8332def6) without their
MIT notice. This is the clouds task's 0.B4 (marked as a decision, never verified in the exe) and the precondition of
its 0.12 check (no TDR on the current shader).

## What Changes

- `runtime/build.zig` installs `runtime/content/atmosphere` as `bin/content/atmosphere`.
- `CloudPass::LoadNoiseTextures` reads each file from the project VFS (a project may ship its own), then
  `ResolveEngineResource("content/atmosphere/...")`, then the working directory, checking existence first, and
  tries once per pass lifetime (`m_noiseLoadAttempted`).
- `runtime/content/atmosphere/LICENSE-three-geospatial.txt` (the MIT notice); `docs/references.md` names the data.
- `scripts/rendering/test_cloud_noise_assets.py` (N1-N5).

## Gates (pre-registered)

N1 the cloud_test run's log reports both noise volumes loaded (before: 'could not load atmosphere/shape.bin').
N2 the noise files are looked up once: at most two log lines about them over the run (before: one warning per
frame). N3 the run completes without a device removal and the manifest's gpu_ms reports the cloud compute pass
(before and after; the clouds task's 0.12). N4 the installed files are byte-identical to runtime/content/atmosphere.
N5 reported: the sky's change with the detail erosion on, the cloud pass cost, a montage.

## Impact
- Affected specs: cloud-noise-assets (new)
- Affected code: `runtime/build.zig`, `runtime/engine/renderer/passes/atmosphere/cloud_pass.{h,cpp}`,
  `runtime/content/atmosphere/LICENSE-three-geospatial.txt`, `docs/references.md`, `scripts/rendering/test_cloud_noise_assets.py`
- Breaking change: NO (every scene with volumetric clouds changes look: the detail erosion turns on)
- User benefit: the clouds render with the noise they were written for, and the log loses a warning per frame.
