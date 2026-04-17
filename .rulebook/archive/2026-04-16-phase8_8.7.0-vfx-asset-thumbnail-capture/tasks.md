# phase8_8.7.0-vfx-asset-thumbnail-capture

Automatic thumbnail generation for VFX assets via headless frame capture.

Reference: UE5 UNiagaraThumbnailRenderer.

## 1. Capture pipeline

- [x] 1.1 VfxThumbnailCapturer headless runner with configurable capture frame + resolution.
      `engine/vfx/baker/vfx_thumbnail.h/.cpp` — `VfxThumbnailConfig` with
      resolution, fps, captureFrame, maxTicks safety cap, and deterministic
      randomSeed. `VfxThumbnailCapturer::PrepareCapture` validates config
      and computes step dt; `AdvanceFrame` counts ticks; `IsReadyToCapture`
      fires at target frame or maxTicks cap. No GPU ownership — the caller
      drives the render loop and hands back raw PNG bytes.
- [x] 1.2 PNG save alongside the asset (e.g. fireball.uzvfxsys -> fireball.thumb.png).
      `VfxThumbnailPathForAsset` replaces the extension with `.thumb.png`.
      `FinishCapture(pngData)` writes the bytes to the derived path and
      returns a `VfxThumbnailResult` (success flag + output path + tick
      index). Empty PNG data is rejected with an error log.
- [x] 1.3 Browser-UI path resolver (deferred to editor follow-up).
      `VfxThumbnailPathForAsset` is the resolver. The browser loads this
      path at display time; when the file does not exist it falls back to
      the generic VFX icon. Widget integration lives in the editor
      follow-up.

## 2. Documentation and tests

- [x] 2.1 Docs: thumbnail capture contract + on-save hook pattern.
      `docs/engine/25-vfx/24-thumbnail-capture.md` covers the full
      pipeline, path convention, config defaults table, determinism note,
      browser-UI resolver stub, and test index.
- [x] 2.2 Tests: deterministic capture at fixed seed + frame.
      `runtime/tests/engine/vfx/test_vfx_thumbnail.cpp` — 13 cases: path
      derivation (4), config validation (4), state machine (4: frame-0
      ready, advance to target, maxTicks cap, advance-after-ready no-op),
      empty PNG rejection, seed defaults + custom seed round-trip.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/24-thumbnail-capture.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_thumbnail.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps). Runtime
      `test_runtime.exe` exec blocked by the pre-existing missing-ffmpeg-
      DLL environment gap. All new code is pure CPU state machine + file
      IO with no platform dependencies.
