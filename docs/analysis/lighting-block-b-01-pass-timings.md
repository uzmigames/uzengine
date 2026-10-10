# Lighting block B, item 1 — per-pass GPU timings in `--diag` (2026-10-05)

Companion to the lighting assessment of 2026-10-05 (block B, "Custo: não há timings por pass no
--diag") and to `docs/analysis/host-responsiveness-verification-2026-09-16.md` (claim 7). Branch
`feat/sponza-gi-parity`, Rulebook task `phase0_lighting-block-b-01-pass-timings`.

## What was wrong

`uzengine.exe --diag` printed `"timing": []` on every run. The render graph already bracketed every
pass with a GPU timestamp pair (`RgBuilder::Execute`) and `--run-manifest` already wrote per-pass
means, but nothing enabled the profiler for a diagnostic run and `PassInspector::SetTimingData` had
no caller. Three defects in the DX12 path would have made the numbers wrong once read:

1. **Readback race.** The profiler ring had 3 slots, as many as the command-list ring. The CPU
   waits for frame N-3's fence before recording frame N, but the resolve at the end of frame N
   mapped the slot of frame N-2, which could still be executing on the GPU (stale or torn values,
   unsigned `end - begin` underflow showing up as a huge time).
2. **Names from the wrong frame.** One shared name table was overwritten every frame, so the
   timings of frame N-3 were labelled with frame N's pass list; a pass that runs on one frame only
   (a sky IBL re-bake) was attributed to whatever pass held its index three frames later, and the
   resolve stopped at a (0, 0) pair in a readback buffer that was never cleared.
3. **Dangling names.** The three multi-scatter passes registered `std::string` names
   (`.c_str()` of block locals) with `AddPass`, which stores the pointer; by execution they were
   freed.

## What changed

- `DX12GpuProfiler`: ring depth `kProfilerFramesInFlight` = 4 (command-list frames in flight + 1);
  each slot keeps its own copied names (`kGpuProfilerNameCapacity` 48) and the number of pairs it
  resolved; `ResolveFrame` reads exactly those pairs from the slot of frame N-3 and drops a pair
  whose end precedes its begin.
- `uzengine.cpp`: `--diag` and `--gpu-profile` enable the profiler (as `--run-manifest` did); a
  diagnostic run measures from the first quarter of its frame budget unless `--timing-first` is
  given (the default of 200 lay beyond the 120-frame run); per pass the window's mean, min, max
  and sample count are accumulated with the GPU frame span (`_frame`, first pass begin to last
  pass end) and handed to `SetTimingData` before `GenerateReport`; the manifest's `gpu_ms` gains
  `_frame` next to `_total` (sum of the means).
- `DiagnosticReport::ToJSON`: `timing` entries carry `gpuMs` (mean), `minMs`, `maxMs`, `samples`;
  the Lua `get_timing` table gains `min_ms`, `max_ms`, `samples`.
- The multi-scatter passes have literal names; the per-frame "GPU Profile" log is opt-in
  (`r.GpuProfiler.Log`, default off).

## Verification

`scripts/test_diag_timing.py`, scene `sky-rebake-reference` (the sun turns at Lua frame 100 and
queues one re-bake inside the measured window). Pre-registered: T0 a window after the last frame
reports nothing (control); T1 >= 8 passes with plain unique names; T2 finite statistics with
min <= mean <= max <= 100 ms; T3 the per-frame passes carry as many samples as the window has
frames; T4 `SkyCubemapCapture`, a graph pass registered only on the re-bake frame, carries one
sample under its own name while the `AtmosphereBake` scope (bracketed every frame) is sampled on
every frame with its max on the bake frame; T5 `_frame` >= every pass and >= 0.98 x the sum of
the passes; T6 manifest and report agree.

| | before (a13003d8) | after |
|---|---|---|
| `test_diag_timing.py` | 2/7 (timing empty; the manifest had 16 passes) | **7/7** (16 passes, 143 window frames, `SkyCubemapCapture` 1 sample, `AtmosphereBake` 143 samples with max 41.9 ms on the bake frame) |

Two observations worth keeping: a scope bracketed every frame around conditional work
(`AtmosphereBake`) reports its mean over all frames, so a once-per-run cost reads as a small mean
with a large max; and a tiny pass (the 64² cubemap capture) can read 0.000 ms — the timestamp
counter's resolution is not its frequency, so sub-microsecond passes are below it.

## Block B baseline — Sponza, project configuration, 1920 x 1080

`uzengine.exe --project sample/projects/sponza.uzproject --frames 200 --diag --run-manifest`,
two runs, window frames 87-237 (150 samples), the project's own GI configuration (radiance cache,
final gather, SDF reflections, GDF 256, mesh SDF 128, TAA). CPU frame 21.8-21.9 ms.

| Pass | run 1 (ms) | run 2 (ms) | share |
|---|---|---|---|
| **GPU frame span (`_frame`)** | **21.55** | **21.68** | 100 % |
| DeferredLighting (direct + GI consumer + SDF reflections) | 8.73 | 8.74 | 41 % |
| SSGI (the SDF final gather) | 8.59 | 8.75 | 40 % |
| GTAO | 1.25 | 1.26 | 5.9 % |
| Shadow (CSM, 4 x 4096²) | 0.90 | 0.89 | 4.2 % |
| RadianceCache_Trace | 0.72 | 0.72 | 3.4 % |
| DepthPrepass | 0.38 | 0.38 | 1.7 % |
| GBuffer | 0.37 | 0.37 | 1.7 % |
| APVolume | 0.18 | 0.17 | 0.8 % |
| TAA, Bloom, HiZ, translucency, sky, tonemap, velocity, overlay | < 0.06 each | | 0.7 % |
| GDF_Composite, AtmosphereBake, Present | 0.000 (nothing to do in a static window) | | |

The two GI consumers (final gather + lighting with the in-shader SDF reflections) are 81 % of the
GPU frame; everything the assessment lists for block B after the GDF (DDGI dedup, clustered
lights, fog) sits in the remaining 19 %. Run-to-run agreement of the per-pass means is about 1 %,
so a change below that is not measurable with 150 frames.

## Still open

- Only DX12 implements the `IRHIDevice::GpuProfiler*` calls; the DX11, Vulkan and Metal profiler
  classes are not reachable through the device interface.
- The FPS printed by `LogRenderStats` divides by a frame count that includes the preload frames
  (host-responsiveness claim 6).
- No GPU debug markers (PIX / debug-utils labels) are emitted; the same bracket in
  `RgBuilder::Execute` is where they would go.
