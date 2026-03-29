# Standalone vs N-API Rendering Diff — Root Cause Analysis

## Executive Summary

The N-API path renders correctly (SceneRenderer runs, draw list is built) but reports `drawCalls=0` because the `uz_frame_stats` struct never has that field populated — it is **hardcoded zero** in `runtime.cpp`. This is a **reporting gap**, not a black-frame GPU problem. The 20 KB H.264 frames being produced confirm the renderer is active. However, there are five additional correctness gaps that may or may not prevent scene geometry from appearing depending on path details.

## Root Causes Ranked by Impact

| # | Severity | Root Cause | File:Line | Effect |
|---|----------|-----------|-----------|--------|
| 1 | High (display) | `draw_calls` field never populated in `uz_render_frame` | `runtime.cpp:703` | JS always sees 0 draw calls — misleads debugging |
| 2 | Medium (ordering) | `setContentRoot` called after `createViewport` — VFS constructed with cwd during Init | `runtime.cpp:385-392`, `viewport-panel.ts:161-302` | Init VFS is wrong; corrected before scene load but fragile |
| 3 | Medium (first frames) | No pre-load texture drain before render loop starts | `uzengine.cpp:345-366` (absent in runtime.cpp) | First N frames use fallback textures |
| 4 | Low (latent race) | `uz_load_scene` (IPC thread) and `uz_render_frame` (render thread) share `world` with no mutex | `runtime.cpp:711-753`, `render_thread.cpp:155` | Safe in current call order but breaks on scene reload mid-loop |
| 5 | Low (diagnostic) | `s_firstSync` is process-lifetime static, fires when world is empty | `scene_sync.cpp:38` | First-frame entity count log is always "0" in N-API path |

## Gap 1 (Confirmed Bug): `draw_calls` is Always Zero in N-API Stats

**File**: `runtime/engine/app/runtime.cpp:698-706`

The `uz_frame_stats.draw_calls`, `.vertices`, and `.triangles` fields are never assigned. The standalone's "9 draw calls" comes from the **StatsOverlay on-screen text** (GPU-side rendered number inside the framebuffer), not from these struct fields.

## Gap 2 (Ordering Bug): `setContentRoot` Called After `createViewport`

N-API call sequence:
```
createViewport(0, 1280, 720)          ← VFS built with CWD (WRONG)
setEditorMode(true)
setContentRoot("e:\\...\\content")    ← VFS rebuilt (correct but too late for Init)
loadScene("e:\\...\\pbr_showcase.uzscene")
startRenderLoop()
```

Fix: Call `setContentRoot` BEFORE `createViewport`.

## Gap 3 (Missing Pre-Load): No `DrainPendingTextureLoads`

The standalone renders one "trigger frame" then calls `DrainPendingTextureLoads` to flush all async texture uploads. The N-API path has no equivalent.

## Gap 4 (Thread Safety): World Access Race

`uz_load_scene` (IPC thread) and `uz_render_frame` (render thread) share `state->world` with no mutex.

## Gap 5 (Diagnostic): `s_firstSync` Fires When World is Empty

The diagnostic log fires on the first `SyncWorldToDrawList` call, which happens before `loadScene` in the N-API path (render thread starts before scene is loaded in some paths).

## Why Frames Are 20 KB H.264

20 KB NAL units for 1493x625 at 20 Mbps confirms **real scene content is being encoded**. A black frame would be <2 KB. The renderer IS drawing — the stats just don't report it.

## Recommended Fixes

### Fix 1: Call `setContentRoot` before `createViewport` in scene-editor-provider.ts

### Fix 2: Wire `draw_calls` in `uz_render_frame`

### Fix 3: Add `uz_drain_pending_textures()` C API for the N-API path

### Fix 4: Guard `World` access with mutex for reload safety
