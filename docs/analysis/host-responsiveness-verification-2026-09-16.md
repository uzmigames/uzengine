# Host responsiveness: verification of the SponzaTest review against this checkout

Date: 2026-09-16. Scope: the two reports written in the SponzaTest checkout —
`E:/SponzaTest/docs/analysis/rendering/sponza-host-responsiveness-review-2026-09-16.md` (static review)
and `sponza-host-measurements-2026-09-16.md` (eight bounded runs) — checked claim by claim against the
source in THIS checkout (`E:/UzmiEngine/uzengine`). No engine code was changed and no workload was run
for this verification. The measured numbers were NOT reproduced here; only the code claims were.

## Claims confirmed in this checkout

| # | claim | evidence here |
|---|---|---|
| 1 | DDGI traces every probe ray once per atlas texel | `ddgi_probe_update_cs.hlsl:342` one thread per texel; `:394-398` the direction comes from `Hammersley(i + frameJitter, ...)` — the ray index and the frame, not the texel; `:400` `TraceProbeRay(probePos, rayDir)` inside that per-texel loop; `:405-407` the texel weight is applied AFTER the trace. `kProbesPerAxis = 16` (`ddgi_system.h:64`), tiles 6 and 16 (`:71`, `:74`). 4096 × (36 + 256) × 32 = 38,273,024 trace calls per update against 4096 × 32 = 131,072 distinct (probe, ray) pairs: 292 per pair |
| 1b | the depth pass computes radiance it does not consume | `:400-403` radiance is read in both passes; in the depth branch (`:410-430`) it feeds only the diagnostic luminance |
| 2 | the SDF radiance cache keeps updating when DDGI is on | `frame_gi_passes.cpp` `m_radianceCache.UpdateProbes(...)` runs whenever the RC pipeline is valid, then DDGI when `wantDdgi`; the DDGI consumer path also requires `m_radianceCache.IsInitialized()` and copies its grid origin (`frame_constants.cpp:800-810`). Whether the RC's probe UPDATES are needed once DDGI wins in the consumer is not settled here |
| 3 | loading blocks the event loop | `uzengine.cpp:995-1016`: messages are pumped only between iterations; five `WaitIdle` per iteration; no minimized check in that loop. `scene_mesh_assets.cpp:102-103` says it in its own words: `FlushSdfBakes()` does a hard `WaitIdle` per batch "which freezes the main thread"; `:246-250` bakes the ENTIRE pending list in one batch, no budget |
| 4 | no background rendering budget | `uzengine.cpp:1035-1040` the loop pauses only when minimized (10 ms sleep); no focus-based throttle |
| 5 | the frame limit is a post-preload frame count, not elapsed time | `uzengine.cpp:1030-1031` `frameLimit = frameIndex + effectiveMaxFrames`, taken after preload |
| 6 | the printed FPS mixes preload frames into the denominator | `uzengine.cpp:1028` `startTime` is taken after preload; `:1215` `LogRenderStats(frameIndex, totalMs)` divides by a `frameIndex` that kept counting through preload. With 37 preload frames the printed ms/frame is understated; their correction of the historical 24 FPS to 18.08 FPS (140444.3 ms / 2539 post-load frames = 55.32 ms) is arithmetically right |
| 7 | no per-pass GPU timings | `--gpu-profile` appears 0× in `uzengine.cpp` (1× in `application_args.cpp`, the other entry point); `PassInspector::SetTimingData` has NO caller anywhere, so `DIAG_JSON` always carries `"timing":[]` — confirmed empty in this session's own `--diag` runs |

## Where this checkout differs from the review's reading

* **There is no 500 FPS cap in this entry point.** `rhi_frame_limiter.h` does default to 500 FPS and
  does return immediately when a frame already exceeds its budget — but `uzengine.cpp` never
  instantiates a `FrameLimiter` (0 references). The binary renders as fast as it can, unconditionally.
  Same practical conclusion (no headroom for the desktop), different mechanism: nothing to tune, only
  something to add.
