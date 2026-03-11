# Overview

[UzEngine](https://github.com/uzmigames/uzengine) is a AAA-quality game engine designed
for autonomous AI-assisted development, embedded inside Visual Studio Code as a native extension.

## Architecture

UzEngine is structured as three cooperating layers:

```
┌─────────────────────────────────────────────────┐
│  VSCode Extension (TypeScript)                  │
│  Editor panels, asset browser, scene hierarchy  │
├─────────────────────────────────────────────────┤
│  Runtime (C++ / N-API .node addon)              │
│  RHI, renderer, physics, audio, ECS, scripting  │
├─────────────────────────────────────────────────┤
│  MCP Server (Node.js)                           │
│  AI tool integration, content generation        │
└─────────────────────────────────────────────────┘
```

**Extension** — The TypeScript layer handles the editor UI. Webview panels provide
inspectors, scene hierarchy, and asset browsers. The extension loads the runtime as a
native `.node` addon via N-API, keeping everything in a single process.

**Runtime** — The C++ core. Handles all GPU work through its own HWND window and DX12
swapchain (no frame streaming to webview for 3D). Compiled with Zig's C++ toolchain
for consistent cross-platform builds.

**MCP Server** — Exposes engine capabilities as AI tools via the Model Context Protocol.
Enables LLMs to create scenes, modify materials, and run engine commands.

> **Phase 1 (current)** focuses on the Runtime layer — building the rendering foundation
> from RHI up through a basic deferred renderer.

## Design Goals

| Goal | Description |
|------|-------------|
| **AAA visual quality** | Physically-based rendering, SM6.6 bindless, compute-driven pipeline |
| **AI-native** | Every system exposes structured interfaces for autonomous AI manipulation |
| **Open formats** | JSON scenes, human-readable configs, no proprietary binary blobs during dev |
| **Observable** | Structured logging, Tracy profiling, GPU breadcrumbs, stat counters on every system |
| **Cross-platform** | DX12 primary, with Vulkan and Metal backends planned |

## Rendering Hardware Interface (RHI)

All rendering goes through an abstract RHI layer. The application never touches DX12,
Vulkan, or Metal APIs directly — only `IRHIDevice`, `IRHICommandList`, and typed handles.

Currently implemented:
- **DX12 SM6.7** — Full bindless via Agility SDK, D3D12MA memory management, DRED debugging
- **Shader model negotiation** — Runtime probes hardware and falls back: SM6.7 → SM6.6 → SM6.0 → SM5.1 → SM5.0

See [RHI Guide](../guides/rhi/overview.md) for details.

## Math Library

SIMD-accelerated math via DirectXMath, wrapped in ergonomic C++ types:

- `Vec2`, `Vec3`, `Vec4` — float vectors with SIMD dot/cross/normalize
- `Mat4` — column-major 4×4 matrix with transforms and decomposition
- `Quat` — quaternion with slerp, axis-angle, and vector rotation
- `Transform` — position + rotation + scale composition

DirectXMath compiles cross-platform (Linux included) via a sal.h compatibility stub.

See [Math Guide](../guides/math/overview.md) for details.

## What's Next

The [Building from Source](building.md) guide walks through prerequisites and your first build.
The [Your First Window](first-window.md) guide creates a running DX12 render loop.
