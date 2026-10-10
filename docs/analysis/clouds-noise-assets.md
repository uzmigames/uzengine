# Clouds side item — the noise volumes reach uzengine.exe

Rulebook task `phase0_clouds-noise-assets` (proposal, design, spec `cloud-noise-assets`). Branch
`feat/sponza-gi-parity`, 2026-10-08. From the clouds rewrite task's map (`phase0-3.1.0-volumetric-clouds-rewrite`, 0.B4).

## Before

`CloudPass::LoadNoiseTextures` looked for `atmosphere/shape.bin` and `atmosphere/shape_detail.bin` in the project's
VFS (rooted at `sample/content` for every sample project) and in the working directory; the files are in
`runtime/content/atmosphere` and nothing deployed them next to the exe since the pbr_test demo went away
(1bc7c943). Measured on the binary before the change (`cloud_test.uzproject`, 180 frames, 1280×720):

| Gate | Before |
|---|---|
| N1 | FAIL: shape False, detail False |
| N2 | FAIL: 183 lines, e.g. [2026-10-08 15:21:10.566] [FileSystem] [warning] FileReadAll: failed to open 'E:\UzmiEngine\uzengine\sample\projects/../content/atmosphere/s |
| N3 | PASS: exit 0, {'CloudComposite': 0.00562836, 'CloudCompute': 1.15841} |

## What changed

- `runtime/build.zig`: `runtime/content/atmosphere` installed as `bin/content/atmosphere`.
- `cloud_pass.{h,cpp}`: `ReadCloudAsset` reads from the project VFS (a project may ship its own volume), then
  `ResolveEngineResource("content/...")`, then the working directory, checking existence first; one attempt per pass
  lifetime (`m_noiseLoadAttempted`); the log names the source.
- `runtime/content/atmosphere/LICENSE-three-geospatial.txt`: the MIT notice of the four data files copied in
  8332def6 (takram/three-geospatial, Copyright (c) 2024 Shota Matsuda); `docs/references.md` names them.

## Gates (`scripts/test_cloud_noise_assets.py`)

| Gate | Criterion | Result |
|---|---|---|
| N1 | both noise volumes loaded | PASS: shape True, detail True |
| N2 | the noise files are looked up once (at most two log lines about them) | PASS: 2 lines, e.g. [2026-10-08 15:22:59.148] [Renderer] [info] CloudPass: shape noise loaded (128^3 RGBA8, SRV=49505) from E:\UzmiEngine\uzengine\runtime\build |
| N3 | the run completes without device removal and gpu_ms reports the cloud compute pass | PASS: exit 0, {'CloudComposite': 0.0056255, 'CloudCompute': 1.29729} |
| N4 | the noise files are installed next to the exe, byte-identical | PASS: shape.bin: identical; shape_detail.bin: identical |

**N5 (reported).** 95.8 % of the frame's pixels changed, mean |Δ luminance|
0.0071 (p99 0.052): the overcast dome without structure
became shaped clouds (`test-cloud-noise/cloud_noise_montage.jpg`). Both screenshots are frame 5, not 170: the
harness passed `--screenshot-frames 170`, which uzengine.exe rejected until the same day's parser fix ("N"
now means "N-N"), so both captures fell back to the default frame alike. Cloud pass cost: CloudComposite 0.006 ms, CloudCompute 1.158 ms
→ CloudComposite 0.006 ms, CloudCompute 1.297 ms.

## Still open

- `shape.bin` (128³, 8 MB as RGBA8 plus an 8 MB staging buffer) is uploaded but not sampled by the ray march; the
  clouds task's map lists it with the 18 CloudCB fields the shader never reads (Phase 7).
- The clouds' lighting (multi-scattering, sky term, aerial perspective) is the clouds task's Phases 2-5.
