# Sponza lighting: handoff for Windows testing

> **Origin**: `test/windows-sponza-parity @ 25cd0c5f` (`E:\SponzaTest`), ported verbatim on 2026-09-08 by `phase0_sponza-parity-06-qa-harness-validation`.
> Paths such as `scripts/*_artifacts/`, `scripts/sponza-*.png|log|json|sha256.txt` refer to evidence that is **not in Git**; the executable identities quoted are SponzaTest builds, not this repository's.


Branch: `test/windows-sponza-parity`. Prepared 2026-09-07. Record `git rev-parse HEAD`, GPU, driver, Windows version and shader model with each test result.

This branch improves SDF diffuse lighting, local reflections, material energy, shadow bias, loading/exposure and camera navigation. It is ready for further testing, with known failures listed below. It does not establish Lumen parity.

## Prepare and run

Use Windows, Zig 0.16.0 on PATH, the Windows SDK/MSVC Build Tools, and the repository's dependencies. From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/setup.ps1
Push-Location runtime
zig build
Pop-Location
```

Setup downloads missing SDK binaries, including Agility 1.619.2 and DXC. It can also install/update other project dependencies; review its output. FFmpeg uses an unpinned `latest` download, so record the installed version. The original test host had locally refreshed FFmpeg/Slang packages. Those downloaded vendor changes are not included in this lighting handoff; a fresh dependency installation has not been validated end to end.

The latest local build produced the engine executable and library, but the aggregate build failed with **23 existing `test_runtime` compilation errors** involving GoogleTest/toolchain character types, incomplete shader compiler types and Metal test types on Windows. A nonzero aggregate build is not a passing test run. Check the produced executable's timestamp; do not mistake an older binary for the build result.

Run one preset at a time:

```powershell
.\scripts\rendering\run_sponza_max_quality.bat
.\scripts\rendering\run_sponza_dxr.bat
```

- **Maximum SDF quality:** 1920x1080, diffuse gather 32 samples, reflections 16 samples/256 steps, GDF256, mesh SDF128. Hardware ray tracing is disabled. The sample project's default gather has 8 samples.
- **DXR experiment:** requires **DXR Tier 1.1 / inline ray tracing**. Enables `r.RayTracing=1` and `r.RayTracing.GI=1`, disables SDF final gather to allow DDGI to supply diffuse GI. Reflections still use SDF/SSR, and AO uses GTAO. The BAT does not perform a capability preflight.

The GTX 1660 SUPER used locally reports DXR Tier 1.0 only. Its DXR run created BLAS but failed to create `DDGI_ProbeUpdate` with `0x80070057`. That fallback image is **not DXR quality evidence**. Validate DDGI pipeline creation and actual GI contribution on a Tier 1.1 GPU. The existing RT shadow/reflection/AO flags alone do not prove those passes are connected. DDGI also retains simplified sky/sun/material inputs and needs separate evaluation.

An optional read-only capability probe reports adapter 0 (which may differ from the engine-selected adapter on multi-GPU systems):

```powershell
zig c++ -std=c++23 scripts/rendering/sponza_dxr_capabilities.cpp -ld3d12 -ldxgi -ldxguid -o scripts/sponza_dxr_capabilities.exe
.\scripts\sponza_dxr_capabilities.exe
```

Wait for geometry, textures and distance fields to finish loading. F5 toggles the free camera; WASD moves, right mouse button plus mouse looks, Space/Ctrl moves vertically, Shift accelerates. F5 returns to the scene camera.

## Manual acceptance scenarios

1. Leave Camera A stationary after loading. Compare the first visible frame and the converged image: shaded columns and arches should stay readable, without the previous large white dirt patch or broad bright lines at wall/floor contacts.
2. Move close to the left column, doorway and shaded arcade. Check contact shadows, noise, light leaking and changes with viewing angle. Stop moving and observe convergence.
3. Test Camera C using `scripts/rendering/sponza-lighting-camera-c.uzproject` with the same quality arguments. For a repeatable moving camera, use `scripts/rendering/sponza-temporal-camera.uzproject` and capture at frame 250. Keep `r.Exposure=0.5`, `r.ColorContrast=0.65`, `r.ColorSaturation=1` for comparison with the recorded Camera C/motion runs.
4. On a Tier 1.1 GPU, compare the two presets at the same camera and exposure. Capture logs with `--log-level info --pass-dump --diag`; reject DXR GI evidence if `DDGI_ProbeUpdate` fails even when the process exits 0.

`verify_sponza_loading.ps1` reproduces the older loading comparison (project defaults, not the FG32 maximum preset). After it completes, `compare_sponza_stationary.ps1 -Prefix sponza-loading-fixed` compares matching Camera A captures. Neither script supplies a Falcor parity score.

Latest local native SDF runs: Camera A and C at 1920x1080, plus a scripted 160 cm camera movement and return at 960x540, all with FG32, exit 0 and empty stderr. Images were inspected. They showed removal of the broad bright contact lines; transient noise remained after the camera jump. Each run collected 31 diagnostic targets. Hi-Z/SSR zero-output warnings, skipped position/shadow-atlas readbacks and unavailable DX12 debug layer limit that evidence. Maximum quality ran at approximately **1 FPS** on the 6 GB GTX 1660 SUPER.

## Reproduce focused checks

Run PowerShell from the repository root, with Zig on PATH and DXC installed. Each script builds its own harness unless a prerequisite is shown. Run GPU checks sequentially.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/rendering/test_material_specular.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/rendering/test_filtered_gather.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/rendering/test_gather_activation.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/rendering/test_gdf_raw_composite.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/rendering/test_gdf_raw_lifecycle.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/rendering/test_surface_geometric_normal.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/rendering/test_rc_trace_exhaustion.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/rendering/repair_sponza_decal_normals.ps1 -Mode Audit

# Build shared GPU fixtures, then run the maximum-resolution controls.
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/rendering/test_sdf_window.ps1 -CompileOnly
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/rendering/test_sdf_refined_controls.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/rendering/test_sdf_diffuse_gpu.ps1
```

