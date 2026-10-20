# D1: readable opt-in ground silhouettes

Item 4.10 is resolved on the dedicated `cloud-shadow-silhouettes` demo.
This is scene-only polish: no engine source, shader, global CVar default,
constant-buffer layout, sampler, or root-signature change. The global
`r.Clouds.GroundShadows` default stays OFF; this demo explicitly enables it.
The existing 24-DWORD root block is untouched.

## Tuning and visual verdict

Launch `scripts/rendering/cloud-shadow-silhouettes.uzproject`. The fixed camera
is at [0, -100000, 250000] cm looking at the origin, below the 3 km cloud base.
Its high-angle view fills the image with ground rather than horizon.
The sun pitch is -25 degrees, intensity 40, with matching atmosphere
illuminance. Positive blue-grey SkyLight fill is retained.

The cloud layer is 0.2 km thick, coverage 0.3, density 20, extinction 0.3.
Noise XY scale 1 puts several footprints in view. Noise Z scale 0.5 avoids
averaging unrelated shapes through the vertical column; detail erosion 0.15
retains coherent lobes. Density compensates for the thin, broken layer and
produces dark cores rather than faint broad attenuation. Two 1024-square
shadow cascades cover 12 km instead of the old 512-square/50 km fixture.
These are authored demo settings, not renderer defaults or shadow-only
extinction changes.

**Opened and inspected**:
`.screenshots/cloud-d1-polish/coherent/frames/f_28.png`.
Recognizable separated cloud footprints have dark blue, sky-filled interiors
and bounded soft edges against light grey ground: a large irregular central
patch, a forked patch above it, smaller detached patches below/left, and
larger partial patches at the frame edges. Wide lit corridors separate
them. The edges remain physically soft, not hard opaque cutouts, but the
silhouettes are legible rather than near-uniform ground darkening.

Comparison opened:
`.screenshots/cloud-m4/scattered-gbv-final/frames/f_20.png` shows the old
horizon framing with faint stretched ground modulation.
The initial new-scene attempt (`on/frames/f_28.png`) also retained overly
broad faint blobs; increasing XY frequency and sunlight alone
(`refined/frames/f_28.png`) improved contrast but not coherent shapes.
The thin vertically coherent layer is the final accepted configuration.

The same tuned camera with the gate disabled was captured and opened at
`.screenshots/cloud-d1-polish/off/frames/f_28.png`: nearly uniform light grey
ground, without the dark footprints. Thus these are cast cloud shadows,
not ground material shapes. Final ON RGB extrema are [21,45,73] to
[184,187,192]; OFF extrema [181,184,190] to [188,191,195].
15.70% of the ground is below half the OFF display brightness, while
43.38% exceeds 95% of OFF brightness. These are display-space contrast
measurements, not linear sunlight transmittance.

## Verification

- `cd runtime && zig build -Doptimize=ReleaseFast`: exit 0;
  `Shader integrity OK (144 shaders verified)`. Build monitor
  `mon_73ZY76NT9DFFD6FK` retained the successful completion.
- Final ON and OFF captures exit 0, 36 graphs each (32 requested frames plus
  preload). ON `coherent/run.log` has no `compile failed`, device removal or
  hang, zero state-claim disagreements and zero explicit double imports.
  All four shadow generation/resolve passes appear in `coherent/manifest.json`.
  Executable identity: `fnv1a64:a5eb257993c21e34`.
- Fresh `runner.run_capture` of
  `scripts/rendering/reflection-reference.uzproject`, 100 frames, screenshot
  95: exit 0. NumPy comparison of
  `.screenshots/cloud-d1-polish/default/frames/f_95.png` against
  `E:/UzmiEngine/f005-after/frames/f_95.png` gives **max_code_delta 0**,
  **different_values 0**, including alpha.
- `npm run test:layouts`: exit 0, A 85/85, B 27/27, C 4/4;
  inventory 163 declarations, zero unmapped. Log:
  `.screenshots/cloud-d1-polish/layouts.log`.
- `GTEST_FILTER=-RuntimeViewportTest.* zig build test -Doptimize=ReleaseFast`:
  7,811 run, **7,791 passed**, 17 skipped, the same three pre-existing
  `VfxEditorSlate.WorkbenchHasTwoColumns`,
  `VfxEditorSlate.TitleBarReflectsEmitterName`, and
  `VfxEditorSlate.StatusBarBindingsReflectTelemetry` failures; exit 1.
  Delta against the supplied 7,791/3 baseline: **0 passes / 0 failures**.
  Test time 35.823 s. No TDR or retry. Logs/XML:
  `.screenshots/cloud-d1-polish/tests.log`, `gtests.xml`.
- Every launched build/test/capture was bounded to at most 180 seconds.
  Final process check found zero `uzengine`, `test_runtime`, or `zig`
  processes. No commit or archive was made. No VFX WIP was edited.

Reproduce from the repository root with
`python docs/analysis/volumetric-clouds-rewrite/d1-silhouettes-verify.py MODE`.
Modes: `coherent`, `off`, `default`, `compare`, `metrics`, `build`,
`tests`, `layouts`. Captures use the existing runner and a 175-second bound.
Historical `on` and `refined` images record intermediate settings; rerunning
those names now uses the final scene.

## Complete owned changed-file list

- `scripts/rendering/cloud-shadow-silhouettes.uzproject`
- `scripts/rendering/cloud-shadow-silhouettes.uzscene`
- `scripts/rendering/cloud-shadow-silhouettes.lua`
- `docs/analysis/volumetric-clouds-rewrite/d1-silhouettes-verify.py`
- `docs/analysis/volumetric-clouds-rewrite/08-d1-silhouette-polish-2026-10-16.md`
- `docs/analysis/volumetric-clouds-rewrite/decisions-pending-2026-10-16.md`
- `.rulebook/tasks/phase0-3.1.0-volumetric-clouds-rewrite/tasks.md`

Capture artifacts are local under `.screenshots/cloud-d1-polish/`.
The pre-existing changes in `05-m4-execution-2026-10-14.md`,
`m4-scattered-verify.py`, and the pre-existing portions of the decisions
file belong to the surrounding session, not this polish.
