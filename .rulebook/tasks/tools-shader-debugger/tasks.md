# Tasks: tools-shader-debugger

## 1. PIX Programmatic Capture Integration

- [ ] 1.1 Add `WinPixEventRuntime` dependency to `vcpkg.json` and `CMakeLists.txt`
- [ ] 1.2 Create `engine/src/tools/shader_debugger.h` defining `ShaderDebugger` class with `CapturePIX(output_path) -> CaptureResult` and `CaptureRenderDoc(output_path) -> CaptureResult` static methods
- [ ] 1.3 Create `engine/src/tools/shader_debugger.cpp` implementing `CapturePIX`: call `PIXBeginCapture(PIX_CAPTURE_GPU, &params)`, render one frame, call `PIXEndCapture(false)`, save to `output_path`
- [ ] 1.4 Implement `CaptureResult` struct: `{success, output_path, frame_number, error_message}`
- [ ] 1.5 Guard all `ShaderDebugger` code with `#ifdef UZ_TOOLS` to prevent PIX dependency in shipping builds
- [ ] 1.6 Handle PIX not installed case gracefully: check if `WinPixGpuCapturer.dll` is loadable before calling any PIX API; return `CaptureResult{success:false, error_message:"PIX not installed"}` if absent

## 2. RenderDoc In-Application API Integration

- [ ] 2.1 Load `renderdoc.dll` at runtime via `LoadLibrary` (optional dependency, not linked at compile time)
- [ ] 2.2 Query `RENDERDOC_GetAPI` entry point; obtain `RENDERDOC_API_1_6_0*` interface pointer
- [ ] 2.3 Implement `ShaderDebugger::CaptureRenderDoc(output_path)`: call `StartFrameCapture(nullptr, nullptr)`, render one frame, call `EndFrameCapture`, move output `.rdc` to `output_path`
- [ ] 2.4 Handle RenderDoc not available case: return `CaptureResult{success:false, error_message:"RenderDoc not available"}` if `renderdoc.dll` not found or API query fails

## 3. Engine Debug Layer Support

- [ ] 3.1 Add `--shader-debug` CLI flag to the engine entry point
- [ ] 3.2 When `--shader-debug` is active, enable `ID3D12Debug::EnableDebugLayer()` and GPU-based validation (`D3D12_GPU_BASED_VALIDATION`)
- [ ] 3.3 Configure DX12 `ID3D12InfoQueue` to break on `D3D12_MESSAGE_SEVERITY_ERROR` in debug sessions

## 4. IPC Handler

- [ ] 4.1 Add `tools.capture_frame(backend: "pix"|"renderdoc", output_path: string)` IPC handler in `engine/src/ipc/handlers/tools_handlers.cpp`
- [ ] 4.2 Implement: set `capture_next_frame` atomic flag with backend and path; renderer executes capture before next frame render
- [ ] 4.3 Return `CaptureResult` JSON to IPC caller on completion
- [ ] 4.4 Register the IPC handler in the engine's IPC dispatch table

## 5. Capture Metadata

- [ ] 5.1 Write `{capture_name}.capture.json` alongside the capture file containing: `frame_number`, `timestamp_iso8601`, `engine_version`, `scene_file_path`, `shader_variant_flags`
- [ ] 5.2 Query active shader variant flags from the renderer's shader variant registry at capture time

## 6. MCP Tool Exposure

- [ ] 6.1 Register `tools.capture_frame` as an MCP tool in the MCP server tool registry
- [ ] 6.2 Document the MCP tool in `docs/specs/ai-native-features.md` with parameter descriptions and example usage

## 7. VSCode Extension Integration

- [ ] 7.1 Create `vscode-extension/src/commands/captureGPUFrame.ts` implementing the `uzengine.captureGPUFrame` command
- [ ] 7.2 Add user settings `uzengine.pixPath` (path to PIX executable) and `uzengine.renderdocPath` (path to RenderDoc executable)
- [ ] 7.3 On capture completion, write capture path to VSCode Output panel and open the file in the registered profiler application using `vscode.env.openExternal`
- [ ] 7.4 Show progress notification "Capturing GPU frame..." while capture is in flight; dismiss on completion

## 8. Testing (≥95% line + branch coverage)

- [ ] 8.1 Write `tests/tools/test_shader_debugger_pix_mock.cpp`: mock PIX API; call `ShaderDebugger::CapturePIX`; assert `CaptureResult.success == true` and output path matches input; assert `PIXBeginCapture` and `PIXEndCapture` called once each
- [ ] 8.2 Write `tests/tools/test_shader_debugger_pix_missing.cpp`: simulate PIX DLL not available; assert `CaptureResult.success == false` and `error_message` contains "PIX not installed"
- [ ] 8.3 Write `tests/tools/test_shader_debugger_renderdoc_mock.cpp`: mock RenderDoc API entry points via dependency injection; assert `StartFrameCapture` and `EndFrameCapture` called; assert output file path set correctly
- [ ] 8.4 Write `tests/tools/test_capture_metadata.cpp`: call `CapturePIX` with known scene and variant flags; assert `.capture.json` file exists and contains correct `frame_number` and `scene_file_path`
- [ ] 8.5 Write `tests/tools/test_ipc_capture_frame_handler.cpp`: send `tools.capture_frame` IPC message with `backend:"pix"`; assert `capture_next_frame` flag is set and IPC response contains `CaptureResult` fields
- [ ] 8.6 Verify ≥95% line and branch coverage across all `engine/src/tools/shader_debugger.cpp` using OpenCppCoverage
