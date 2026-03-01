# UzEngine Documentation

Welcome to the UzEngine documentation! This project is a proof-of-concept (PoC) of a 3D engine coupled to VSCode via JPEG streaming.

## Quick Start

1. **Read first:** [Architecture Overview](architecture.md) — Understand the system design
2. **Build:** [Build Guide](build.md) — Set up and compile the project
3. **Use:** [VSCode Extension Reference](vscode-extension.md) — Run the extension and view the stream
4. **Extend:** [IPC and MCP Guide](ipc-mcp.md) — Control the engine via IPC or AI agents
5. **Deep dive:** [Streaming Pipeline](streaming-pipeline.md) — Frame lifecycle and implementation details

---

## Documentation Structure

### Core Concepts

- **[Architecture](architecture.md)** — System overview, threading model, component responsibilities
  - DX12 renderer with GPU readback
  - Ring buffer for frame passing
  - JPEG encoder (libjpeg-turbo)
  - WebSocket streaming
  - VSCode extension webview

### Implementation Guides

- **[Streaming Pipeline](streaming-pipeline.md)** — Frame lifecycle from GPU render to VSCode display
  - Frame readback and RGBA pixel capture
  - JPEG encoding parameters and performance
  - WebSocket binary protocol
  - Client-side base64 data URLs and browser rendering

- **[IPC and MCP](ipc-mcp.md)** — Control and automation interfaces
  - File-based IPC (JSON polling)
  - Registered handler functions
  - MCP server and 5 primitive tools
  - Thread safety and synchronization
  - Usage examples for AI agents

### Building and Development

- **[Build Guide](build.md)** — Prerequisites, build steps, troubleshooting
  - vcpkg dependency management
  - CMake configuration
  - VSCode extension build (npm)
  - Environment variables
  - Common issues and solutions

- **[VSCode Extension Reference](vscode-extension.md)** — Extension architecture and usage
  - Commands and configuration
  - ServerManager process lifecycle
  - StreamPanel webview
  - WebSocket connection and JPEG display
  - Debugging

---

## Key Architecture

### High-Level Pipeline

```
DX12 GPU Render
    ↓
GPU Readback (RGBA pixels)
    ↓
Ring Buffer (8-slot, lock-free SPSC)
    ↓
JPEG Encode (libjpeg-turbo)
    ↓
WebSocket Broadcast (binary frames)
    ↓
VSCode Webview
    ↓
<img src="data:image/jpeg;base64,...">
```

### Technology Stack

| Component | Technology | Notes |
|-----------|-----------|-------|
| Render | DirectX 12 | GPU rendering |
| Frame capture | D3D12 Readback buffer | CPU-side RGBA pixels |
| Compression | libjpeg-turbo | Fast JPEG encoding |
| Networking | IXWebSocket | WebSocket server |
| Config format | JSON (nlohmann-json) | IPC commands/responses |
| VSCode UI | TypeScript + Webview | Stream display and controls |
| Control interface | File-based IPC | polling model |
| AI/Agent interface | MCP (Model Context Protocol) | Stable tool primitives |

---

## Project Structure

```
UzEngine/
├── src/
│   ├── main.cpp                 ← Entry point, render loop, IPC setup
│   ├── common/
│   │   ├── ring_buffer.h        ← Lock-free SPSC queue
│   │   ├── types.h              ← FrameData, constants
│   │   ├── ipc_registry.h/cpp   ← Handler registry
│   │   └── ipc_poller.h/cpp     ← JSON polling thread
│   ├── renderer/
│   │   ├── renderer.h/cpp       ← DX12 renderer, GPU readback
│   │   └── scene_params.h       ← Shared scene state
│   ├── encoder/
│   │   └── jpeg_encoder.h/cpp   ← libjpeg-turbo wrapper
│   └── network/
│       └── ws_server.h/cpp      ← IXWebSocket server
├── vscode-uzengine/
│   ├── src/
│   │   ├── extension.ts         ← VSCode entry point
│   │   ├── server-manager.ts    ← Process lifecycle
│   │   └── stream-panel.ts      ← Webview + JPEG display
│   ├── package.json
│   └── tsconfig.json
├── tools/
│   └── mcp-uzengine/
│       ├── index.js             ← MCP server
│       ├── ipc-client.js        ← File-based IPC client
│       └── engine-process.js    ← Process spawning
├── CMakeLists.txt
├── vcpkg.json                   ← Dependencies
└── docs/                        ← This documentation
    ├── README.md                ← You are here
    ├── architecture.md
    ├── streaming-pipeline.md
    ├── ipc-mcp.md
    ├── build.md
    └── vscode-extension.md
```

---

## Use Cases

### 1. Stream 3D Rendering to VSCode

