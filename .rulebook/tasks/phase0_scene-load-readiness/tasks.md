## 1. Implementation
- [ ] 1.1 Add `SceneLoadReadiness { ResourcesResident, SceneComplete, CaptureReady }` to `SceneRenderer` and make it the loop's contract; `IsSceneFullyLoaded()` stays as a query equal to `SceneComplete`.
- [ ] 1.2 Add a temporal frame counter that restarts at reveal and a reveal reset that covers TAA jitter and history, SSGI, SSR, denoiser guides, GTAO, clouds, env-MIS and auto exposure.
- [ ] 1.3 Replace the pre-load loop in `uzengine.cpp` with a loading service loop: pump messages, `ServiceLoading`, present the loading overlay; keep the two exposure-seed frames before reveal.
- [ ] 1.4 Lift the SDF completion cap while loading and suppress Global Distance Field composition until `SceneComplete`; record one whole-window recompose there.
- [ ] 1.5 Advance texture finalize and deferred mesh flush on explicit fence completion instead of the next `FlushPendingLoads` call.
- [ ] 1.6 Default `r.GI.MeshSdf.PreloadWait` to 0 once 2.1 passes; keep the CVar one release with a deprecation log line.
- [ ] 1.7 Add `r.LazyLoad.BudgetMs` for post-reveal main-thread integration; keep the 8-per-frame SDF cap for regional GDF recomposes.
- [ ] 1.8 Decompress SDF cache hits on workers into staging buffers pre-mapped by the main thread; record SDF uploads on the copy queue and complete them on fences.
- [ ] 1.9 Key `CaptureSequence`, `--screenshot`, `--diag` and the run manifest (`frames.preload`) on `CaptureReady`.

## 2. Testing
- [ ] 2.1 `scripts/rendering/test_preload_determinism.py --gate` passes with conditions A, B and C agreeing (the delay condition no longer changes the capture).
- [ ] 2.2 Falcor parity gate (`scripts/falcor/falcor_reference_test.py`) unchanged at PSNR >= 35 dB on the existing references.
- [ ] 2.3 First presented Sponza frame compared with the current build (`--pass-dump` and screenshot, PSNR reported) and reviewed visually.
- [ ] 2.4 Pre-load wall time and `frames.preload` recorded before and after on Sponza, Debug and Release, with the GPU time per iteration from the run manifest.
- [ ] 2.5 Unit tests for the readiness state machine: transitions, a failed asset, a failed SDF bake and device removal all reach a terminal state and never report `CaptureReady` early.
- [ ] 2.6 Unit tests for the reveal reset: `TemporalHistoryState` reports a reset on the first revealed frame and the jitter index restarts at 1 regardless of the loading iteration count.
- [ ] 2.7 Unit tests for the copy-queue SDF upload path with `MockRHIDevice`: staging ownership until the fence passes, no `WaitIdle` in the service, correct volume bytes.
- [ ] 2.8 Window-responsiveness check: the loading loop pumps messages at least every 16 ms under a 500 ms simulated GPU stall (diagnostic CVar), and closing the window during load exits cleanly.
- [ ] 2.9 Verify 95%+ coverage on all new code.

## 3. Documentation
- [ ] 3.1 Update `docs/analysis/rendering-arch/flicker/capture-preload-determinism.md` with the reveal-rebase design, the retired wait and the new gate results.
- [ ] 3.2 Update the runtime startup and CLI documentation under `docs/engine/` with the readiness states, the loading overlay and the affected flags (`--frames`, `--screenshot-preload`, `--run-manifest`).
- [ ] 3.3 Add `docs/analysis/scene-load-readiness/` with the Sponza measurements (Debug and Release), the per-pass GPU costs and the Codex/Claude design review.

## 4. Tail (docs + tests — check or waive with tailWaiver)
- [ ] 4.1 Update or create documentation covering the implementation
- [ ] 4.2 Write tests covering the new behavior
- [ ] 4.3 Run tests and confirm they pass
