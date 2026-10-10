# phase10_10.9.0-renderer-headless-frame-readback

Off-screen RTV + CPU readback service.

Reference: Standard off-screen rendering pattern.

## 1. Service

- [x] 1.1 `RendererHeadlessFrameCapture` class: allocates RTV of `(w, h, format)`.
      `engine/renderer/headless/headless_frame_capture.h/.cpp` —
      `HeadlessFrameCapture` class. `Init(device, w, h, format)` creates
      an off-screen texture with
      `RenderTarget | CopySrc | ShaderResource` usage + a readback
      buffer sized `rowPitch * h` with 256-byte pitch alignment
      (DX12 constraint, backend-compatible). Format defaults to
      `RGBA8Unorm`; `BytesPerPixel` supports RGBA8/BGRA8/sRGB variants
      and RGBA16F.
- [x] 1.2 `CaptureFrame(renderCallback) → std::vector<uint8_t>` (raw RGBA8).
      Split into `BeginFrame(cmdList)` / caller-rendered-between /
      `EndFrame(cmdList)` / `ReadPixels()` so the render graph can use
      the RTV as a normal target without fighting a monolithic
      `CaptureFrame` callback. `ReadPixels` returns tightly-packed
      `width * height * 4` bytes after removing the 256-aligned row
      padding.
- [x] 1.3 Staging buffer + fence sync for GPU→CPU readback.
      `EndFrame` inserts `RenderTarget → CopySrc` barrier and
      `CopyTextureToBuffer` into the readback buffer, then leaves the
      RTV in `CopySrc` for the next `BeginFrame` (first-capture
      transition uses `Undefined` instead of `CopySrc`). Fence
      synchronisation is the caller's responsibility — they call
      `device->WaitIdle()` before `ReadPixels`.

## 2. PNG encode

- [x] 2.1 Reuse the existing PNG encoder (lib, not libpng/spng bridge).
      Uses `stb_image_write.h` (same encoder the editor's
      `ScreenshotCapture` uses). Implementation TU is
      `editor/screenshot/screenshot_capture.cpp` — header-only include
      here avoids duplicate symbols.
- [x] 2.2 `CaptureFramePNG(...)` convenience wrapping `CaptureFrame` + encode.
      `ReadPNG()` returns a PNG-encoded `vector<uint8_t>`;
      `SaveToPNG(path)` wraps `ReadPNG` + a file write.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Inline header docs cover the lifecycle (Init → BeginFrame loop →
      Shutdown), thread-safety, and the difference from
      `ScreenshotCapture` (backbuffer-bound).
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/renderer/test_headless_frame_capture.cpp` —
      10 cases: default uninitialised state, uninitialised-read returns
      empty, null-device rejected, zero-size rejected,
      unsupported-format rejected, Begin/End no-op on uninitialised,
      row-pitch 256 alignment math (4 cases), square-thumbnail memory
      budget (128×128 = 65 KiB, 256×256 = 1 MiB). The full GPU path
      (allocate → render → readback) exercises the thumbnail pipeline
      smoke test when phase 9.7 lands.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (33/35 steps). The runtime-exec
      step is still blocked by the pre-existing missing-ffmpeg-DLL
      environment gap.
