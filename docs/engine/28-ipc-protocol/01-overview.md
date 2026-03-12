# IPC Protocol — N-API In-Process Addon

> **Status**: Planned (Phase 1–2)
> **Source**: `extension/ipc/`, `runtime/core/ipc/`
> **References**: Electron N-API, VSCode Extension API, WebCodecs (frame streaming), Media Foundation

## Purpose

The **Inter-Process Communication (IPC) Protocol** connects the VSCode extension (UI, editor panels) to the UzEngine runtime. Unlike traditional game engines with separate editor/game processes, UzEngine's runtime is an **N-API .node addon** embedded directly in the extension's Node.js process. This eliminates round-trip latency, enables shared memory, and simplifies asset hot-reload coordination.

The runtime creates its own HWND with a DX12 swapchain for 3D viewport rendering. 2D editor panels (inspector, hierarchy, asset browser) use frame streaming: the runtime encodes frame buffers as H.264 via Media Foundation, and the webview decodes with WebCodecs VideoDecoder at 120 FPS.

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  VSCode Extension (TypeScript)                                      │
│                                                                     │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐            │
│  │  Inspector   │  │  Hierarchy   │  │Asset Browser│  (HTML/CSS) │
│  │    Panel     │  │    Panel     │  │    Panel    │            │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘            │
│         │Frame Stream      │                │                    │
│         └─────────────────┬────────────────┘                    │
│                           │ H.264 video frames (120 FPS)          │
├───────────────────────────┼──────────────────────────────────────┤
│  N-API Bridge (C++ .node addon, Node.js process)                │
│                           │                                       │
│  ┌──────────────────────────────────────────────────────────────┐│
│  │  IPC Protocol Handler                                        ││
│  │  • Function call dispatch (sync/async)                       ││
│  │  • Typed array buffers (shared memory)                       ││
│  │  • Callback registration                                    ││
│  │  • Error propagation                                        ││
│  └──────────────────────────────────────────────────────────────┘│
├───────────────────────────────────────────────────────────────────┤
│  Runtime (.dll, running in Node.js address space)               │
│                                                                     │
│  ┌──────────────────┐      ┌──────────────────┐                 │
│  │  3D Viewport     │      │   Scene Manager  │                 │
│  │  (HWND + DX12    │      │                  │                 │
│  │  swapchain)      │      │  • Node hierarchy│                 │
│  │                  │      │  • Component     │                 │
│  │  Pitch-black bg  │      │    serialization │                 │
│  │  + render loop   │      │  • Hot-reload    │                 │
│  └──────────────────┘      └──────────────────┘                 │
│                                                                     │
│  ┌──────────────────┐      ┌──────────────────┐                 │
│  │   Asset System   │      │  FileWatcher     │                 │
│  │                  │      │                  │                 │
│  │  • VFS (.uz*)    │      │  • Disk monitor  │                 │
│  │  • Content-hash  │      │  • Hot-reload    │                 │
│  │    cache         │      │    coordination  │                 │
│  └──────────────────┘      └──────────────────┘                 │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

## Design Principles

- **In-process, not out-of-process** — N-API addon in Node.js, not separate executable. Latency < 1ms, shared memory, simpler IPC.
- **Async by default** — all I/O operations (scene loading, asset compilation, physics simulation) are async. UI never blocks.
- **Viewport is 3D, panels are 2D** — 3D viewport renders directly to HWND. 2D panels (inspector, hierarchy) use frame streaming over IPC.
- **Frame streaming via H.264** — Media Foundation encodes, WebCodecs decodes. 120 FPS at 854x480 requires hardware acceleration.
- **Hot-reload coordination** — FileWatcher on runtime side notifies extension, which triggers async recompilation.
- **Error propagation** — runtime exceptions become extension Promise rejections, displayed in UI.
- **Shared memory for bulk data** — typed arrays (Float32Array, Uint8Array) passed by reference, not copy.

## Core Message Types

### Function Calls (Async)

