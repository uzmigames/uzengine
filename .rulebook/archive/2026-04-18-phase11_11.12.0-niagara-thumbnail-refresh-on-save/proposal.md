# Proposal: phase11_11.12.0-niagara-thumbnail-refresh-on-save

## Why
Phase 9.7 shipped `VfxThumbnailSaveExtension` + capture queue but thumbnail generation is deferred to a render-loop pump that the viewport app doesn't run today. Niagara automatically refreshes the emitter's thumbnail after every save (so the asset browser tile updates). Without this wiring, browser tiles stay frozen at startup-time thumbnails, making old effects indistinguishable from edited ones.

## What Changes
Wire the `VfxThumbnailCaptureQueue` drain into `vfx_viewport_app`'s render loop (`OnUpdate`). Every frame, peek the queue; if a request is pending, allocate a 128×128 `HeadlessFrameCapture`, render the target emitter for 15 ticks using the capturer's deterministic seed, grab PNG bytes, hand to `VfxThumbnailCapturer::FinishCapture()` which writes the `.thumb.png`. Also register the "Re-capture" button callback on the overlay (already exposed via `onRecaptureThumbnail`) to push a fresh request manually.

## Impact
- Affected specs: docs/engine/25-vfx/24-thumbnail-capture.md (augmented)
- Affected code: runtime/demos/vfx_viewport_app/vfx_viewport_app.cpp (render-loop drain); VfxEditorOverlay wires the Re-capture callback
- Breaking change: NO (queue is additive)
- User benefit: asset browser tiles stay in sync with saves
- Estimated effort: S

## Source
UE5 asset-thumbnail-on-save flow + phase 9.7 capturer.
