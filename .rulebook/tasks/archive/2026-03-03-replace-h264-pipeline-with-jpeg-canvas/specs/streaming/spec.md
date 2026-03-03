# Streaming Subsystem Specification

## Purpose

Define the requirements for the UzEngine real-time frame streaming subsystem that delivers
rendered frames from the C++ engine process to the VSCode webview viewer with minimal latency,
zero codec-negotiation complexity, and reliable startup behaviour in VSCode webview contexts.

---

## REMOVED

The following components are removed from the streaming subsystem specification.

### H.264 Hardware Encoder (NVENC)

The NVENC-based H.264 encoder (`nvenc_encoder`) is removed. Hardware codec availability is
device-dependent and adds an optional dependency that cannot be guaranteed in all developer
environments.

### H.264 Software Fallback Encoder (FFmpeg/x264)

The FFmpeg/x264 software encoder fallback (`ffmpeg_encoder`) is removed along with the encoder
factory (`encoder_factory`) that selected between hardware and software paths.

### Fragmented MP4 Muxer

The hand-written fMP4 muxer (`fmp4_muxer`) is removed. Fragmented MP4 requires a valid init
segment to be delivered before any decodable frames, creating a fragile two-phase startup sequence.

### MSE / SourceBuffer Viewer

The `<video>` element, `MediaSource` object, and all `SourceBuffer` management code in the VSCode
webview are removed. MSE autoplay is blocked by VSCode's Content Security Policy in hidden
webviews, causing a black screen on every panel open or reveal.

---

## ADDED

### JPEG Encoder (`src/encoder/jpeg_encoder.h/.cpp`)

#### Scenario: Encode a single RGBA frame to JPEG

Given a valid RGBA pixel buffer of dimensions W x H,
when `JpegEncoder::encode(rgba, W, H, quality, out)` is called,
then the output vector SHALL be populated with a valid JPEG blob of non-zero size,
and the function SHALL return true.

#### Scenario: Configurable quality parameter

Given a quality value Q in the range [1, 100],
when `encode()` is called with quality Q,
then the resulting JPEG blob size MUST decrease as Q decreases and increase as Q increases,
confirming that the quality parameter is forwarded to the libjpeg-turbo compressor.

#### Scenario: Default quality produces acceptable frame size

Given a 1280x720 RGBA frame encoded at quality 85,
when the JPEG blob is produced,
then its size MUST be less than 150 000 bytes (150 KB).

#### Scenario: Encoder handles successive frames without memory leaks

Given that `JpegEncoder::encode()` is called for 1000 consecutive frames,
when all frames have been encoded,
then process heap growth MUST be less than 1 MB above baseline,
confirming no per-frame allocation leaks in the libjpeg-turbo compress path.

#### Scenario: Encoder destructor releases libjpeg-turbo handle

Given a `JpegEncoder` instance constructed and then destroyed,
when the destructor runs,
then `tjDestroy()` MUST be called on the internal `tjhandle`,
confirmed by running under AddressSanitizer with zero leak reports.

---

### WebSocket Frame Broadcast

#### Scenario: Each broadcast message is one complete JPEG

Given that the encode thread calls `WsServer::broadcast(blob.data(), blob.size())`,
when the WebSocket client receives a binary message,
then the message payload SHALL be a self-contained, independently decodable JPEG image,
requiring no prior context or init segment from the server.

#### Scenario: No init segment is sent at connection time

Given a new WebSocket client connecting to the engine,
when the connection is established,
then the server MUST NOT send any init-segment or header message before the first frame,
confirming that `ws_server.setInitSegment()` is no longer called.

---

### Canvas Viewer (TypeScript webview)

#### Scenario: First frame renders immediately on WebSocket open

Given the VSCode stream panel is opened and the engine is running,
when the first binary WebSocket message arrives,
then `createImageBitmap()` MUST be called with a `Blob` of type `image/jpeg`,
and `ctx.drawImage()` MUST paint the resulting bitmap to the canvas,
with the canvas displaying a visible frame within 200 ms of connection.

#### Scenario: Canvas displays correctly after panel hide and reveal

Given the stream panel is hidden (moved to background tab),
when the panel is revealed again,
then the canvas MUST display the current stream frame within 500 ms of becoming visible,
with no black screen or manual user action required.

#### Scenario: Canvas displays correctly after panel close and reopen

Given the stream panel is closed and reopened,
when the WebSocket reconnects and the first frame arrives,
then the canvas MUST display the frame immediately,
confirming no residual MSE or SourceBuffer state interferes with rendering.

#### Scenario: FPS counter reflects live frame rate

Given the stream is running at engine-native frame rate,
when at least one second has elapsed since the stream started,
then the FPS overlay element MUST display a value between 1 and 120 (inclusive),
updated once per second using frame-arrival timestamps.

#### Scenario: FPS counter sustains target rate

Given a 1280x720 stream at quality 85 running on the target development machine,
when the stream has been running for 10 seconds,
then the FPS counter MUST display a value ≥ 55,
confirming the JPEG decode and canvas paint path is not the bottleneck.

#### Scenario: WebSocket binary message is decoded without copy

Given a binary WebSocket message received as `ArrayBuffer` or `Blob`,
when `createImageBitmap()` is called,
then the implementation MUST pass the data directly without first base64-encoding it,
keeping decode overhead minimal.

---

## MODIFIED

### Encode Thread (`src/main.cpp`)

#### Scenario: Encode thread uses JpegEncoder, not encoder factory

Given the engine is running,
when the readback buffer is populated with a new RGBA frame,
then the encode thread MUST call `JpegEncoder::encode()` with quality 85,
and MUST NOT reference any `IEncoder`, `EncoderFactory`, or `IFrameMuxer` symbols.

#### Scenario: Encode thread broadcasts raw JPEG without muxing

Given a JPEG blob produced by `JpegEncoder::encode()`,
when the encode thread forwards the frame to the network layer,
then it MUST call `ws_server.broadcast(blob.data(), blob.size())` directly,
with no intermediate mux step.

### Build System (`CMakeLists.txt`)

#### Scenario: libjpeg-turbo is resolved via vcpkg

Given `vcpkg.json` lists `libjpeg-turbo` as a dependency,
when CMake configures with the vcpkg toolchain,
then `find_package(libjpeg-turbo CONFIG REQUIRED)` MUST succeed,
and the target `libjpeg-turbo::turbojpeg-static` MUST be available for linking.

#### Scenario: Legacy encoder and muxer sources are excluded from build

Given the CMakeLists.txt has been updated,
when CMake generates the build system,
then the source files `encoder_factory.cpp`, `nvenc_encoder.cpp`, `ffmpeg_encoder.cpp`,
and `fmp4_muxer.cpp` MUST NOT appear in any compile target,
confirmed by `cmake --build` completing with no undefined-reference errors for those symbols.
