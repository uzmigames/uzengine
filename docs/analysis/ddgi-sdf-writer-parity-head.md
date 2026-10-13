# DDGI / SDF writer parity on the current scene

## Pre-registration (2026-10-13, before the first capture)

The SDF fixture prerequisite is satisfied: BvhCompare is bit-identical.
For item 1.2, run `test_gi_energy_gpu.ps1 -Frames 200` on the same
workspace before and after the TLAS filtering change. Preserve each log
and run `gi_energy_populations.py` on both.

The primary population is RC class 8 inside the building: cell x 5..12,
z 7..14, all y. Both writers trace this population from the grid position.
Report its count, ratio of mean DDGI/RC radiance, and median/p10/p90 of
per-probe ratios where RC mean > 0.05. Report the script's other populations
as context, not replacements if the primary population disagrees.
The descriptive parity band is 1 +/- 0.10; no parity threshold gates the
implementation. Missing/non-finite diagnostics invalidate the measurement.

For item 1.3, use `run_probe_indirect_residual.py` with
`r.GI.MeshSdf.TwoSided=0` and `=1`, two repeats each, separate output
directories. Retain the existing Camera C, 240-frame, full-grid, identical
direction protocol and R1/C criteria unchanged. Report R1 measured/predicted
ratios, normalized median, within-band fraction, and repeat agreement.
H-61's building sky-escape term will additionally be reported on the same
inside-building class-8 population as item 1.2, from the retained per-probe
logs: mean DDGI sky fraction minus mean RC sky fraction, in percentage
points, and the mean fractions separately. This is a measurement, not an
attribution of a defect or a decision to change TwoSided's default.

## Results

### Population correction for the residual protocol

The first residual captures completed but the added class-8 summary stopped
analysis: the unchanged Camera C project uses classification mode 0 and its
logs contain class 0, not class 8. The original R1 population and criteria
remain unchanged. The supplementary H-61 summary uses building cells
x 5..12, z 7..14 with fieldDistance >= 5 cm. This population correction
was made after captures, before reading numerical residual results, and is
not the pre-registered class-8 population of item 1.2. Class-8 H-61 numbers
are unavailable under this protocol. Both writers use grid origins under
mode 0; the same-direction/full-grid alignment gates still apply.

### Implementation and checks

`runtime/engine/renderer/core/gbuffer/draw_list.h:183` now owns
`IsGIOccluder`: blend bits [1:0] must be < 2 and decal bit 4 must be clear.
This is the extracted SDF membership rule, not a new filtering policy.
`frame/frame_gi_passes.cpp:209` calls it before testing mesh/SDF availability.
`raytracing/tlas_manager.cpp:79` calls it before mesh lookup or BLAS creation.
Previously TLAS checked only mesh validity and included every material domain.
Mesh availability remains a separate backend requirement.

`DrawCommand.GIOccluderMembership` in
`runtime/tests/engine/renderer/test_material.cpp:19` checks opaque, masked,
translucent, additive, opaque decal, masked decal, and packed-ORM opaque draws.
The residual runner accepts repeated `--cvar NAME=VALUE` overrides, records
them in its report, and logs the supplementary H-61 sky-fraction summary.

Both baseline and post-change builds used `.tools/zig-x86_64-windows-0.15.2/zig.exe`
from `runtime/`, serially. Both returned `EXIT=0` and
`Shader integrity OK (139 shaders verified)`.
The owned regression command was:

```text
./build/bin/test_runtime.exe --gtest_filter="DrawCommand.*:DrawList.*:PBRMaterial.*:SortKeyOpaque.*:SortKeyTransparent.*:PerFrameCBLighting.*:MeshSdfCacheEntry.*" --gtest_brief=1
[==========] 36 tests from 7 test suites ran. (0 ms total)
[  PASSED  ] 36 tests.
GTEST_EXIT=0
```

