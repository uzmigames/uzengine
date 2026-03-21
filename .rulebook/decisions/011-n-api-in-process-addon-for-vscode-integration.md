# 11. N-API In-Process Addon for VSCode Integration

**Status**: proposed
**Date**: 2026-03-15

## Context

UzEngine runs inside VSCode as an editor. Need to deliver rendered frames from DX12 to VSCode webview panels. Options: separate process with IPC, in-process addon, or native HWND embedding.

## Decision

C++ runtime compiled as .node N-API module loaded directly into VSCode/Electron process. DX12 renderer + H.264 hardware encoder inside the extension. WebCodecs for hardware-accelerated decode in webview. Validated at 120 FPS @ 854x480 with 50K instances. Must match Electron's Node ABI exactly. NAPI_DISABLE_CPP_EXCEPTIONS required — C++ exceptions crash Electron. Frame delivery: native HWND swapchain for editor viewport (Phase 5.1), H.264 streaming for webview panels (2.2ms encode + 3.3ms decode).

## Alternatives Considered

- Separate process with WebSocket streaming (higher latency)
- WebGPU in browser (limited features, no DX12)
- Native window embedding only (no webview flexibility)
- Shared memory IPC (complex, platform-specific)

## Consequences

Pros: Zero IPC latency, direct GPU access, 120 FPS validated, single process simplicity. Cons: Must match Electron ABI exactly (fragile), C++ exceptions forbidden, shares memory space with VSCode (crash risk). H.264 config: Baseline profile, low-latency, no B-frames, CBR 20Mbps, GOP=15.
