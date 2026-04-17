# VFX thumbnail capture

Automatic thumbnail generation for VFX assets via headless frame capture.
The asset browser shows visual previews without requiring a full bake pass.

Reference: UE5 `UNiagaraThumbnailRenderer`.

## Pipeline

1. Editor save handler creates a `VfxThumbnailCapturer` with a
   `VfxThumbnailConfig` (resolution, target frame, fps, seed).
2. `PrepareCapture(config)` validates the config and computes the fixed
   step dt (`1 / fps`).
3. A headless loop calls `VFXSystem::Tick(capturer.GetStepDt())` then
   `capturer.AdvanceFrame()` until `capturer.IsReadyToCapture()`.
4. The renderer captures the current frame as raw PNG bytes.
5. `capturer.FinishCapture(pngData)` writes the thumbnail to the
   asset-derived path (`VfxThumbnailPathForAsset(assetPath)`).

## Path convention

```
vfx/fireball.uzvfxsys  →  vfx/fireball.thumb.png
effects/smoke.uzfx      →  effects/smoke.thumb.png
fire                    →  fire.thumb.png
```

The browser resolver simply appends `.thumb.png` after stripping the
original extension. The convention is stable across save/reload cycles.

## Config defaults

| Field         | Default | Notes                                     |
|---------------|---------|-------------------------------------------|
| `width`       | 128     | Pixel width of the thumbnail.              |
| `height`      | 128     | Pixel height.                              |
| `fps`         | 60      | Fixed step rate during capture.            |
| `captureFrame`| 15      | Which tick to snapshot ("peak" frame).     |
| `maxTicks`    | 300     | Safety cap against infinite loops.          |
| `randomSeed`  | 42      | Fixed seed for determinism; 0 = no override.|

## Determinism

When `randomSeed` is non-zero the VFX system is seeded before the capture
loop, making the thumbnail bit-for-bit reproducible across saves. Combined
with the fixed-dt step and the target `captureFrame`, two saves of the same
asset produce identical thumbnails regardless of when the save happened.

## Browser-UI resolver (deferred)

The editor's asset browser loads `VfxThumbnailPathForAsset(path)` at display
time. When the file does not exist (uncaptured or deleted), the browser falls
back to the generic VFX icon. The resolver is a simple string operation — no
VFS look-up at query time.

## Tests

`runtime/tests/engine/vfx/test_vfx_thumbnail.cpp` covers: path derivation
(standard extension, `.uzfx`, no extension, dots in directory), config
validation (valid, zero/negative fps, zero resolution), state machine
(capture-frame-0 immediately ready, advance to target, maxTicks cap,
advance-after-ready is no-op), empty PNG rejection, and seed round-trip.
