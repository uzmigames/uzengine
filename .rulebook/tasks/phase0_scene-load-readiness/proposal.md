# Proposal: Scene Load Readiness — replace the Present gate with explicit readiness states (Phase 0)

## Why

`runtime/tools/uzengine.cpp` (lines ~1102-1148) runs a pre-load loop that renders full
frames, waits for the device twice per iteration and presents nothing until
`SceneRenderer::IsSceneFullyLoaded()` (`scene_mesh_assets.cpp:332`) returns true. The gate
was added so that lighting computed through missing occluders is never shown and so the
capture tooling has a deterministic "fully loaded" point. Measured on Sponza (RTX 4090):

| | Debug | Release |
|---|---|---|
| Pre-load | 5.02 s | 2.59 s |
| of which invisible full frames | ~1.7 s | 1.05 s (18 x 36.5 ms GPU + 2 x WaitIdle) |
| of which main-thread SDF staging (138 cache hits) | 0.44 s | 0.44 s |
| Render loop, 120 frames | 3.67 s | 3.70 s |

The 18 invisible frames exist only because `mesh_sdf_bake_queue.cpp:51` caps SDF
completions at `kCompletionsPerPreloadFrame = 8`, tied to `r.GI.GDF.MaxRegions` so the Global
Distance Field recomposes regions instead of whole windows (`global_distance_field.cpp:376-391`).
Regional recomposes do not bump `sceneRevision` (`global_distance_field.cpp:655`), so SSGI
(`ssgi_renderer.cpp:373`) and the radiance cache (`frame_gi_passes.cpp:275-285`) keep history
accumulated through a partially loaded scene, and the first presented frame inherits it.

The pre-load frame count also leaks into the TAA jitter phase (`frame_setup.cpp:465`,
`jitterIdx = frameIndex % 8`) and into TAA history (ledger H-73,
`docs/analysis/rendering-arch/flicker/capture-preload-determinism.md`). That was patched with
`r.GI.MeshSdf.PreloadWait`, a condition-variable wait on every worker preparation
(`mesh_sdf_bake_queue.cpp:354-363`) so every process pre-loads in the same number of frames;
the wait costs 438 ms in Debug and hides the real cause. Whenever one pre-load frame is slow
(a busy GPU), the window shows "Not responding" because messages are pumped only between frames.

A design review with Codex (2026-10-22) agreed on the direction: keep readiness as a signal,
stop using it to block Present, and fix determinism at the reveal instead of at the loader.

## What Changes

### New Systems

1. **Readiness states** — `SceneLoadReadiness { ResourcesResident, SceneComplete, CaptureReady }`
   exposed by `SceneRenderer`. `ResourcesResident`: every requested asset is uploaded or failed
   and all copy fences have passed. `SceneComplete`: ResourcesResident plus all mesh SDFs
   resolved and one whole-window GDF composition recorded. `CaptureReady`: SceneComplete plus
   the reveal reset and the two exposure-seed frames. `IsSceneFullyLoaded()` remains as a query
   equal to SceneComplete.
2. **Loading service loop** — the runtime drives the initial load with
   `SceneRenderer::ServiceLoading(cmdList, vfs)`: integrates async meshes, drains textures,
   services SDF bakes and advances upload completion on fences, without recording a frame.
   The window presents an opaque loading overlay (existing text renderer) each iteration.
3. **Reveal reset** — a temporal frame counter that restarts at reveal (TAA jitter, TAA/SSGI/
   SSR/denoiser history via `TemporalHistoryState`, the per-pass `m_historyValid` flags of GTAO,
   clouds and env-MIS, and auto exposure). Capture tooling (`CaptureSequence`, `--screenshot`,
   `--diag`, run manifest) keys on `CaptureReady`.
4. **Streaming budget** — after reveal, main-thread integration is bounded by a millisecond
   budget (`r.LazyLoad.BudgetMs`) in addition to the existing item counts; the 8-per-frame SDF
   completion cap stays because it is what keeps GDF recomposes regional while a scene is visible.
5. **Worker-side SDF staging** — cache-hit volumes are LZ4-decompressed by the worker directly
   into staging buffers the main thread mapped in advance (no RHI call off the main thread);
   SDF uploads are recorded on the copy queue and completed on fences, removing `WaitIdle`
   from the service.

### Refactoring

- `uzengine.cpp` pre-load loop: replaced by the loading service loop and the reveal sequence;
  `--frames N` keeps meaning N frames after reveal; the run manifest keeps `frames.preload`.
- `MeshSdfBakeQueue::Service`: completion cap lifted while loading (not presenting); the
  `PreloadWait` branch is retired (CVar kept one release, default 0) once the determinism gate
  passes with the delay condition.
- `GlobalDistanceField`: composition suppressed while loading; the first composition at
  SceneComplete is whole-window and bumps `sceneRevision`, which resets SSGI and the radiance
  cache by the existing consumer contract.
- Texture finalize / deferred mesh flush: advance on explicit fence completion instead of
  "next FlushPendingLoads call".

## Impact

- Affected specs: `docs/analysis/rendering-arch/flicker/capture-preload-determinism.md`,
  `docs/engine/` runtime startup / CLI documentation, this task's `specs/scene-load-readiness/spec.md`
- Affected code: `runtime/tools/uzengine.cpp`, `runtime/tools/capture_sequence.h`,
  `runtime/engine/renderer/scene/scene_renderer.h`, `scene_mesh_assets.cpp`,
  `scene_texture_assets.cpp`, `scene_render_frame.cpp`, `temporal_history_state.h`,
  `runtime/engine/renderer/gi/mesh_sdf_bake_queue.{h,cpp}`, `global_distance_field.cpp`,
  `runtime/engine/renderer/frame/frame_gi_passes.cpp`, `frame_setup.cpp`,
  `runtime/engine/renderer/mesh/gpu_mesh.cpp`
- Breaking change: NO for CLI flags and capture tooling. Visible change: the first presented
  frames show a loading overlay instead of nothing; `frames.preload` in run manifests counts
  service iterations instead of rendered frames.
- User benefit: Sponza pre-load from 2.6 s to about 1.0 s in Release and from 5 s to about
  2 s in Debug; the window never stops responding during load; captures no longer depend on
  loader thread timing.
- Enables: streaming reveal (present the scene while it streams, UE/Godot style) as a later
  opt-in; mip-first texture residency; world streaming (scene chunks) on the same service loop.