```typescript
// Extension → Runtime
interface IPCCall {
    id: number;              // Unique request ID
    method: string;          // "scene.load", "asset.compile", etc.
    params: any[];           // Arguments (scalars, strings, buffers)
}

// Runtime → Extension (response)
interface IPCResponse {
    id: number;
    result?: any;            // Success case
    error?: { code: string; message: string };  // Error case
}

// Usage (TypeScript)
const mesh = await ipc.call("asset.load_mesh", ["content/models/cube.gltf"]);
const result = await ipc.call("physics.raycast", [origin, direction, maxDist]);
```

### Callbacks (Persistent)

```typescript
// Register a callback once, invoked multiple times
interface IPCCallback {
    id: number;
    method: string;
    params: any[];
}

// Usage: listen for file changes on disk
ipc.on("file_changed", (path: string, reason: "modified" | "deleted") => {
    console.log(`File changed: ${path}`);
});

// Listen for viewport size changes
ipc.on("viewport_resized", (width: number, height: number) => {
    // UI responds, triggers re-layout
});
```

### Typed Arrays (Shared Memory)

```typescript
// Pass large buffers efficiently without copy
const meshData = new Float32Array(vertexCount * 3);  // vertex positions
const indexData = new Uint32Array(indexCount);       // triangle indices

const mesh = await ipc.call("mesh.create", [
    { name: "Cube" },
    meshData,  // shared memory reference
    indexData
]);
```

## N-API Implementation Details

### Critical Rules

