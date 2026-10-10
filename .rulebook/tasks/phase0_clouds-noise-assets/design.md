# Design: the cloud noise lookup

## Before
`LoadNoiseTextures` (cloud_pass.cpp): `vfs->Read("atmosphere/shape.bin")` (the project's content root), then
`std::ifstream("atmosphere/shape.bin")` (the working directory). On failure it returned before the detail block, and
`Execute` called it again next frame because `m_hasNoiseTextures` stayed false.

## After
- `ReadCloudAsset(vfs, path, source)`: `vfs->Exists(path)` then `Read`; else `ResolveEngineResource("content/" + path)`
  (the exe's directory) and `current_path() / path`, each checked with `is_regular_file` before it is opened, so a
  missing file logs nothing until the one summary warning.
- `m_noiseLoadAttempted` set on the first call, cleared in `Shutdown`: one attempt per pass lifetime.
- The success lines name the source the file came from.
- build.zig: `addInstallDirectory(content/atmosphere -> bin/content/atmosphere)`, next to the DLL deployment.

## Harness
`scripts/test_cloud_noise_assets.py`: `sample/projects/cloud_test.uzproject`, 180 frames at 1280x720, screenshot at
frame 170; `--before` captured on the binary before the change.
