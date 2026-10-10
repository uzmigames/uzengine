# Design: per-pass GPU timings in `--diag`

## Context
The render graph brackets every pass with `GpuProfilerBeginTimestamp` / `EndTimestamp` when
`SetGpuProfileEnabled(true)`; `uzengine.exe` enabled that only for `--run-manifest` and never
called `PassInspector::SetTimingData`, so `DIAG_JSON` always carried `"timing": []`.

## Decisions
- **Ring depth 4, resolve N-3.** The host waits for frame N-3's fence before recording frame N
  (`uzengine.cpp`, `kFramesInFlight` 3). Resolving at the end of frame N is therefore safe for the
  slot of frame N-3 and for no younger frame. `kProfilerFramesInFlight` = 4 keeps the slot of the
  frame being resolved out of the three that may be in flight. The constant is shared with the
  DX11/Vulkan/Metal profiler classes, which only size their arrays with it.
- **Per-slot names and counts.** Names are copied (48 characters) into the slot at `BeginTimestamp`
  and the pair count is stored at `EndFrame`; the resolve reads exactly `count` pairs, so a frame
  with fewer passes never exposes stale pairs of an older frame and a one-frame pass keeps its own
  name. Dangling pointers are impossible for the profiler; `RgBuilder::AddPass` still stores the
  pointer, so graph pass names remain literals (the multi-scatter passes were fixed).
- **One accumulation.** The diag report and the manifest read the same `RunStats` window (sum,
  count, min, max per pass; GPU frame span per frame). `_total` stays the sum of means (consumers
  such as `measure_gpu_cost.py` read it); `_frame` is the measured span.
- **Window for `--diag`.** A diagnostic run is capped at 120 frames after preload; without
  `--timing-first` the window opens at a quarter of the budget (frame 37 + 30 on Sponza). An
  explicit `--timing-first` is honoured as before.
- **Log opt-in.** The per-frame pass log (one line per pass per frame) is behind
  `r.GpuProfiler.Log`; nothing parsed it (grep over `scripts/`).

## Results
See `docs/analysis/lighting-block-b-01-pass-timings.md`: harness 2/7 -> 7/7; Sponza project
configuration at 1080p: GPU frame 21.6 ms, DeferredLighting 8.7 ms, SSGI (final gather) 8.7 ms,
GTAO 1.25, Shadow 0.9, RadianceCache_Trace 0.72 — the two GI consumers are 81 % of the frame.

## Rejected
- Keeping ring depth 3 and resolving after an explicit fence wait: it would add a stall per frame
  or move the resolve to the next frame's start, which the host already does implicitly through
  the allocator fence; a deeper ring costs 128 queries.
- Making `GpuTimingResult::name` an owning string: the result vector is rebuilt every frame and its
  consumers copy immediately; the per-slot buffer gives the same safety without allocation.