Use the VSCode extension to view real-time DX12 rendering in the editor.

```bash
# Build and run
cmake --build build --config Release
code .
# Command: UzEngine: Open Stream Viewer
```

### 2. Control Scene from AI Agents

Use the MCP tool interface to interact with the engine from Claude Code.

```python
# Claude Code script
uz_call("scene.setBackground", {"r": 0.0, "g": 0.05, "b": 0.15})
uz_call("scene.setRotation", {"y": 1.5})
screenshot = uz_screenshot()  # Verify result
```

### 3. Automate Rendering Tasks

Write custom tools that poll engine status or trigger screenshots.

```bash
# Example: automated frame capture
node tools/mcp-uzengine/index.js uz_screenshot > frame.json
```

---

## Key Design Decisions

1. **JPEG over H.264/MSE** — Simpler pipeline, independent frames, MSE sandbox restrictions avoided
2. **libjpeg-turbo** — Fast, quality 85 gives ~40-80 KB/frame at 1280×720
3. **Data URL img tags** — Bypasses CSP restrictions, simple client-side code
4. **Ring buffer with drop-oldest** — Render thread never blocks; encoder skips frames when behind
5. **File-based IPC** — No complex RPC mechanisms; polling model works well for PoC
6. **Inlined webview script** — No webpack/bundler, tight CSP, single HTML file
7. **Lock-free data structures** — Minimal synchronization overhead between threads

---

## Performance Targets

| Metric | Target | Typical |
|--------|--------|---------|
| Render FPS | 60 | 60 |
| Display FPS | 30+ | 30-40 |
| Frame size | <100 KB | 40-80 KB (quality 85) |
| Bandwidth | ~2.4 Mbps | 1.2-2.4 Mbps at 30 fps |
| Latency | <50 ms | 15-30 ms |
| Encoding time | <10 ms | 5-15 ms |

---

## Supported Platforms

- **Windows 10 or later** (64-bit)
- Visual Studio 2022
- DirectX 12

**Not supported:**
- Linux (DX12 Windows-only, would require porting to Vulkan)
- macOS (no DX12 support)
- 32-bit builds

---

## Troubleshooting

### Engine doesn't start

1. Check build output: `.\build\Release\UzEngine.exe`
2. Verify turbojpeg.dll is present: `ls build\Release\turbojpeg.dll`
3. Check Windows Event Viewer for crash logs

### WebSocket connection fails

1. Verify port 9002 is open: `netstat -ano | findstr :9002`
2. Check firewall: localhost should not require firewall rule
3. Try manual connection: `wscat -c ws://localhost:9002` (if wscat installed)

### JPEG encoding slow

1. Check CPU usage (Task Manager)
2. Reduce quality setting in encoder
3. Verify ring buffer is not full (indicates encode is too slow)

### VSCode extension issues

1. Check "UzEngine Server" output channel
2. Enable debug logging: set `RUST_LOG=debug` (if applicable)
3. Reinstall extension: `code --uninstall-extension uzmigames.uzengine`

---

## Development

### Editing Code

All source files are in `src/`. C++ standards:
- C++20
- No exceptions for control flow
- RAII for resource management
- Atomic operations for thread-safe state
- Lock-free data structures where possible

### Running Tests

(No test suite yet; tests are tracked in Rulebook tasks)

### Contributing

1. Follow existing code style
2. Add IPC handlers in `main.cpp`
3. Update documentation
4. Test on Windows 10+ with Visual Studio 2022

---

## References

### External Documentation

- [DirectX 12 Guide](https://docs.microsoft.com/en-us/windows/win32/direct3d12/) — Microsoft
- [IXWebSocket](https://github.com/machinezone/IXWebSocket) — GitHub
- [libjpeg-turbo API](https://libjpeg-turbo.org/About/TurboJPEG) — Official
- [VSCode Extension API](https://code.visualstudio.com/api) — Microsoft
- [Model Context Protocol](https://modelcontextprotocol.io/) — Anthropic

### Related Projects

- This is a PoC; production streaming engines (OBS, FFmpeg) handle more complex scenarios
- For persistent 3D worlds, consider game engines (Unreal, Unity)
- For low-latency video streaming, investigate RIST or WebRTC

---

## License

(Add your license here if applicable)

---

## Questions?

Refer to the specific documentation files above:
- **"How do I build?"** → [Build Guide](build.md)
- **"How do I use the extension?"** → [VSCode Extension Reference](vscode-extension.md)
- **"How do I control the engine?"** → [IPC and MCP Guide](ipc-mcp.md)
- **"How does the streaming work?"** → [Streaming Pipeline](streaming-pipeline.md)
- **"What's the overall architecture?"** → [Architecture](architecture.md)
