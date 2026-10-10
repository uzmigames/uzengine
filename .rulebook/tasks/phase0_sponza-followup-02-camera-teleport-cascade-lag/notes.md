# Notes — phase0_sponza-followup-02-camera-teleport-cascade-lag

2026-09-08. Exe 12:40.

## Outcome: the premise does not reproduce — there is no cascade lag

The proposal was written from the task-05 white-flash investigation, which read the evidence as "the first frame after a teleport renders with the previous pose's shadow cascades". A deterministic repro says otherwise.

`scripts/sponza-teleport-camera.{uzproject,uzscene,lua}` holds `Camera_A` for 60 frames and then jumps in a single update, logging the absolute frame indices (`TeleportCameraQA started base_frame=37 jump_frame=97 after_jump_frame=98`). Auto exposure **off**, so the comparison is HDR-linear and a stale shadow map would show directly.

Jump into the ground-floor corridor (the pose `fly_camera.lua` used to teleport to):

| Frame | What | mean /255 | share < 16 |
|---|---|---|---|
| 96 | last hold frame, `Camera_A` | 42.8 | 3.9 % |
| **97** | **the jump frame** | **1.2** | **99.4 %** |
| 98 | one after | 1.2 | 99.4 % |
| 99 | two after | 1.2 | 99.4 % |
| 107 | settled | 1.2 | 99.4 % |

Jump within the atrium (`Camera_A` shifted 400 cm along X, so any stale cascade would show on lit geometry rather than hide in a dark corridor):

| Frame | mean /255 | share < 16 |
|---|---|---|
| 96 (before) | 68.2 | 0.7 % |
| **97 (jump)** | **4.5** | **94.7 %** |
| 98 / 99 / 107 | 4.5 | 94.7 % |

In both directions **the jump frame is already identical to the settled frames**. The shadow cache does invalidate correctly: `ShadowCacheManager::UpdateCache` fails `IsCacheValid` on the changed `cascadeCenter`, `ComputeScrollingData` returns an overlap below `scrollingOverlapThreshold`, and the cascade falls through to `StaticPrimitivesOnly` — a full static re-render on the same frame.

## What the task-05 observation actually was

The numbers line up exactly with the QA run: task 05 measured 43.0 on the first presented frame and 1.2 from the second on; the QA scene measures 42.8 for `Camera_A` and 1.2 for the corridor. So the first presented frame was **`Camera_A`'s view**, not an unshadowed corridor — the old `fly_camera.lua` set the camera in `on_update`, not `on_start`, so the scene camera survived one frame before the script's pose took over.

The flash was therefore entirely the exposure: the preload seeds exposure for `Camera_A` (a bright atrium), the next frame shows a corridor ~35× darker in HDR, and auto exposure needs ~40 frames to adapt. With auto exposure on, the same QA jump reproduces exactly that shape — 233.1 before the jump, 28.8 on the jump frame, then 29.5 / 30.4 / 33.7 / 38.4 at frames 100 / 110 / 130 / 160, climbing for the rest of the run.

Task 05's fix (the script now starts from the active camera via `uz.camera.get_position` / `get_forward`) removes the pose change entirely, so nothing to fix here.

**Correction to the task-05 notes:** their step 6 says the first frame was "the ground-floor corridor **without shadows**". That was a misreading of two similar views — Sponza's nave looks alike from either end, and the pose I compared against was itself the corridor. The measurements above supersede it; the conclusion of task 05 (start the script at the scene camera) is unaffected, since it removed the jump either way.

## Kept

- The QA scene stays as a regression guard for camera cuts: `--project scripts/sponza-teleport-camera.uzproject`, jump at `base_frame + 60`, absolute indices in the log.
- `uz.camera.get_forward()` (added in task 05) is what lets a controller script start from the scene camera instead of teleporting.

## Still open (unchanged by this task)

- With auto exposure on, a hard camera cut between areas of very different brightness takes tens of frames to adapt. `AutoExposure::ResetHistory` exists (task 05) and would make a cut instant if the engine called it on a large camera jump. Not done here: it is an exposure-policy decision (a cut in a cinematic may *want* the adaptation), not a defect.
