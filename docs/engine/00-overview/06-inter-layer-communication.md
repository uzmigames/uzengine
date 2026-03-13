# Architecture Overview — Inter-Layer Communication

> **Status**: Reference (Phase 2 design)
> **Source**: ARCHITECTURE.md §Inter-Layer Communication, §Layers

## Architecture

```
┌───────────────────────────────────────────────┐
│  VSCode Extension Process (Electron/Node.js)  │
│                                               │
│  ┌─────────────────┐    N-API (in-process)    │
│  │  Extension UI   │◄──────────────────────►  │
│  │  (TypeScript)   │    direct function calls │
│  └────────┬────────┘                          │
│           │              ┌──────────────────┐ │
│           │              │  Runtime Engine  │ │
│           │              │  (.node native)  │ │
│           │              │  C++ / DirectX   │ │
│           │              └──────────────────┘ │
│           │                                   │
│           │ MCP protocol                      │
│           ▼                                   │
│  ┌─────────────────┐                          │
│  │  MCP Server     │                          │
│  │  (Node.js)      │                          │
│  └─────────────────┘                          │
└───────────────────────────────────────────────┘
```

## Communication Patterns

| Path | Method | Latency | Use Case |
|------|--------|---------|----------|
| **Extension ↔ Runtime** | N-API direct calls | ~0ms | create entity, load scene, render commands |
| **Runtime → Extension** | N-API callbacks | ~0ms | frame ready, profiler data, log output |
| **Webview ↔ Extension** | `postMessage` / `onDidReceiveMessage` | ~1ms | editor panels (inspector, hierarchy) |
| **Webview ↔ Runtime** | Indirect (webview → extension → N-API) | ~1-2ms | property edits, scene queries |
| **Extension ↔ MCP** | MCP protocol (stdio/SSE) | ~5-50ms | LLM commands, tool invocations |
| **MCP ↔ Runtime** | Via extension host as intermediary | ~5-50ms | scene manipulation, screenshot capture |

## Layer Responsibilities

### Extension (TypeScript) — Phase 2

- Editor UI: scene hierarchy, inspector, asset browser, viewport host
- Project/scene management
- Webview host for 2D editor panels (NOT viewport)
- Direct N-API calls to runtime

### Runtime (.node / .exe) — Phase 1 (CURRENT)

- RHI abstraction + DX12/Vulkan/Metal backends
- Render pipeline (deferred PBR, post-processing)
- Physics (Jolt), ECS (EnTT), Audio, Input
- Creates native HWND for 3D viewport (no webview)
- Crash reporting (SEH + minidump)

### MCP Server (Node.js) — Phase 3

- Exposes MCP tools for engine manipulation
- Translates LLM commands into engine actions
- Access to project/scene state

## Frame Delivery

### Viewport (native HWND — zero overhead)

The runtime creates a borderless HWND for the 3D viewport with a direct DX12 swapchain. No readback, no encoding, no postMessage. The viewport renders at native refresh rate.

### Preview/Remote Panels (H.264 streaming)

For webview-based preview panels and remote/headless scenarios, frames are streamed via H.264:

| Step | Component | Detail |
|------|-----------|--------|
| Encode | C++ Media Foundation | HW-accelerated H.264, Baseline, CBR 20Mbps, low-latency |
| Transport | Node Buffer via postMessage | Same as JPEG/WebP approach |
| Decode | WebCodecs VideoDecoder | HW-accelerated, `avc1.42001e`, `optimizeForLatency: true` |

**Performance:** 120 FPS sustained at 854x480 (POC 0.2 benchmark).

## Why Not WebSocket / IPC

POC 0.1 used WebSocket with JPEG streaming. The runtime rendered at 244 FPS but the display showed ~60 FPS — serialization and frame encoding were hard bottlenecks. In-process N-API eliminates all of this for the primary viewport. H.264 streaming is only used for secondary panels where zero-copy isn't possible.
