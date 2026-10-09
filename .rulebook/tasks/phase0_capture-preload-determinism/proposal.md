# Capture reproducibility: a pre-load whose length does not depend on thread timing

## Why

The followup close-out runs of 2026-10-09 (Debug build of 2cc2e85c) failed two gates on process-to-process
variation, and a third read SEPARATED on it:

- followup-14, `test_correction_gates.py --repeats 4`, 10/11: same-configuration processes differed by up to
  0.192 in the linear TAA output (6.2 % of the mean) against a 2 % power bound.
- followup-09, `test_region_capture.py`, 22/23 on one dump on/off pair; amended to three processes per
  condition (registered before its run), the comparison was indistinguishable but unpowered: processes of
  one condition differed by up to 80 code values at frames 62-64.
- followup-11, step 5 of the display study, read SEPARATED on a 2.5 ppm shift; the pre-registered re-run
  passed and read the failure as chance, with a same-configuration max spread of 0.23.

The cause, measured:

- **Every outlier process had a shorter pre-load.** `uzengine.exe` renders frames until the scene is fully
  loaded and counts them in the frame index. Pre-load took 43 frames in the odd process and 44 in the rest
  (the followup-14 diagnostic: one of four; the followup-09 repeats: one of six). Its asynchronous parts run
  on worker threads (the mesh-SDF bake preparation, texture decodes), so the frame on which each finishes
  depends on thread timing, and the count of "batch SDF bake" starts differed with it (35 vs 36).
- **The difference lives in the TAA history, not in the frame.** At frames 237-252 the current-frame input
  of the TAA agreed to 4e-5 between the 43- and 44-frame processes while the history differed by 0.189 at
  the worst pixel (17 281 pixels > 1e-3). The history clamp makes the resolve settle into
  history-dependent states, so the difference does not decay over 190 frames.
- **Without that difference the runs reproduce.** Among the 44-frame processes of the followup-09 repeats,
  the dump on and dump off runs were bit-identical in one pair and one code value apart in the others; the
  43-frame process differed from every one of them by up to 80 code values. The five small reference scenes
  of the render-graph state harness (pre-load 3-5 frames) were bit-identical between processes.

The renderer is therefore not irreproducible as such: the capture pipeline lets thread timing choose
between a few persistent states. Every cross-process comparison of the flicker programme carries that
lottery as its floor, and the build configuration moves it (CPU speed sets the pre-load length: the
"Sponza frames differ between builds" trap).

## What Changes

- In pre-load mode (`SceneRenderer::SetPreloading(true)`, nothing presented), the mesh-SDF bake queue waits
  for its worker preparations before starting bakes, and starts them in mesh order, so the frame on which
  every bake starts and completes depends only on the scene.
- The pre-load length is logged and recorded in the run manifest; the other asynchronous sources of the
  readiness check are audited for the same timing dependence and made to wait in pre-load mode where they
  show it.
- Outside pre-load nothing changes (the throttled runtime streaming keeps its budgets).

## Gate (registered before the first build of the change)

On the Debug build: 6 processes of `scripts/sponza-flicker-w.uzproject` at fixed exposure all report the
same pre-load length and screenshot frame 237 within 1 code value of each other (bit-identical expected);
then `test_region_capture.py` 23/23 and `test_correction_gates.py --repeats 4` 11/11 (the followup-09 and
followup-14 close-outs). Negative control: the build before shows the 43/44 split in the same 6-process
run, or the gate reports that it could not reproduce the problem.

## Impact
- Affected specs: capture-reproducibility (new)
- Affected code: runtime/engine/renderer/gi/mesh_sdf_bake_queue.*, scene/scene_mesh_assets.cpp,
  runtime/tools/uzengine.cpp (pre-load length in the manifest), possibly the texture decode drain
- Breaking change: NO (pre-load scheduling only; the presented frames are those of a fully loaded scene)
- User benefit: captures that reproduce across processes and build configurations; the flicker programme's
  equivalence checks measure the renderer instead of the loader's thread timing.
