# Changelog — extension

## [0.3.0] — 2026-03-28

### Worker Isolation, H.264 Streaming, Scene Editor

#### Worker Process Architecture
- **feat:** `RuntimeClient` IPC proxy — all native addon calls are forwarded to an isolated child process via `child_process.fork()`. GPU crashes kill only the worker, not the Extension Host.
- **feat:** Worker copies `uzengine.node` + `uzengine_runtime.dll` to `%TEMP%/uzengine-worker-{pid}/` before loading. Original files in `extension/native/` are **never locked** by the Windows loader, allowing DLL replacement while VSCode is running.
- **feat:** Temp directory is cleaned up automatically on worker exit.

#### Scene Editor (Custom Editor for .uzscene)
- **feat:** `SceneEditorProvider` — custom text editor that opens `.uzscene` files in a viewport with H.264 video streaming from the DX12 renderer.
- **feat:** Full-size `<canvas>` with WebCodecs `VideoDecoder` for low-latency H.264 decoding.
- **feat:** Toolbar overlay with gizmo buttons (Translate/Rotate/Scale, Local/World space) and live stats (FPS, frame time, draw calls, triangles).
- **feat:** Input forwarding (mouse move/button, keyboard, scroll) from webview to native renderer via fire-and-forget IPC.
- **feat:** Automatic content root detection from `.uzscene` path (strips `/scenes/` segment).
- **feat:** Session management — worker restart on tab reopen, stale session guard.

#### Viewport Panel
- **feat:** `ViewportPanel` — standalone viewport with auto-load of `.uzproject` default scene.
- **feat:** Pointer lock on right-click for camera orbit (mouseDelta forwarding).
- **feat:** Key mapping from `KeyboardEvent.code` to Win32 virtual-key codes with modifier support.

#### Scene Tree
- **feat:** `SceneTreeProvider` — tree view for entity hierarchy, lazy client access, async IPC refresh.

#### Console Panel
- **feat:** `EngineConsole` — output channel with log forwarding from C++ runtime via worker IPC.

#### Centralized Logging
- **feat:** Worker log now writes to `%LOCALAPPDATA%/UzEngine/logs/worker_YYYY-MM-DD_HH-MM-SS.log`.

#### Build & Debug
- **fix:** `launch.json` timeout increased to 45s for Extension Host debugger attach.
- **fix:** Added `--disable-extensions` to dev launch config to avoid Extension Host hangs from heavy extensions (Copilot, etc.).

## [0.2.0] — 2026-03-10

### Added
- VSCode extension skeleton with activation entry point
- Native addon loading (`uzengine.node` from `native/`)
- ESLint 9 flat config with typescript-eslint 8 (errors only, no warnings)
- Prettier 3.8 config
- Vitest 4 config with 95% coverage threshold
- LogOutputChannel logging (replaces console.log)
- Syntax highlighting for HLSL (`.hlsl`, `.hlsli`, `.fx`, `.fxh`)
- Syntax highlighting for Zig (`.zig`)
- Language registration for UzEngine formats (`.uzscene`, `.uzmaterial`, `.uzmesh`, `.uzprefab`, `.uzproject`)
- Markdown fenced code block injection for `hlsl` and `zig`
- C/C++ syntax injection for UzEngine types, macros, and interfaces:
  - Assert macros (`UZ_CHECK`, `UZ_ENSURE`, `UZ_VERIFY`)
  - Logging (`UZ_LOG`, `UZ_DECLARE_LOG_CATEGORY`, `UZ_DEFINE_LOG_CATEGORY`)
  - Profiling (`UZ_PROFILE_FUNCTION`, `UZ_PROFILE_SCOPE`, `UZ_PROFILE_FRAME_MARK`)
  - Stat counters (`UZ_DECLARE_STAT`, `UZ_DEFINE_STAT`, `UZ_STAT_INCREMENT`)
  - RHI interfaces (`IRHIDevice`, `IRHICommandList`, `IRHISwapChain`, `IRHIFence`)
  - RHI types and enums (`RHIFormat`, `RHIBackend`, `RHIHandle`, `ShaderModel`, etc.)
  - Shader pipeline (`ShaderVFS`, `ShaderCache`, `ShaderCompiler`, `ShaderPipeline`, etc.)
  - Math types (`Vec2`, `Vec3`, `Vec4`, `Mat4`, `Quat`, `Transform`, `AABB`, `Frustum`)
  - Containers (`RingBuffer`, `FreeList`, `HandleMap`, `FrameArray`, `ResourcePool`)
  - Memory (`Own`, `Ref`, `RefMut`, `LinearAllocator`, `PoolAllocator`, `FrameAllocator`)
  - DX12 backend (`DX12Device`, `DX12SwapChain`, `DX12Fence`, `DescriptorAllocator`)
  - Platform (`PlatformWindow`, `InputSystem`, `WindowDesc`)
- Extension marketplace icon (`icon.png`)
- File type icons (SVG) for HLSL, Zig, and all UzEngine formats
- `open-vscode.bat` launcher with `--extensionDevelopmentPath` and `--remote-debugging-port=9229`
