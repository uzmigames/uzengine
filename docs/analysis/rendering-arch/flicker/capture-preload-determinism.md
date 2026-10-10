# Capture reproducibility: a pre-load whose length does not depend on thread timing

Task: `.rulebook/archive/2026-10-10-phase0_capture-preload-determinism` (2026-10-09). Ledger H-73.

## The finding

`uzengine.exe` renders frames until the scene is fully loaded (the pre-load, nothing presented), counts them
in the frame index, and only then starts the render loop. On Sponza the pre-load took 43 or 44 frames in the
followup-09 and followup-14 runs of 2026-10-09, 44 in the render-graph state harness an hour later and 39
after that, all on one Debug binary. A process with a different length landed in a different persistent TAA
state:

- followup-14 diagnostic (four processes of one configuration, dumps kept): the 43-frame process differed from
  the three 44-frame ones by up to 0.192 in linear radiance at frame 237, with the TAA's current-frame input
  identical at that pixel (7.6055) and within 4.1e-05 anywhere, and the history 0.189 apart. The history clamp
  makes the resolve settle into history-dependent states, so the difference did not decay over 190 frames.
- followup-09 repeats (six processes, dump on and off): the 43-frame process differed from every other by up
  to 80 code values (mean 0.15) at frames 62-64; among the 44-frame processes the dump on / off pair was
  bit-identical once and at most one code value apart otherwise.

So the cross-process floor that the flicker programme's equivalence checks are judged against was mostly the
loader's thread timing, not the renderer. It is also why Sponza frames differ between build configurations.

## The cause

Mesh loads are budgeted (4 per pre-load frame in every run), texture decodes are drained synchronously each
pre-load frame (`DrainPendingTextureLoads` loops until no decode is pending), and copy-queue uploads are
waited for. The mesh-SDF bake queue was the exception: its worker threads prepare each bake (cache read or
CPU build), and every frame the main thread started whatever the workers had finished by then. In a 44-frame
run some frames started none and later frames bursts of 9 or 10, with the last starts at frames 37 and 40;
in a 39-frame run every bake had started by frame 35.

## The fix

In pre-load mode (`SceneRenderer::SetPreloading(true)`), `MeshSdfBakeQueue::Service` waits until no
preparation is queued or running (a condition variable the workers notify) and starts the prepared bakes in
mesh order. Outside the pre-load nothing waits. The wait is `r.GI.MeshSdf.PreloadWait` (default 1);
`r.GI.MeshSdf.Diagnostic.PrepareDelayMs` makes every worker sleep before each preparation, to test it. The run
manifest records the length (`frames.preload`).

## The gate (registered before the build; amended once, also before it)

The first negative-control run on the build before (45545f4e, six unloaded processes) gave 39 frames
everywhere; under 32 CPU busy loops on every second process the same build split 38 / 39 (worst pair 2 code
values). Machine load moves the length unreliably, so the gate became an intervention inside the fix's own
build (`scripts/rendering/test_preload_determinism.py --gate`, three interleaved rounds):

| condition | pre-load (frames) | result |
|---|---|---|
| A defaults (the wait, no delay) | 38, 38, 38 | |
| B the wait, 30 ms worker delay | 38, 38, 38 | A/B screenshots within 1 code value |
| C no wait, 30 ms worker delay (negative control) | 45, 45, 45 | the delay moves the length without the wait |

**PASS.** The same build under 32 CPU busy loops on every second process (`--stress 32`, six processes): 38
in all six, the manifest agreeing, worst pair 1 code value: **PASS**.

Processes of equal length still differ by up to one code value at a few pixels, so bit-identity across
processes is not restored in full. The allocation-layout dependence found on the same day (frames moved by
one code value when one pass skipped its resources) is the next suspect; it stays open as item 2.4.

## Consequences for the harnesses

- Comparisons of the build before this fix should pair processes by pre-load length (every run log states
  it, and from this fix on the manifest).
- followup-09 and followup-14, which failed on this floor, passed on the fix's build: `test_region_capture.py`
  23/23 (the neutrality check powered: processes within 0.00018 code values on average, against up to 80 code
  values before) and `test_correction_gates.py --repeats 4` 11/11 (the default configuration's process spread
  0.192 -> 1.19e-07).
- Outside the pre-load nothing changed: the six small references of the render-graph state harness (no
  distance-field bakes) are bit-identical to the build before; Sponza differs only through its pre-load length
  (now 38 frames, 44 in the build before's captures), which is the point of the fix.
