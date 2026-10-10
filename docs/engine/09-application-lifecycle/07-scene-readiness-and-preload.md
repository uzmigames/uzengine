# Scene Readiness, Preload and Exposure Seeding

> **Status**: Implemented 2026-09-08 (ported from the `test/windows-sponza-parity` branch, task `phase0_sponza-parity-05-loading-exposure-cli`)
> **Entry point**: `runtime/tools/uzengine.cpp` (`RunWindowed`) · **Renderer**: `SceneRenderer::IsSceneFullyLoaded`, `SceneRenderer::ResetExposureHistory` · **Post**: `AutoExposure::ResetHistory`

## Why

Sponza streams 138 meshes through a 4-meshes-per-frame budget and bakes one mesh SDF per mesh. Before this change:

- `IsSceneFullyLoaded()` only looked at the upload queues. Meshes the budget had skipped lived only in ECS components, so the check reported "loaded" while the ceiling was still streaming (SponzaTest evidence: `load complete` at 02:15:04, last mesh at 02:16:19; here: screenshot at 14:29:40, last mesh at 14:29:41).
- The preload presented a single frame, drained only the textures requested by the first meshes, and handed over to the main loop. Lighting computed through missing occluders was on screen for seconds.
- The screenshot path bypassed readiness after `--screenshot-frame + 120` frames, so headless captures could contain a partial scene.
- Auto exposure adapted to the partial scene, so the first complete frames were mis-exposed.

## Readiness accounting

`SceneRenderer::IsSceneFullyLoaded()` (`runtime/engine/renderer/scene/scene_mesh_assets.cpp`) returns `false` while any of these is pending, checked in this order before the previous queue checks:

| Counter | Where it changes | What it covers |
|---|---|---|
| `m_budgetDeferredMeshCount` | reset at the top of `SyncWorldToDrawList`; `++` at both budget early-outs (base mesh `continue`, LOD `break`) in `scene_sync.cpp` | meshes skipped by the per-frame load budget — invisible to every upload queue |
| `m_deferredMeshUploads` | `scene_mesh_assets.cpp` | batched GPU mesh uploads not yet flushed |
| `m_pendingSdfBakes` | `scene_mesh_assets.cpp` | mesh SDF bakes queued for the GPU baker (SDF GI needs them before the first lit frame) |
| `m_asyncPendingSlots` | `scene_texture_assets.cpp` | async texture decodes in flight |

`IsSceneFullyLoadedDebug()` logs the four counters at Debug level; the screenshot path calls it every 30 frames while it waits.

## Preload loop (no present)

```cpp
window.SetTitle("<title> - Loading scene...");
while (window.PumpMessages() && window.IsOpen()) {
    sceneRenderer.RenderFrame(cmd, swapChain, &world, &vfs, 0.016f, frameIndex); device->WaitIdle(); ++frameIndex;
    DrainPendingTextureLoads(cmd);                                               device->WaitIdle();
    if (sceneRenderer.IsSceneFullyLoaded()) {
        RenderFrame(dt = 0);                      // full-HDR frame for the exposure pass to read
        sceneRenderer.ResetExposureHistory(cmd);  // zero EV + exposure (UAV → CopyDst → UAV)
        RenderFrame(dt = 0);                      // exposure pass re-initialises from that HDR; readback catches up
        break;
    }
}
window.SetTitle("<title>");
```

Nothing is presented until the loop exits. On Sponza (default project) the log line is
`Pre-load complete: assets ready and exposure seeded after 37 frames; no partial scene presented`.

`AutoExposure::ResetHistory` copies `m_zeroBuffer` over the 8-byte exposure buffer (which now carries `CopyDst` usage). `exposure_average_cs.hlsl` already treats `prevExposure < 1e-10` as "initialise directly to the target" — no shader formula changed. Two GPU dependencies are covered: the exposure pass reads the *previous* HDR frame, and its result reaches the CPU through a one-frame readback.

### Frame counting after the change

- `frameIndex` keeps counting through the preload, so the main loop starts at the preload count (37 on Sponza).
- `--frames N` is relative: the loop stops at `preloadFrames + N`, i.e. N **presented** frames.
- `--screenshot-frame K` is **absolute** and the capture also waits for `IsSceneFullyLoaded()`. Any K below the preload count captures the first presented frame. The 120-frame timeout bypass is gone; readiness is exact, so the wait cannot hang.

## Staging buffer lifetime

`scene_texture_assets.cpp` no longer calls `ReleaseStaging()` on the previous batch right after `GraphicsWaitForCopy()` — that is a GPU-side queue wait, and the copy queue could still be reading the buffers. The batch's staging buffers go through `TakeStagingBuffers()` → `QueueBufferDestroy()` and are released when the frame fence retires them. This removed an intermittent page fault on `TextureStagingBuffer`.

## Verification (2026-09-08, this repo)

| Check | Result |
|---|---|
| `scripts/rendering/test_scene_readiness.ps1` (reads the production function) | 14/14 |
| `scripts/rendering/test_exposure_reset.ps1` (command order, barriers, copy bounds) | 4/4 |
| Order in the runtime log | last mesh 10:49:59.5 → `Pre-load complete` 10:50:01.1 → `Screenshot saved` 10:50:03.0; 138 meshes and 138 SDF bakes before the capture |
| First presented frame vs frame 120, Camera A, 1080p (luminance mean / share below 16) | 51.6 / 37.8 % → 51.7 / 37.0 %; SponzaTest reference exe 52.7 → 52.8 |
| `scripts/rendering/verify_sponza_loading.ps1` | startup / converged / camera-c: exit 0, empty stderr, exactly one `Pre-load complete` each |
| `scripts/rendering/compare_sponza_stationary.ps1 -Prefix sponza-loading-fixed` (first presented frame vs frame 640, max-quality cvars) | whole image 53.34 → 53.42 /255 (+0.15 %), mean abs RGB diff 3.65/255; sunlit floor −0.46 %, shaded floor +2.3 %, shaded column +20.9 % (GI still converging) |
| First-frame RHI errors (`CommandAllocator::Reset` / `Close` 0x80004005 seen on 2026-09-07) | absent in all runs |

### The fly-camera teleport

`sample/content/scripts/fly_camera.lua` used to start at a hard-coded pose (`0, -250, 130`, yaw 90°). Its first `on_update` moved the camera away from `Camera_A` **after** the exposure had been seeded for `Camera_A`, and the first frame after a teleport renders with the previous frame's shadow cascades — the ground-floor corridor came out unshadowed (43/255 mean with auto exposure off; 1.2/255 one frame later). Seeded exposure × unshadowed frame = a white flash that took ~40 frames to adapt away, and every "Camera A" capture taken with the `Controller` entity in the scene was really the script's pose. The script now initialises from the active camera (`uz.camera.get_position()` and the new `uz.camera.get_forward()`), so the `Controller` entity no longer changes the opening view. The one-frame cascade lag on a teleport is pre-existing and unrelated to loading.

## Related

- [`docs/analysis/rendering/sponza-loading-exposure-2026-09-07.md`](../../analysis/rendering/sponza-loading-exposure-2026-09-07.md) — original investigation and evidence (SponzaTest)
- [`docs/guides/build-and-deployment.md`](../../guides/build-and-deployment.md) — CLI flags, `--cvar` precedence, `--gpu-breadcrumbs`
- [`docs/engine/12-rendering/33-sdf-gi-final-gather.md`](../12-rendering/33-sdf-gi-final-gather.md) — the SDF bakes readiness now waits for
- [`05-game-instance.md`](./05-game-instance.md) — scene loading in the `Application` path (same trigger, no timeout)
