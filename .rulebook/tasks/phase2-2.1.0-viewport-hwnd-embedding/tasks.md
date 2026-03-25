## Phase A: H.264 Encoder (`runtime/engine/ipc/`)

- [x] A.1 Create `h264_encoder.h/.cpp` — `H264Encoder` class with PIMPL, `Initialize(Config)`, `EncodeFrame`, `Shutdown`. Uses FFmpeg libavcodec (not Media Foundation).
- [x] A.2 FFmpeg setup: `avcodec_find_encoder_by_name`, `avcodec_alloc_context3`, `avcodec_open2`. Baseline profile, low-latency CBR, no B-frames.
- [x] A.3 HW encoder probe order: h264_nvenc (NVIDIA) → h264_amf (AMD) → h264_qsv (Intel) → h264_mf (Media Foundation) → libx264 (software fallback). First successful open wins.
- [x] A.4 RGBA→NV12 conversion via libswscale (`sws_getContext` + `sws_scale`, SWS_BILINEAR).
- [x] A.5 Triple-buffered readback: `FrameReadback` class with 3 readback buffers + 3 fences. GPU copies frame N while CPU reads frame N-2. Eliminates `WaitIdle()` stall. Fallback to single-buffer path if init fails. First 2 frames skip encoding while pipeline fills.
- [x] A.6 Dedicated encoder thread with work queue. Implemented SubmitFrameAsync()/GetEncodedFrame() with background std::thread, mutex+CV signaling, double-buffered staging/output, frame-drop on backpressure. Atomic forceKeyframe/targetBitrate for cross-thread safety. runtime.cpp triple-buffered path updated to async submit+get pattern.
- [x] A.7 `EncodeFrame(rgbaData, stride, &outData, &outSize)` — synchronous encode, returns NAL units in encoder-owned buffer.
- [x] A.8 `Shutdown()` — frees swscale, packet, frame, codec context in reverse order.
- [x] A.9 `RequestKeyframe()` — sets `forceKeyframe` flag, next frame gets `AV_PICTURE_TYPE_I` + `AV_FRAME_FLAG_KEY`.
- [x] A.10 PNG fallback when H.264 encoder unavailable. Extension render loop tries `encodeFrame()` first; if null, falls back to `captureScreenshot()` → PNG base64 → webview `pngFrame` message. WebView also has `usePngFallback` for environments without WebCodecs.
- [x] A.11 `SetBitrate(kbps)` — updates `codecCtx->bit_rate` + `rc_max_rate` dynamically.
- [x] A.12 Per-encoder low-latency options: NVENC p1+ull+zerolatency, AMF ultralowlatency+speed, QSV veryfast, MF cbr, x264 ultrafast+zerolatency.
- [x] A.13 `GetExtradata(&outData, &outSize)` — returns SPS/PPS from `codecCtx->extradata` for WebCodecs `configure()`.

## Phase A.0: FFmpeg Integration (prerequisite)

- [x] A.0.1 Add FFmpeg LGPL shared build download to `scripts/setup.ps1` (BtbN pre-built, keeps LICENSE).
- [x] A.0.2 Update `runtime/thirdparty/README.md` with FFmpeg entry (LGPL 2.1+, libraries used).
- [x] A.0.3 Add `ffmpeg/include` to `thirdparty_include_dirs` in `build.zig`.
- [x] A.0.4 Add `engine/ipc` to source dirs + `addCppDir` in `build.zig`.
- [x] A.0.5 Link `avcodec.lib`, `avutil.lib`, `swscale.lib` in all targets (runtime.lib, test, exe, dll).
- [x] A.0.6 Verify `setup.ps1` downloads and extracts FFmpeg successfully.
- [x] A.0.7 Verify `zig build` compiles and links H264Encoder with FFmpeg.

## Phase A.1: Crash Protection Infrastructure (prerequisite)

- [x] A.1.1 `PlatformWindow::SetLibraryMode(bool)` — no `PostQuitMessage`, scoped `PeekMessageW` to HWND.
- [x] A.1.2 Hidden HWND when `parent_hwnd == 0` — no standalone window, DX12 swapchain still works.
- [x] A.1.3 `CrashHandler_SetLibraryMode(bool)` — VEH instead of UEF, writes crash reports, propagates exception.
- [x] A.1.4 `CrashHandler_SetCrashDir(const char*)` — redirect crash output to engine dir (not VSCode exe dir).
- [x] A.1.5 N-API SEH wrappers: `uz_seh_render_frame`, `uz_seh_create_viewport`, `uz_seh_capture_screenshot`, `uz_seh_runtime_init`, `uz_seh_encode_frame`, `uz_seh_get_codec_extradata`.
- [x] A.1.6 `UzNapiCheckSeh()` — converts SEH exception code to `Napi::Error`.
- [x] A.1.7 Extension crash detection: consecutive error counting, stops render loop on SEH, shows VSCode notification.

## Phase B: Runtime C API + N-API Bridge

