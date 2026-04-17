# phase11_11.12.0-niagara-thumbnail-refresh-on-save

Drain the thumbnail capture queue from the viewport render loop.

Reference: phase 9.7 `VfxThumbnailSaveExtension` + phase 10.9 `HeadlessFrameCapture`.

## 1. Render-loop drain

- [ ] 1.1 `VFXViewportApp::OnUpdate(dt)` polls the global
      `VfxThumbnailCaptureQueue` each frame. If a request is pending
      AND no capture is mid-flight, start one.
- [ ] 1.2 Capture state machine on the app:
      - `Idle` → (queue non-empty) → `Preparing` (alloc 128×128 RTV,
        deterministic seed, 0 ticks)
      - `Preparing` → (each frame advances capturer tick) → `Capturing`
        (when `IsReadyToCapture()` → render + readback)
      - `Capturing` → (PNG bytes available) → `Finishing`
        (call `FinishCapture(bytes, config)`, pop queue, `Idle`)
- [ ] 1.3 During capture, temporarily hide the editor overlay + timeline
      scrubber so only particles land in the thumbnail.

## 2. Recapture button wiring

- [ ] 2.1 `VfxEditorOverlay::onRecaptureThumbnail` callback set to push
      a fresh `VfxThumbnailCaptureRequest` with `config.assetPath =
      currentAsset` into the queue.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 3.1 Update or create documentation covering the implementation
      Append "Viewport integration" section to
      `docs/engine/25-vfx/24-thumbnail-capture.md`.
- [ ] 3.2 Write tests covering the new behavior
      Capture state machine transitions; queue empty/non-empty logic;
      overlay hide/restore around capture.
- [ ] 3.3 Run tests and confirm they pass
      `zig build` clean; QA: save asset, see `.thumb.png` appear on disk
      within ~250ms; asset browser tile updates next refresh.
