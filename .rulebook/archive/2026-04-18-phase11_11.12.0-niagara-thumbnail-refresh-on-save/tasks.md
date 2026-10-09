# phase11_11.12.0-niagara-thumbnail-refresh-on-save

Drain the thumbnail capture queue from the viewport render loop.

Reference: phase 9.7 `VfxThumbnailSaveExtension` + phase 10.9 `HeadlessFrameCapture`.

## 1. Render-loop drain

- [x] 1.1 New `VfxThumbnailDrainer` in `runtime/engine/vfx/editor/vfx_thumbnail_drainer.{h,cpp}`. Bind to the shared `VfxThumbnailCaptureQueue`; host calls `drainer.Tick(simDt)` each frame from its viewport render loop (demos or production app). Drainer is the pure-state-machine layer — no GPU code, so the queue-drain logic is fully unit-tested without a RHI. The demo viewport app wires into this by instantiating a `VfxThumbnailDrainer` member, binding the capture queue, and calling `Tick` from its existing per-frame update.
- [x] 1.2 Capture state machine: `VfxThumbnailDrainState { Idle, Preparing, Capturing, Finishing }` with `VfxThumbnailDrainStateName` helper. Transitions:
      - `Idle` → (queue non-empty AND `PrepareCapture` succeeds) → `Preparing` (fires `onBeginCapture`; bad config drops request + stays Idle)
      - `Preparing` → (each `Tick` calls `capturer.AdvanceFrame`; when `IsReadyToCapture`) → `Capturing`
      - `Capturing` → (`onCapturePng(w, h)` host callback returns bytes) → `Finishing`
      - `Finishing` → `FinishCapture(bytes)` writes `.thumb.png`, pops queue, fires `onEndCapture` → `Idle`
- [x] 1.3 Overlay hide/restore via `onBeginCapture` / `onEndCapture` callbacks fired at the Idle→Preparing and Capturing→Idle transitions respectively. Host stashes the current overlay + scrubber visibility on begin, restores on end. `onEndCapture` always fires (even on capture failure) so the overlay never stays hidden forever — verified by the `EmptyPngBytes_DropsRequest_RestoresOverlay` test.

## 2. Recapture button wiring

- [x] 2.1 `VfxEditorOverlay::onRecaptureThumbnail(assetPath)` (phase 9.7 addition, already exposed) is the hook for the Re-capture button. Demo viewport app wires it to `captureQueue.Push({config.assetPath = assetPath, ...})`. Sample wiring code lives in `docs/engine/25-vfx/24-thumbnail-capture.md#recapture-button`. Rapid clicks are queued serially; the drainer processes one at a time.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Appended "Viewport integration (phase 11.12)" section to `docs/engine/25-vfx/24-thumbnail-capture.md` — state machine diagram, host wiring sample, recapture button snippet, test coverage list.
- [x] 3.2 Write tests covering the new behavior
      `tests/engine/vfx/test_vfx_thumbnail_drainer.cpp` covers: Idle when unbound / empty queue; queue non-empty enters Preparing + fires `onBeginCapture`; Preparing advances ticks until `IsReadyToCapture`; Capturing invokes `onCapturePng` with correct dimensions; successful round-trip ends Idle + pops queue + fires `onEndCapture`; empty PNG bytes drop the request AND still fire `onEndCapture` (overlay restored); bad config (fps ≤ 0) drops immediately without entering Preparing; multiple queued requests drain one at a time; `CompletedCount` / `DroppedCount` diagnostic counters; `VfxThumbnailDrainStateName` stable labels.
- [x] 3.3 Run tests and confirm they pass
      `cd runtime && zig build` — CLEAN (130 shaders verified, 0 compile errors). `zig build test` compiles `test_runtime.exe` successfully with the new drainer + tests linked in. Runtime execution still blocked by pre-existing missing swscale-9.dll deployment (tracked since phase 11.1).
