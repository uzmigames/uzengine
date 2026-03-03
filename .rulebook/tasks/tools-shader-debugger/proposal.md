# Proposal: tools-shader-debugger

## Why

Shader bugs are among the most expensive bugs in game development to diagnose. A black screen, an invisible mesh, incorrect lighting — the cause could be a wrong matrix multiply, a missing shader variant, a broken descriptor binding, or a resource state transition error. Without a GPU frame capture, a developer can only guess. With a capture, the problem is usually visible in under a minute. The barrier is the workflow: launch PIX or RenderDoc separately, manually attach to the engine process (which may be embedded in VSCode), capture a frame, save the file, and open it in the profiler UI.

The `tools-shader-debugger` tool removes every manual step from this workflow. A single VSCode command "UzEngine: Capture GPU Frame (PIX)" or "UzEngine: Capture GPU Frame (RenderDoc)" sends an IPC command to the running engine, which uses PIX programmatic capture (`PIXCaptureParameters`, `PIXBeginCapture`/`PIXEndCapture`) or RenderDoc's in-application API (`RENDERDOC_API_1_6_0`) to capture the next rendered frame, save the `.wpix` or `.rdc` file to a configurable output path, and then launches the profiler with the capture pre-loaded. The VSCode output panel shows the capture file path and any API errors. The whole workflow takes one keypress.

For shader authors working within the VSCode editor environment, this integration reduces the GPU debugging iteration cycle from 5–10 minutes of manual setup per session to under 10 seconds. It is especially critical for AI agents generating HLSL code — they can request a frame capture as part of their validation workflow via MCP, inspect the output, and self-correct without human intervention.

## What Changes

- PIX programmatic capture integration: link against `WinPixEventRuntime` via vcpkg, wrap in `ShaderDebugger::CapturePIX()` in `engine/tools/shader_debugger.h`
- RenderDoc in-application API integration: load `renderdoc.dll` at runtime (optional), call `RENDERDOC_API_1_6_0::StartFrameCapture` / `EndFrameCapture` from `ShaderDebugger::CaptureRenderDoc()`
- IPC handler `tools.capture_frame(backend: "pix" | "renderdoc", output_path: string)` added to engine IPC server
- Engine command-line flag `--shader-debug` enables debug layers (D3D12 validation + GPU-based validation) for diagnostic sessions
- VSCode command `uzengine.captureGPUFrame` (TypeScript): sends IPC command, polls for output file, opens file in registered application
- User setting `uzengine.pixPath` and `uzengine.renderdocPath` for profiler executable locations
- Capture metadata file `{capture_name}.capture.json`: frame number, timestamp, engine version, scene file, shader variant flags active at capture time
- MCP tool `tools.capture_frame` exposed via MCP server — allows AI agents to request captures programmatically
- Unit tests: mock IPC handler tests; integration test verifies capture file is created and non-empty

## Impact

- Affected specs: `docs/specs/debug-profiling.md` (add GPU capture section), `docs/specs/ai-native-features.md` (add tools.capture_frame MCP tool)
- Affected code: `engine/src/tools/shader_debugger.cpp` (new), `engine/src/ipc/handlers/tools_handlers.cpp` (new IPC entry), `vscode-extension/src/commands/captureGPUFrame.ts` (new)
- Breaking change: NO (new tool, opt-in capture API)
- User benefit: Reduces GPU debugging setup from 5+ manual steps to a single VSCode command; enables AI agents to self-diagnose shader output via MCP capture API; captures include scene context metadata for faster root-cause analysis
