# RC probe support follow-ups: verification

## Build and runtime tests (2026-10-13)

From `runtime/`, the vendored Zig 0.15.2 build ran after checking that no Zig
process was active:

```
E:/UzmiEngine/uzengine/.tools/zig-x86_64-windows-0.15.2/zig.exe build
```

`runtime/build_rcprobe.log`: `Shader integrity OK (139 shaders verified)`.
The monitored command completed with `EXIT=0`.

```
build/bin/test_runtime.exe --gtest_filter=-VfxEditorSlate.*
```

`runtime/gtest_rcprobe_owned.log`: 7656 tests from 749 suites, 7575 passed,
81 environment-dependent skips, zero failures; `GTEST_EXIT=0`. This includes
`DDGIDiagnosticReadback.MapsOnlyAfterFourFramesWithoutWaitingIdle`, which passed
in 17 ms. Clangd is not installed, so LSP diagnostics were unavailable; the
vendored compiler and tests provide the C++ verification.

The excluded WIP suite was run separately:

```
build/bin/test_runtime.exe --gtest_filter=VfxEditorSlate.*
```

`runtime/gtest_rcprobe_vfx_wip.log`: 6 passed, the 3 known failures:
`WorkbenchHasTwoColumns`, `TitleBarReflectsEmitterName`,
`StatusBarBindingsReflectTelemetry`. No VFX code or test expectation was changed
by this task.

## Exercised GPU run

The existing `flicker.runner.run_capture` ran
`sample/projects/sponza.uzproject` for 130 frames with the shipped classification
3, relocation and visible seeds, and overrides enabling DXR GI, both transport
diagnostics and the reach-grid dump. Capture timeout was 240 seconds, no GPU
validation flag was supplied, and a screenshot was requested at frame 126.

Artifacts:
`.screenshots/flicker-elimination/rc-probe-support-followups/h71-shipped/`
contains `run.log`, `manifest.json`, and `frames/f_126.png`.
`runtime/h71_rcprobe_summary.log` records:

```
CAPTURE_EXIT=0
READBACKS=[('RC transport', '120', '124'), ('DDGI transport', '120', '124')]
H71_PASS
```

The visible-seed log records `visibleSeeds=true relocate=1`. The captured
1920x1080 frame was inspected: geometry, lighting and the GI/DDGI panel rendered
normally, not blank. The exercised run did not hang. A single successful run
does not establish the cause or absence of the historical intermittent hang.