Recorded focused results include material energy 686/686, filter/reprojection 8714/8714, activation 192/192, raw compositor 210/210 (including 200 bitwise comparisons with the preserved expanded-field reference), resource lifecycle 34/34, geometric normals 11/11 and RC exhaustion 12/12. The five final reflection and diffuse GPU controls had maximum channel error 0.000195. These harnesses complement native scene testing; they do not certify the entire renderer.

Additional runners cover quaternion order, input, mesh BVH, exposure reset, loading readiness, BRDF, SSR and shadow bias. `test_radiance_cache_sh.ps1` requires `-Zig (Get-Command zig).Source`. `test_sdf_gpu.ps1` runs the broader synthetic GPU cases; `test_sdf_window.ps1 -BvhCompare` compares the native mesh fields. The default window resolution sweep intentionally includes insufficient resolutions and can fail those cases.

## Known unresolved failures and test priorities

These assertions remain enabled. Reproduce after building shared GPU fixtures:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/rendering/test_sdf_surface_origin.ps1 -Label axis
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/rendering/test_sdf_dynamic_trace.ps1
```

- The surface receiver fixture leaks one of 32 rays for some seed/orientation combinations (maximum channel error 0.009328). The ideal grazing-plane fixture passes 14/15; the ray with normal cosine 0.01 exhausts 256 steps.
- Scalar, filtered distance fields can lose nearby-wall visibility. A rejected expansion variant reduced leakage but incorrectly removed valid light; it is not the production implementation.
- The repaired decal has correct LOD0/1 orientation; some LOD2/3 triangles remain inconsistent. LOD bias 0.1 favors detail but does not force LOD0.
- No per-texel Surface Cache, complete emissive transport or complete Lumen screen/mesh tracing system is implemented. Falcor is unavailable on the test host, so PSNR >=35 dB and visual parity are unverified.
- Hardware DXR 1.1 GI and cross-platform native execution require testing on appropriate machines.

## Git contents and local evidence

Commit source code, shaders, reusable runners/fixtures, sample scenes, the repaired tracked mesh, launchers and reports. `scripts/rendering/fixtures/gdf_expanded_before_raw.h` intentionally freezes the old compositor for the 200 preservation checks; it must not be regenerated from the current implementation.

Do not commit generated executables/libraries, compiled shaders, caches, capture PNGs, logs, exit/argument files, metrics/hashes, test artifact folders, the original mesh backup or the downloaded Lumen paper. `.gitignore` covers these Sponza artifacts without hiding the test sources. No local evidence files were deleted.

Local `.mcp.json` changes and downloaded FFmpeg/Slang vendor changes remain outside these commits. Existing tracked vendor files were not removed from Git. The Agility download directory is ignored and recreated by setup.

The dated rendering reports describe the investigation and references. Their `scripts/*_artifacts`, capture, hash and `.omo` paths refer to **local evidence excluded from Git**, not files expected in another clone. Regenerate evidence with the runners and attach relevant captures/logs to the testing team's report. No remote push or external message is part of this local commit handoff.