* **The capture harness here does have a wall-clock bound.** `scripts/flicker/runner.py:145,175` runs
  the engine under `subprocess.run(..., timeout=1800)`. Thirty minutes is generous, and a stalled
  preload is still not caught by the frame limit (claim 5), but "waits indefinitely" describes
  `verify_sponza_loading.ps1` in the other checkout, not this runner.
* **The H-46/H-47 studies exist here**, not there: `scripts/run_door_energy_neighbourhood.py`,
  `scripts/run_door_coverage_depth.py`, data under `.screenshots/flicker-elimination/35-*` and `36-*`.
* **This checkout's region-dump instrumentation costs nothing in a normal run**: every record write is
  behind `_debugRegionUav == 0xffffffff` and only a `--region-dump` capture binds a UAV. Binaries from
  the two checkouts are comparable on frame time in that respect.

## One consistency check the measurements allow

Disabling DDGI saved 2.77 ms per frame at 1080p; enabling it cost 1.93 ms at 720p. A fixed-size
workload should cost the same at both resolutions, and it roughly does: the DDGI atlas does not scale
with the screen. At 38.3 M ray queries per update, 2.77 ms is about 13.8 G queries per second, which
is the order of magnitude an RTX-class GPU delivers on inline `RayQuery` against a small TLAS. So the
static count and the measurement agree: the redundancy IS the DDGI cost. Tracing each (probe, ray) once
would remove most of those ~2–3 ms — and that is about 5 % of a 58 ms frame at the full preset, or a
third of a 5.9 ms frame at 720p with the cheap settings. Both statements are true; which one matters
depends on the configuration being fixed.

## Not verified here

The eight measured frame times, the 56 % attributed to SDF reflections, the GPU/CPU/memory samples and
the 37–38 s load were taken in the other checkout with its runner and were not re-run. The DXR
launcher's exact settings live there. The Windows-wide freeze remains, as their report says, only
partially diagnosed: expensive GPU work is reproduced; RAM exhaustion and broad CPU saturation are
not; driver/DPC stalls are neither established nor excluded.

## Corrections from review (2026-09-16, later the same day)

Three statements above were wrong or too strong. They are left in place; this section supersedes them.

1. **The 500 FPS cap IS active in this checkout.** It lives in the swap chain, not in the entry point:
   `dx12_swapchain.h:118` holds a `FrameLimiter m_frameLimiter` and `dx12_swapchain.cpp:236` calls
   `WaitForTargetFrameTime()` inside Present. Finding no reference in `uzengine.cpp` showed nothing
   about this build, and nothing at all about the other checkout. The review's original reading
   stands: the limiter returns immediately once a frame already exceeds its 2 ms budget, so it gives
   the desktop no headroom — but it is there.
2. **The preload loop does not wait five times per iteration.** Two `WaitIdle` are on the normal
   path of every iteration (`uzengine.cpp:997`, `:1004`); the other three (`:1009`, `:1014`, `:1016`)
   run once, inside `if (IsSceneFullyLoaded())`, to seed the exposure. The loop still pumps messages
   only between iterations and still ignores the minimized state.
3. **2–3 ms is the observed effect of DISABLING DDGI, not the gain of deduplicating its rays.** A
   corrected DDGI still traces 131,072 rays, stores their results and integrates them per texel; how
   much of the 2.77 ms survives is unmeasured. The 13.8 G queries/s figure was a plausibility check
   between the static count and the measured difference. It is not a measurement of the pass, and
   only per-pass timestamps would be.

Also corrected in the same review: the earlier 24 FPS figure was a reading error on top of the
engine's denominator bug, not the bug alone.

### Order agreed for the follow-up work

1. Fix the frame-time metric and wire per-pass timings, verifying the synchronisation of the
   timestamp readback.
2. Measure, then reduce, the SDF reflection cost.
3. Frame pacing with real headroom for the desktop, and reduced load in the background.
4. Split loading into smaller batches with message processing between them.
5. Deduplicate the DDGI tracing and validate visual equivalence.

The central point is unchanged: excessive engine load is identified; the full cause of the
Windows-wide freeze is not demonstrated. Results from the two checkouts stay separate.
