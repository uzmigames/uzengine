# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added
- **Engine IPC protocol** (`add-engine-ipc`): file-based IPC via `build/Release/preview/`
  - `src/common/ipc_registry.h/cpp` — function dispatch table (`string → handler`)
  - `src/common/ipc_poller.h/cpp` — dedicated thread polling `command.json` every 50ms, atomic writes via `.tmp + rename`, `status.json` updated every 1s
  - `src/renderer/scene_params.h` — thread-safe scene state (`std::atomic` for rotation/pause, `std::mutex` for background color)
  - 12 registered handlers: `engine.ping`, `engine.getStatus`, `engine.screenshot`, `scene.setRotation`, `scene.setBackground`, `scene.setGeometry`, `scene.setPaused`, `scene.setParam`, `file.read`, `file.write`, `file.list`, `shader.reload` (stub)
  - `external/stb/stb_image_write.h` + `src/common/stb_impl.cpp` — PNG screenshot capture
  - `nlohmann-json` added to `vcpkg.json` and `CMakeLists.txt`
  - FPS tracking in main loop via QPC, updates `g_metrics.fps` every second

- **MCP server** (`add-mcp-primitives`): `@uzmigames/uzengine-mcp` at `tools/mcp-uzengine/`
  - 5 stable primitive tools: `uz_call`, `uz_screenshot`, `uz_status`, `uz_read_file`, `uz_write_file`
  - `uz_screenshot` returns inline base64 PNG — Claude sees the frame directly
  - `ipc-client.js` handles command/response polling with UUID-based dedup
  - Added `uzengine` entry to `.mcp.json` — auto-discovered by Claude Code on workspace open
  - `docs/ai-interaction.md` — full tool API reference and Skills model documentation

- **H.264 encoder improvements**:
  - Profile/level fallback cascade: `Main@4.2 → Main@4.0 → Baseline@4.2 → Baseline@4.0 → Baseline@3.1`
  - Fixes `SetOutputType failed: 0xC00D36B6` at 60fps (Baseline@3.1 too low for 60fps)
  - BGRA→NV12 conversion with BT.601 coefficients when MFT rejects ARGB32 input
  - `useNV12_` flag auto-detected from `SetInputType` fallback

- **60fps pipeline**:
  - `kTargetFPS = 60` in `src/common/types.h`
  - Muxer ticks: `90000 / kTargetFPS`, keyframe interval: `frameCount % kTargetFPS`
  - Ring buffer reduced to 2 frames for lower latency
  - Encode thread uses `yield()` instead of `sleep(1ms)`
  - Removed vsync: `Present(0, 0)` instead of `Present(1, 0)`

- **3D pyramid geometry**:
  - Replaced flat triangle with 4-face pyramid (apex yellow, base RGB vertices)
  - Depth buffer added (D32_FLOAT DSV heap), Z-test enabled in PSO
  - Rotation: Y-axis + 0.3 rad X tilt for 3D depth perception

- **Headless window mode**:
  - `UZENGINE_HEADLESS=1` env var suppresses window
  - Window created with `WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_POPUP` at position `-32000,-32000`
  - `ShowWindow` never called in headless mode — zero taskbar presence, zero Alt+Tab

- **VSCode extension**:
  - Extension starts UzEngine.exe automatically when stream viewer opens
  - MSE codec string updated: tries `Main@4.2 → Main@4.0 → Baseline@4.0` in order
  - Buffer trim reduced to 0.3s + live-edge seek when >0.5s behind buffered end
  - `.vscodeignore` fix: `server-manager.js` now included in `.vsix`

### Fixed
- `ix::initNetSystem()` missing on Windows — caused immediate crash on startup
- `ICodecAPI` undeclared — added `#include <strmif.h>`
- `QueryInterface` wrong overload — changed to `__uuidof(ICodecAPI)` form
- Colored vertical stripes in stream — BGRA readback was fed as NV12 without conversion
- `ixwebsocket` include path — added explicit vcpkg include dir to CMakeLists
- `zlib1.dll` missing at runtime — added `zlib` to vcpkg dependencies

---

## [0.1.0] — Initial Release

- DX12 spinning triangle renderer with GPU readback
- NVENC primary encoder + MF H.264 (Windows Media Foundation) fallback
- Hand-written fMP4 muxer (fragmented MP4, no external libs)
- WebSocket server via IXWebSocket for real-time H.264 streaming
- VSCode extension with MSE player in webview (MediaSource Extensions)
- Full pipeline: DX12 → readback → H.264 → fMP4 → WebSocket → VSCode webview