Separately, `VfxEditorSlate.*` reproduced the three known unrelated failures:
`WorkbenchHasTwoColumns`, `TitleBarReflectsEmitterName`,
`StatusBarBindingsReflectTelemetry` (9 tests, 6 passed). These tests were not
modified. The full runtime suite was not run. clangd is unavailable, so LSP
diagnostics could not run; the successful build supplied compiler validation.
Normal DX12 captures were used, not GPU validation.

### Item 1.2: writer parity

Before and after captures both ran 200 frames and passed the energy harness's
6 measurement checks. TLAS instance count changed from 138 to 129. The SDF
grid mean remained 1.330234; DDGI grid mean was approximately 1.284985 in both
runs. The engine grid ratio is 0.9660 but averages different populations.

Results from `gi_energy_populations.py`:

| Population | n | Before mean ratio | After mean ratio | Before median | After median |
| --- | ---: | ---: | ---: | ---: | ---: |
| RC grid population (field >= 5 cm) | 3885 | 0.9862 | 0.9862 | 0.9947 | 0.9946 |
| RC valid, class 8/9/10 | 3904 | 0.9729 | 0.9728 | 0.9944 | 0.9943 |
| Valid inside building | 832 | 0.9419 | 0.9423 | 0.9325 | 0.9322 |
| **Primary: inside building, class 8** | **664** | **0.9259** | **0.9265** | **0.9218** | **0.9213** |
| Valid outside building | 3072 | 0.9777 | 0.9777 | 0.9971 | 0.9971 |

Primary means: DDGI/RC 0.8235/0.8894 before and 0.8240/0.8894 after.
Primary per-probe distribution (RC > 0.05, n=511): p10/p90
0.7636/1.0350 before, 0.7613/1.0260 after. The primary mean ratios are within
the descriptive 1 +/- 0.10 band; this is one capture per condition and does
not establish that the small difference is statistically significant.

Raw logs: `scripts/rendering/ddgiw_before.log`, `ddgiw_after.log`;
energy summaries: `ddgiw_before_summary.log`, `ddgiw_after_summary.log`.
Build/test logs: `runtime/build_ddgiw_before.log`, `build_ddgiw.log`,
`test_ddgiw_owned.log`, `test_ddgiw_vfx_known.log`.

### Item 1.3: TwoSided residual

Two repeats per condition, Camera C, 240 frames, full grid, identical ray
directions. The pre-existing R1 test asks whether the indirect term is the
sole writer residual; it is not an acceptance gate for the TLAS fix.

| TwoSided | R1 n | Measured ratio median | Predicted ratio median | Measured/predicted median | p10 / p90 | Within +/- 0.05 |
| --- | ---: | ---: | ---: | ---: | --- | ---: |
| 0 | 571 | 1.4052 | 0.8465 | 1.6363 | 1.1505 / 1.8758 | 1.6% |
| 1 | 3271 | 0.9715 | 0.9515 | 1.0090 | 1.0005 / 1.1618 | 71.8% |

Model/null mean absolute errors: 0.4866/0.3628 for 0, 0.0544/0.0655 for 1.
Both conditions report R1 false and the registered reading `NOT SOLE`;
the required 90% within-band fraction is not met.
Both full runners exited 0 with status `complete`; both repeats agree at
the displayed precision and pass the original C repeat-agreement gate.

Supplementary H-61 building/field-valid mean sky fractions:

| TwoSided | n | DDGI sky fraction | RC sky fraction | DDGI minus RC (percentage points) |
| --- | ---: | ---: | ---: | ---: |
| 0 | 64 | 0.807617 | 0.539062 | 26.8555 |
| 1 | 441 | 0.286069 | 0.255102 | 3.0967 |

The populations differ because TwoSided changes the field validity set.
These aggregate differences must not be read as a fixed-population causal
effect. The registered item-1.2 class-8 population remains unavailable in
this mode-0 residual fixture. R2 indirect shares by layer and full precision
numbers are retained in the JSON reports.

Reports and capture manifests/logs are under
`docs/analysis/ddgi-sdf-writer-parity-head/twosided{0,1}/`.
Whether the remaining writer differences constitute a defect, and whether
any TwoSided policy should change, are judgment calls reserved for the parent.
