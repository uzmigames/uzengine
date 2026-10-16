# SSR linear HDR exposure correction

## Gate registered before the correction build

With SSR on and the exposure adapting, the ratio of the SSR hit colour to the previous frame's scene colour
at the hit stays within 1 +- 0.5 % on every frame of the adaptation; before the change it follows the
per-frame exposure ratio (negative control). At a steady exposure the frames are bit-identical to the build
before. Defaults (`r.SSR` 0) unchanged.

## Investigation

The previous colour is copied from the TAA history (or raw HDR without TAA), before
tonemapping. These textures contain linear, non-pre-exposed HDR. FidelityFX's
current/previous exposure correction applies to pre-exposed colour, not to this
input. The trace must therefore receive 1.

There is an important mismatch with the proposal's expected negative control:
`RegisterPostProcessPasses` updates `m_ssrPrevExposure` while registering passes.
`scene_render_frame.cpp` calls that method before `rgBuilder.Execute`, which
invokes the SSR trace callback. The old callback consequently divides current
exposure by the already updated current exposure on the normal copy path.
This is source evidence, not a measured negative control; do not report an
exposure-ratio defect as reproduced.

The existing region-dump records expose TAA colour and exposure, plus the
lighting reflection after composition. They do not expose raw SSR trace colour,
hit UV, or the sampled previous colour at that UV. Comparing those existing
channels would not measure the specified hit/source ratio. That gate remains
unverified unless dedicated trace instrumentation is added.

## Implementation and coverage

Remove the exposure-history field and its registration-time update, and pass
`1.0f` in the existing trace root-constant slot. Keep the shader layout and
sampling arithmetic unchanged; update its comments to describe linear HDR.
SSR's default remains off.

There is no CPU unit-test seam for this pass-local root-constant assignment and
GPU sample. A source-text pin test would not validate the behavior. Runtime
capture evidence and the existing runtime suites are the relevant checks.

## Validation results

Vendored Zig 0.15.2 build from `runtime/`: `EXIT=0`;
`Shader integrity OK (139 shaders verified)` in `runtime/build_ssr.log`.
`./build/bin/test_runtime.exe --gtest_brief=1`: `TEST_EXIT=1`.
7663 tests from 748 suites ran: 7579 passed, 81 skipped, 3 failed.
The only failures are the known unrelated WIP:
`VfxEditorSlate.WorkbenchHasTwoColumns`,
`VfxEditorSlate.TitleBarReflectsEmitterName`, and
`VfxEditorSlate.StatusBarBindingsReflectTelemetry`. No owned suite failed.
Clangd is not installed, so LSP diagnostics were unavailable.

Captured `reflection-reference.uzproject`, 1920x1080, frame 120, 122 requested
frames, `r.AutoExposure=0`, `r.Exposure=1`, with SSR on and separately with
the default SSR off. Every manifest records preload 4, exposure 1, assets ready.
Baseline executable hash: `fnv1a64:cc960284678f348a`; corrected executable:
`fnv1a64:be89ee6357c554ce`. Corrected captures both exited 0 with no shader
compile failure or device removal in their logs.

Both before/after PNG pairs are byte-identical, which also proves identical
decoded pixels (maximum and mean pixel difference 0):

| Case | PNG bytes | Before/after identity |
| --- | ---: | --- |
| `r.SSR=1`, steady exposure | 864385 | Exact |
| default `r.SSR=0`, steady exposure | 849513 | Exact |

This checks one frame in one scene per condition, not every possible steady
frame. The inspected SSR-on image contains the coloured blocks reflected in
the floor; the default image has no floor reflections.

Artifacts are outside the repository at `E:/UzmiEngine/ssr-before-on_120.png`,
`ssr-after-on_120.png`, `ssr-before-default_120.png`, and
`ssr-after-default_120.png`, with matching `.json` manifests and `.log` logs
(without the `_120` suffix). Per-frame hit/source ratios before and after
adaptation remain **unverified**; no numerical ratio is inferred from these
steady screenshots. The full adaptation acceptance gate has not passed, and
task 1.1 remains open.
