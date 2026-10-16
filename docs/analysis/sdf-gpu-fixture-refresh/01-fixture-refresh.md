# SDF GPU fixture refresh

Gate: `phase0_sdf-gpu-fixture-refresh`, run on 2026-10-13.

## Binding changes

`scripts/rendering/test_sdf_gpu.cpp` and `test_sdf_window.cpp` now allocate
13 root DWORDs, send 9 for mesh bakes and 13 for GDF composites, and assert
the corresponding CPU layouts are 36 and 52 bytes. Bakes explicitly send
`sliceOffset=0`. Composites send `regionOrigin=0`, packed whole-window
`regionSize`, and `band=0` (untruncated).

The CLAMP sampler at s2 remains for mesh/material sampling; a linear WRAP
sampler at s3 serves the toroidal GDF readers. `windowOrigin` follows
`GlobalDistanceField::Composite`: each component is the first world voxel
index modulo resolution, packed in 9-bit lanes. The centered window fixture
uses resolution/2 on each axis. The shared fixture computes it from its
center and extent, including the translated room. CPU GDF readback checks
map window-relative voxels to the same wrapped texels.

The CPU reference fixtures establish the region/band layout but use a
single voxel at texel zero. Their zero window origin is not appropriate
for these world-space GPU volumes. A first GPU run confirmed that copying
zero verbatim makes the centered aperture test pass while room transport
fails; the final fixtures use the production origin calculation instead.

The window fixture also writes its packed R16 evidence under
`scripts/rendering/test_sdf_dynamic_gpu_artifacts`, matching its runner
after the rendering-script directory move.

## Build and existing test coverage

The standalone programs are not targets in `runtime/build.zig`; their
PowerShell runners compile them using `zig c++` and compile production
HLSL using the vendored DXC. Every runner is launched from the repository
root with `.tools/zig-x86_64-windows-0.15.2` prepended to PATH.

The runtime build used the same vendored Zig 0.15.2:

```text
Shader integrity OK (139 shaders verified)
EXIT=0
```

`runtime/build/bin/test_runtime.exe
--gtest_filter="GIGpuTest.*:GIResourceValidity.*:MeshSdfCacheEntry.*"` passed
23/23 tests, exit 0. The unavailable DX12 debug interface logs
`0x887A002D`, but these tests passed without skips. clangd is not installed,
so compiler execution supplies fixture diagnostics.

The refreshed standalone fixtures are the regression tests for the current
shader ABI. Their existing semantic checks, negative controls, BVH field
hash comparison, and new layout assertions cover the change; no production
shader or gtest expectations were weakened.

## Runner evidence and blocked gate

All commands below ran from `E:/UzmiEngine/uzengine` as
`powershell -NoProfile -File scripts/rendering/<runner>`, with the vendored
Zig directory first on PATH. Output logs are `scripts/rendering/sdffix-*.log`.

| Runner / arguments | Exit | Result |
| --- | --- | --- |
| `test_sdf_window.ps1 -BvhCompare` | 0 | All 2,097,152 fp16 mesh voxels bit-identical; brute and BVH both preserve the aperture at mesh128/GDF256, mesh voxel 37.500 cm, combined bias 28.516 cm. |
| `test_sdf_gpu.ps1` | 0 | 32,768 mesh voxels, invalid/wrong-sign/sentinel counts zero; max error 0.000960. Enclosed history feedback relative error 0.023452; empty sky relative error 0.024672. Reflection room/transformed/mixed-validity max absolute error 0.000195. Intentional negative controls rejected. |
| `test_sdf_refined_controls.ps1` | 0 | Five reflection controls and narrow window pass; trace failures zero, room max absolute error 0.000195, empty/dark error zero. |
| `test_sdf_surface_origin.ps1` | 1 | Wrapper compile error: `GatherSdfDiffuse` requires eight arguments, but seven were provided (`axis.hlsl:121`, candidate `sdf_diffuse_gather.hlsli:119`). No new surface-origin measurement. |
| `test_sdf_diffuse_gpu.ps1` | 1 | Same seven-versus-eight-argument compile error in `surface-gather.hlsl:90`. No new diffuse measurement. |
| `test_sdf_reflection_energy.ps1` | 0 | Uniform and layered checks pass; 64 pixels, eight rays per pixel. |

Energy measurements, RGB:

```text
uniform1:
  karis_over_plain=(0.9965,0.9949,0.9949)
  plain_mean=(0.7929,0.2003,0.1002)
  karis_mean=(0.7901,0.1993,0.0996)
layers0.05/1/20:
  karis_over_plain=(0.8845,0.8846,0.8846)
  plain_mean=(1.1941,0.3006,0.1503)
  karis_mean=(1.0561,0.2659,0.1330)
```

The fixture ABI refresh builds and BVH identity passes, but the overall
gate is **not verified** because two required runners cannot compile.
Per the task's explicit stop condition, their stale helper calls were
reported rather than changed by guessing the new argument. No production
lighting change, archive, commit, or full-suite pass is claimed.