- [x] B.1 Add `uz_encode_frame(const uint8_t** out_data, uint32_t* out_size) -> int` to `runtime.h/.cpp`. Readback via `ScreenshotCapture::MapReadback`.
- [x] B.2 Wire `uz_encode_frame` to `H264Encoder::EncodeFrame` — synchronous, readback + encode + return NAL data.
- [x] B.3 Add `uz_request_keyframe()` to C API.
- [x] B.4 Add `uz_get_codec_extradata(const uint8_t** out_data, uint32_t* out_size)` to C API — returns SPS/PPS via `H264Encoder::GetExtradata()`.
- [x] B.5 N-API `encodeFrame(): Buffer | null` in `napi_viewport.cpp` with SEH wrapper.
- [x] B.6 N-API `requestKeyframe(): void`.
- [x] B.7 N-API `getCodecExtradata(): Buffer | null` with SEH wrapper.
- [x] B.8 Update `uzengine.d.ts` with `encodeFrame()`, `requestKeyframe()`, `getCodecExtradata()`, `setEncodeBitrate()`.
- [x] B.9 Rebuild NAPI addon (`cmake-js compile --runtime=electron --runtime-version=34.0.0`). Fixed Win32 `GetVersion`/`Shutdown` macro collisions with Napi prefix. FFmpeg DLLs deployed to extension/native/.
- [x] B.10 Add `uz_set_encode_bitrate(uint32_t kbps)` to C API + N-API `setEncodeBitrate(kbps)` binding.

## Phase C: WebView Canvas + WebCodecs Decoder

- [x] C.1 `SceneEditorProvider` webview: full-size `<canvas id="viewport">` with 2D context.
- [x] C.2 WebView JS: `VideoDecoder` init — `avc1.42001e`, HW-accelerated, `optimizeForLatency: true`, codec extradata as `description`. Dynamic `codedWidth/codedHeight` from canvas dimensions.
- [x] C.3 WebView JS: `receiveFrame(data)` — Annex-B NAL keyframe detection (NAL type 5 = IDR), `EncodedVideoChunk` (key/delta), decode.
- [x] C.4 WebView JS: `output` callback — `ctx.drawImage(frame, ...)`, `frame.close()`.
- [x] C.5 Extension render loop: 8ms interval (120 FPS target), `renderFrame()` + `encodeFrame()` → H.264 path, with PNG fallback via `captureScreenshot()` when encoder returns null.
- [x] C.6 Toolbar overlay: scene name, gizmo T/R/S buttons, Local/World space, stats display — all HTML on top of canvas.
- [x] C.7 CSP: `img-src data: blob:` for PNG fallback path.
- [x] C.8 PNG fallback: `typeof VideoDecoder === 'undefined'` → Image decode path + `pngFrame` message type.
- [x] C.9 Codec extradata flow: extension sends `codecExtradata` message on `ready` → webview calls `initDecoder(extradata)`.

## Phase D: Input Forwarding (already exists, verified)

- [x] D.1 Canvas mouse/keyboard/scroll capture on `<canvas>` element with `tabindex="0"` + `canvas.focus()` on mousedown.
- [x] D.2 Mouse coordinates relative to canvas via `e.offsetX`/`e.offsetY`.

## Phase E: Lifecycle & Error Recovery

- [x] E.1 Panel hidden → `stopRenderLoop()` (clearInterval).
- [x] E.2 Panel revealed → `addon.requestKeyframe()` + `startRenderLoop()` for clean decoder restart.
- [x] E.3 Panel resize → webview sends `resize` message → extension calls `resizeViewport(w,h)` → C++ reinits encoder → extension sends new `codecExtradata` + `requestKeyframe()`.
- [x] E.4 Panel disposed → `stopRenderLoop()`.
- [x] E.5 Decoder error recovery: webview posts `decoderError` message → extension calls `addon.requestKeyframe()`.
- [x] E.6 HW encoder unavailable detection: encoder probes 5 codecs, falls back to software. Extension falls back to PNG `captureScreenshot()`. WebView has PNG fallback for no-WebCodecs environments.
- [x] E.7 SEH crash protection: `uz_seh_encode_frame` catches native crashes → JS exception → render loop stops → VSCode notification.

## Phase F: FFmpeg DLL Deployment

- [x] F.1 `napi/CMakeLists.txt` — copy FFmpeg DLLs (avcodec-62.dll, avutil-60.dll, swscale-9.dll) alongside uzengine.node post-build.
- [x] F.2 `scripts/deploy-napi.ps1` — already copies all DLLs from napi build output to `extension/native/`.

## Testing

- [x] T.1 Unit test: H264Encoder init/encode/shutdown cycle (requires GPU + FFmpeg DLLs). Written 30 tests in `tests/engine/ipc/test_h264_encoder.cpp` covering DefaultConstruction, InitializeAndShutdown, DoubleInitFails, ShutdownWithoutInit, EncodeProducesOutput, RequestKeyframe, SetBitrate, GetExtradata, MoveConstruction, MoveAssignment, AsyncSubmitAndGet, and destructor cleanup. 18 FrameReadback tests in `tests/engine/ipc/test_frame_readback.cpp`. 11 C API tests added to `tests/engine/app/test_runtime_api.cpp`. All 54 non-GPU tests pass.
- [ ] T.2 Unit test: encoder fallback chain (mock unavailable codecs). Requires FFmpeg codec-unavailability mocking infrastructure not yet in place.
- [ ] T.3 Integration: Extension activates → open .uzscene → canvas shows rendered frame via H.264.
- [ ] T.4 Integration: resize panel → viewport resizes → encoder reinits → decoder reconfigures.
- [ ] T.5 Benchmark: measure encode latency (target <4ms), total latency (target <7ms).
- [ ] T.6 Benchmark: measure sustained FPS at 1280x720 (target 120 FPS).

## Documentation

- [x] Doc.1 Update `docs/engine/28-ipc-protocol/03-frame-streaming.md` — marked Phase 1 as DONE, noted FFmpeg libavcodec instead of MF, added implementation notes.
- [x] Doc.2 Update `docs/specs/2.0-napi-foundation.md` — added `encodeFrame`, `requestKeyframe`, `getCodecExtradata`, `setEncodeBitrate` to interface example.
- [x] Doc.3 Update `extension/README.md` — documented scene-editor-provider H.264 → WebCodecs → canvas pipeline with PNG fallback.
