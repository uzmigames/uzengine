# N-API addon validated at 120 FPS — NAPI_DISABLE_CPP_EXCEPTIONS required
**Source**: manual
**Date**: 2026-03-15
**Tags**: vscode, napi, electron, performance, integration, h264
The VSCode integration via N-API in-process addon was validated at 120 FPS sustained @ 854x480 with 50K instances. Critical requirements: (1) Must match Electron's Node ABI exactly (--runtime=electron --runtime-version=X), (2) Must include ${CMAKE_JS_SRC} delay-load hook for Windows/Electron, (3) NAPI_DISABLE_CPP_EXCEPTIONS is REQUIRED — C++ exceptions crash Electron instantly. H.264 frame delivery: Baseline profile, low-latency mode, no B-frames, CBR 20Mbps, GOP=15. Encode 2.2ms + decode 3.3ms per frame.