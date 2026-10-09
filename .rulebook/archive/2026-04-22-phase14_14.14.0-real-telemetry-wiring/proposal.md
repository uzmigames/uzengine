# Proposal: phase14_14.14.0-real-telemetry-wiring

## Why

Status bar chips show hardcoded zeros for FPS / DC / VRAM. The
stats have no basis in actual runtime state. Wiring real FPS
turns the chips into a useful diagnostic surface — colour warns
on frame drops, particle count reflects the live emitter.

## What Changes

1. `VfxEditorOverlay::UpdateTelemetry(fps, drawCalls, vramUsedMb,
   vramTotalMb)` accessor lets the host push per-frame telemetry
   into the status bar without exposing the widget directly.
2. Demo's `OnUpdate(dt)` computes instantaneous FPS as `1.0f / dt`
   with a 0.1 s exponential smoothing filter to avoid flicker on
   frame hitches; calls `UpdateTelemetry(smoothedFps, 0, 0, 0)`.
3. DC + VRAM stay zero for now — their sources (renderer draw-call
   counter, D3D12MA VRAM query) are surfaced when those APIs
   expose public accessors in a future task.

## Impact

- Affected code:
  - `runtime/engine/vfx/editor/vfx_editor_overlay.{h,cpp}`.
  - `runtime/demos/vfx_viewport_app/vfx_viewport_app.{h,cpp}`.
- Breaking change: NO.
- User benefit: FPS chip reflects real frame rate and tints by the
  phase-14.6 threshold.
