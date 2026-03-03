## 1. Dependencies and Build System

- [x] 1.1 Add `libjpeg-turbo` to `vcpkg.json` manifest
- [x] 1.2 Update `CMakeLists.txt`: add `find_package(libjpeg-turbo CONFIG REQUIRED)` and link `libjpeg-turbo::turbojpeg`
- [x] 1.3 Remove H.264 encoder sources from `CMakeLists.txt` compile list (`encoder_factory`, `nvenc_encoder`, `ffmpeg_encoder`)
- [x] 1.4 Remove fMP4 muxer sources from `CMakeLists.txt` compile list (`fmp4_muxer`)
- [x] 1.5 vcpkg install + cmake --preset release confirms libjpeg-turbo found with no errors
- [x] 1.6 Add POST_BUILD step to copy `turbojpeg.dll` to output directory (x64-windows triplet is dynamic)

## 2. JPEG Encoder (C++)

- [x] 2.1 Create `src/encoder/jpeg_encoder.h` — `JpegEncoder` with `encode(rgba, w, h, quality, out)`
- [x] 2.2 Create `src/encoder/jpeg_encoder.cpp` — `tjInitCompress`, `tjCompress2`, `tjDestroy` in destructor
- [x] 2.3 Encoder compiles and links against `libjpeg-turbo::turbojpeg`

## 3. Remove Legacy Encoder and Muxer Files

- [x] 3.1 Delete `src/encoder/encoder.h`
- [x] 3.2 Delete `src/encoder/encoder_factory.h` and `src/encoder/encoder_factory.cpp`
- [x] 3.3 Delete `src/encoder/nvenc_encoder.h` and `src/encoder/nvenc_encoder.cpp`
- [x] 3.4 Delete `src/encoder/ffmpeg_encoder.h` and `src/encoder/ffmpeg_encoder.cpp`
- [x] 3.5 Delete `src/muxer/fmp4_muxer.h` and `src/muxer/fmp4_muxer.cpp`
- [x] 3.6 No remaining `#include` references to deleted headers

## 4. Simplify Encode Thread in main.cpp

- [x] 4.1 Remove encoder factory / `IEncoder` / `IFrameMuxer` lifecycle code
- [x] 4.2 Remove `ws_server.setInitSegment()` call
- [x] 4.3 Instantiate `JpegEncoder`; call `encode()` with quality 85 per frame
- [x] 4.4 Call `ws_server.broadcast(blob)` with raw JPEG bytes
- [x] 4.5 Encode thread compiles and builds successfully

## 5. TypeScript Canvas Viewer

- [x] 5.1 Remove `<video>`, `MediaSource`, and all `SourceBuffer` code
- [x] 5.2 Remove live-edge sync, stall detection, MSE error handling
- [x] 5.3 Add `<img id="stream">` to webview HTML (data URL approach)
- [x] 5.4 On message: convert ArrayBuffer to base64 `data:image/jpeg` and set `img.src`
- [x] 5.5 Client-side FPS counter via `Date.now()` frame timestamps
- [x] 5.6 `npm run compile` — zero TypeScript errors

## 6. Integration Testing

- [x] 6.1 `cmake --build` — no errors, `turbojpeg.dll` copied to output
- [x] 6.2 Canvas displays spinning triangle immediately on open
- [x] 6.3 No black screen on open (confirmed working)
- [x] 6.4 Stream running continuously at 60 fps
- [x] 6.5 FPS overlay shows 60 fps at 1280x720
- [x] 6.6 WebSocket message verified as valid JPEG (FF D8 FF E0)

## 7. Documentation

- [x] 7.1 Agent memory updated with architectural decision and gotchas
