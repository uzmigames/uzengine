# Architecture Overview — Runtime Execution Model

> **Status**: Reference (core architectural decision — validated by POC 0.2)
> **Source**: ARCHITECTURE.md §Runtime Execution Model, §Multi-Scene Runtime Pool, §CLI Interface

## The Decision

> **The runtime is an N-API native addon (.node) loaded in-process by the VSCode extension host. It is NOT a separate process.**

This is the single most important architectural decision. Everything else follows from it.

## Why In-Process

| Aspect | In-Process N-API (.node) | Separate Process (REJECTED) |
|--------|--------------------------|------------------------------|
| **Frame delivery** | Shared GPU textures (viewport HWND) | WebSocket JPEG streaming (bottleneck) |
| **Function calls** | Direct C++ ↔ JS binding (~0 overhead) | IPC serialization (~1-5ms per call) |
| **Memory** | Shared address space, zero-copy | Separate heaps, must serialize everything |
| **Crash isolation** | Addon crash kills extension host (restarts in ~1s) | Process crash is isolated |
| **Debugging** | Attach debugger to extension host | Attach to separate process |
| **Lifecycle** | Loads on `require()`, unloads on deactivate | Must spawn, manage, heartbeat, kill |

**Why not separate process:** POC 0.1 proved that frame streaming is a hard bottleneck (244 FPS render → ~60 FPS display). The engine needs to render at display refresh rate with zero transfer overhead. In-process shared textures achieve this. The crash isolation tradeoff is acceptable — Electron restarts the extension host cleanly, and the runtime has SEH handlers + minidump generation.

**Viewport rendering (not a webview):** The runtime creates its own borderless HWND (child of the VSCode window or floating) for the 3D viewport. This gives direct DX12 swapchain presentation — no readback, no encoding, no postMessage.

## N-API Build Requirements

Critical findings from POC 0.2:

| Requirement | Why |
|-------------|-----|
| Include `${CMAKE_JS_SRC}` in CMakeLists | Delay-load hook for Electron — without it, `require()` segfaults |
| Use `NAPI_DISABLE_CPP_EXCEPTIONS` | `NAPI_CPP_EXCEPTIONS` crashes Electron (electron#22799) |
| Compile with `--runtime=electron --runtime-version=<ver>` | Must match Electron's Node ABI |

**Current target:**
- VSCode 1.111.0 = Electron 39.6.0 / Node 22.22.0 / ABI 140

## Multi-Scene Runtime Pool

### Problem

1. **Cold-start latency** — DX12 device + subsystem init takes seconds
2. **Multiple scenes** — user may open 3-4 scenes simultaneously
3. **Build conflicts** — recompiling while pool processes run causes EBUSY errors

### Architecture

```
┌─ Extension Host ──────────────────────────────────────────┐
│                                                           │
│  ┌─ Runtime Pool Manager ───────────────────────────────┐ │
│  │                                                      │ │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐            │ │
│  │  │ Runtime 0│  │ Runtime 1│  │ Runtime 2│  (warm)    │ │
│  │  │ scene: A │  │ scene: B │  │ (idle)   │            │ │
│  │  └──────────┘  └──────────┘  └──────────┘            │ │
│  │                                                      │ │
│  │  Pool config: min_warm=2, max_total=8                │ │
│  └──────────────────────────────────────────────────────┘ │
│                                                           │
│  User opens Scene C → Pool assigns Runtime 2 → loads      │
│  Pool spawns new Runtime 3 (pre-warm) to maintain min     │
└───────────────────────────────────────────────────────────┘
```

| Aspect | Decision |
|--------|----------|
| **Pre-warming** | Idle runtimes init DX12 device + core systems, load NO scene |
| **Scene assignment** | Extension requests runtime → pool assigns idle instance → sends scene path |
| **Pool sizing** | `min_warm` (default: 2), `max_total` (default: 8), configurable via `.uzproject` |
| **Graceful shutdown** | `--pool-shutdown` kills pool cleanly; build scripts call before compile |
| **Crash recovery** | Pool detects exit code → creates minidump → spawns replacement → notifies extension |

## CLI Interface

The runtime compiles as both a static library and a standalone executable:

```bash
# Interactive windowed mode
uzengine --scene "scenes/level1.uzscene"

# Headless screenshot (CI/LLM workflow)
uzengine --headless --scene "scenes/test.uzscene" --screenshot output.png --exit-after-frame 1

# Backend selection
uzengine --scene "scenes/test.uzscene" --rhi dx12          # default
uzengine --scene "scenes/test.uzscene" --rhi vulkan         # Vulkan
uzengine --scene "scenes/test.uzscene" --rhi metal          # Metal (macOS)

# Debug overlays
uzengine --scene "scenes/test.uzscene" --overlay wireframe
uzengine --scene "scenes/test.uzscene" --overlay normals --overlay depth

# Validation
uzengine --scene "scenes/test.uzscene" --gpu-validation --gpu-breadcrumbs
```

### Backend Selection Priority

```
CLI flag (--rhi dx12)                    ← highest
  ↓
Project config (.uzproject → "rhi")
  ↓
User config (~/.uzengine/config.json)
  ↓
Auto-detect (probe GPU capabilities)    ← lowest
```

## Visual Feedback Loop (LLM Integration)

The `--screenshot` mode enables autonomous development:

```
1. LLM writes/modifies shader or scene code
2. Build runtime
3. Run: uzengine --headless --screenshot frame.png --exit-after-frame 1
4. LLM reads frame.png with vision
5. LLM evaluates: "specular too bright, normal map inverted"
6. LLM fixes code → goto 2
```

Requirements: headless rendering, deterministic first frame, GPU readback → PNG, debug overlays (wireframe, normals, depth, GBuffer channels), non-zero exit codes on failure.