1. **NAPI_DISABLE_CPP_EXCEPTIONS required** — Electron crashes with `NAPI_CPP_EXCEPTIONS` enabled (electron#22799). Fallback to C-style error codes.

2. **Include CMAKE_JS_SRC** — CMakeLists.txt must include `${CMAKE_JS_SRC}` for Electron delay-load hook:
   ```cmake
   add_library(uzengine_ipc SHARED ${PROJECT_SOURCES} ${CMAKE_JS_SRC})
   set_target_properties(uzengine_ipc PROPERTIES SUFFIX ".node")
   ```

3. **Version Compatibility** — VSCode 1.111.0 uses:
   - Electron 39.6.0
   - Node 22.22.0
   - N-API ABI 140

   Compile with: `--runtime=electron --runtime-version=39.6.0`

### Message Dispatcher

```cpp
// Simplified dispatcher pattern (C++ side)
napi_status DispatchCall(napi_env env, const std::string& method,
                         const std::vector<napi_value>& params,
                         napi_value* out_result) {
    // Parse method name: "scene.load" → namespace="scene", action="load"
    auto [ns, action] = ParseMethod(method);

    if (ns == "scene") {
        if (action == "load") {
            return HandleSceneLoad(env, params, out_result);
        } else if (action == "save") {
            return HandleSceneSave(env, params, out_result);
        }
    } else if (ns == "asset") {
        if (action == "compile") {
            return HandleAssetCompile(env, params, out_result);
        }
    }

    return napi_set_last_error(env, napi_generic_failure, "Unknown method");
}
```

## Frame Streaming (H.264 Video Codec)

### Motivation

POC 0.2 evolution:
- **Raw RGBA**: Copy every frame to shared buffer — 2 FPS (bottleneck: bandwidth)
- **JPEG (stb_image)**: Software encoding — 138 FPS (still bandwidth-limited)
- **WebP (libwebp)**: Software encoding — 24 FPS (slower codec)
- **H.264 Media Foundation**: Hardware encoding — **120 FPS** (real-time, minimal latency)

### Architecture

```
┌──────────────────────────────────────┐
│  C++ Runtime (H.264 Encoder)         │
│                                      │
│  Frame 1: Read GPU VRAM → MF Encoder│
│           ├─ Output: .h264 keyframe │
│           └─ Send to extension      │
│  Frame 2: Read GPU VRAM → MF Encoder│
│           ├─ Output: .h264 delta    │
│           └─ Send to extension      │
│  Frame N: ...                       │
└──────────────────────────────────────┘
          │
          │ H.264 elementary stream
          │ (keyframe every 15 frames)
          │
          ▼
┌──────────────────────────────────────┐
│  TypeScript (WebCodecs Decoder)      │
│                                      │
│  Recv .h264 stream                   │
│    ├─ Create VideoDecoder            │
│    ├─ Decode frame                   │
│    └─ Render to canvas               │
│                                      │
│  Perf: 120 FPS @ 854x480            │
└──────────────────────────────────────┘
```

### C++ Encoder Setup

```cpp
/// Initialize Media Foundation H.264 encoder for frame streaming.
/// Runs on a separate thread, triple-buffered (encode while GPU renders next frame).
class H264Encoder {
public:
    /// Initialize encoder with target bitrate and frame rate.
    /// Must be called with CoInitializeEx(COINIT_MULTITHREADED) first.
    void Initialize(uint32_t width, uint32_t height, uint32_t fps, uint32_t bitrate_kbps);

    /// Encode a GPU-side texture to H.264 elementary stream.
    /// Texture is read from GPU, given to MFEncoder, output passed to callback.
    /// Returns immediately; callback invoked on encoder thread when frame ready.
    void EncodeFrame(ID3D12Resource* colorBuffer,
                     std::function<void(const uint8_t* data, size_t size)> on_encoded);

    /// Shutdown encoder, flush pending frames.
    void Shutdown();

private:
    IMFTransform* m_encoder = nullptr;           // H.264 encoder transform
    IMFMediaType* m_output_type = nullptr;       // Output bitstream type
    // ... thread pool, output sample queue, etc.
};
```

### TypeScript Decoder

```typescript
/// Decode and render H.264 stream from runtime.
export class H264StreamDecoder {
    private decoder?: VideoDecoder;
    private canvas: HTMLCanvasElement;

    async initialize(width: number, height: number) {
        this.canvas.width = width;
        this.canvas.height = height;

        this.decoder = new VideoDecoder({
            output: (frame) => this.renderFrame(frame),
            error: (err) => console.error("Decode error:", err),
        });

        const config: VideoDecoderConfig = {
            codec: "avc1.42001e",  // Baseline profile, level 3.0
            codedWidth: width,
            codedHeight: height,
            hardwareAcceleration: "prefer-hardware",
        };

        await this.decoder.configure(config);
    }

    /// Receive H.264 frames from runtime IPC.
    async receiveFrame(data: Uint8Array) {
        if (!this.decoder) return;

        const chunk = new EncodedVideoChunk({
            type: this.isKeyframe(data) ? "key" : "delta",
            timestamp: performance.now() * 1000,
            data: data,
        });

        this.decoder.decode(chunk);
    }

    private renderFrame(frame: VideoFrame) {
        const ctx = this.canvas.getContext("2d")!;
        ctx.drawImage(frame, 0, 0);
        frame.close();
    }

    private isKeyframe(data: Uint8Array): boolean {
        // Check NAL unit header: first byte bits [4:6] indicate nal_unit_type
        // Type 5 = IDR (key frame), Type 1 = non-IDR
        return (data[0] & 0x1F) === 5;
    }
}
```

## Runtime Pool (Multi-Scene Editing)

POC 0.1 validated an important pattern: maintain a **pre-warmed pool of processes** to avoid cold-start latency when opening multiple scenes. However, in 0.2+, this becomes a **pre-warmed pool of runtime instances** (still in-process, but multiple World instances):

```typescript
/// Manage multiple scene instances in a single runtime.
class RuntimePool {
    private worlds: Map<string, WorldHandle> = new Map();

    /// Create or reuse a world for a scene.
    async openScene(scenePath: string): Promise<WorldHandle> {
        if (this.worlds.has(scenePath)) {
            return this.worlds.get(scenePath)!;
        }

        // Compile and load scene asynchronously
        const world = await ipc.call("world.create", [scenePath]);
        this.worlds.set(scenePath, world);
        return world;
    }

    /// Close a world and clean up resources.
    async closeScene(scenePath: string): Promise<void> {
        const world = this.worlds.get(scenePath);
        if (world) {
            await ipc.call("world.destroy", [world.id]);
            this.worlds.delete(scenePath);
        }
    }
}
```

## Hot-Reload Coordination

When a user edits a `.cpp`, `.lua`, `.uzshader`, or `.gltf` file:

```
1. Extension's FileWatcher detects change
2. Extension calls ipc.call("asset.hot_reload", [path])
3. Runtime validates + recompiles asset
4. Runtime updates VFS cache
5. All scenes using that asset are notified via ipc.on("asset_changed")
6. Extension UI refreshes (if asset is selected in inspector)
```

Example: user edits `shaders/pbr.hlsl`:

```cpp
// C++ side (runtime)
napi_value HandleAssetHotReload(napi_env env, const std::string& path) {
    try {
        // 1. Recompile shader
        auto shader = CompileShader(path, "DXC", "SM6.6");

        // 2. Update cache entry
        GetAssetCache()->Set(path, shader.bytecode);

        // 3. Notify all scene managers
        for (auto& [scene_id, scene_mgr] : g_active_scenes) {
            scene_mgr->InvalidateShaderCache(path);
        }

        // 4. Call IPC callbacks
        return napi_invoke_callback(env, "asset_changed", {
            { "path", path },
            { "type", "shader" }
        });
    } catch (const std::exception& e) {
        return napi_set_error(env, e.what());
    }
}
```

## Error Propagation

Runtime exceptions become JavaScript Promise rejections:

```typescript
// TypeScript — errors surface naturally
try {
    const mesh = await ipc.call("asset.load_mesh", ["bad/path.gltf"]);
} catch (err) {
    if (err.code === "ASSET_NOT_FOUND") {
        showStatusBar("Mesh not found: bad/path.gltf");
    } else if (err.code === "SHADER_COMPILE_ERROR") {
        showDiagnostic(err.message);  // Shows in editor
    }
}
```

```cpp
// C++ side — throw exceptions, they auto-propagate
napi_value HandleAssetLoad(napi_env env, const std::string& path) {
    try {
        if (!FileExists(path)) {
            throw std::runtime_error(
                fmt::format("Asset not found: '{}'", path));
        }

        auto data = LoadAssetFromDisk(path);
        return napi_serialize_to_typed_array(env, data);
    } catch (const std::exception& e) {
        return napi_set_last_error(env, e.what());
    }
}
```

## Comparison

| Feature | UE5 | Godot | Unity | **UzEngine** |
|---------|-----|-------|-------|-------------|
| Architecture | Separate editor/game processes | In-process (editor + game in one VM) | In-process (editor + game in one VM) | **In-process N-API addon** |
| IPC | Windows named pipes, TCP sockets | Direct function calls (same process) | Direct function calls (same process) | **N-API messages + shared memory** |
| 3D viewport | Dedicated RHI rendering thread | Direct D3D12/Vulkan rendering | Direct rendering | **HWND with DX12 swapchain** |
| 2D panels | Custom slate/UMG rendering | HTML/CSS in Godot UI | uGUI in editor | **Frame streaming (H.264)** |
| Hot-reload latency | 500ms–2s (restart editor systems) | Instant (same process) | 1–2s (domain reload) | **< 100ms (asset + shader hot-reload)** |
| Viewport performance | 120 FPS (native) | 120 FPS (native) | 120 FPS (native) | **120 FPS (H.264 encoded)** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 1** | Basic N-API wrapper (function calls, error propagation), HWND + DX12 viewport, raw frame buffer streaming (2 FPS baseline) |
| **Phase 1.5** | H.264 Media Foundation encoder, WebCodecs decoder, 120 FPS frame streaming |
| **Phase 2** | Multi-world runtime pool, hot-reload coordination, callback registration, typed array zero-copy |
| **Phase 2.5** | Frame streaming optimization (adaptive bitrate, selective region encoding), viewport drag-to-pan, zoom |
| **Phase 3** | Full IPC protocol spec (all runtime subsystems exposed), diagnostic logging, performance profiling |
| **Phase 4** | Networked frame streaming (opus audio + VP9 video for remote editing), breakpoint debugging via DAP |
