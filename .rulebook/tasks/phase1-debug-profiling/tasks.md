## 1. Assert System (`src/core/assert.h`)

- [ ] 1.1 Define `ASSERT(expr)` — debug-break + stderr log + file/line/function, stripped in Release
- [ ] 1.2 Define `ASSERT_MSG(expr, fmt, ...)` — same as ASSERT with printf-style message
- [ ] 1.3 Define `VERIFY(expr)` — evaluates in Release, breaks only in Debug
- [ ] 1.4 Define `ENSURE(expr)` — logs in Release + continues (recoverable errors)
- [ ] 1.5 Replace all ad-hoc `if (!x) return` patterns in existing code with ASSERT/VERIFY
- [ ] 1.6 Add `UZ_DEBUG` CMake define for Debug builds; `UZ_SHIPPING` strips all asserts

## 2. CPU Profiler: Tracy (`src/core/profiler.h`)

- [ ] 2.1 Add `tracy` to `vcpkg.json`
- [ ] 2.2 CMakeLists: `option(UZ_PROFILING ...)`, link `Tracy::TracyClient`, define `TRACY_ENABLE`
- [ ] 2.3 Create `src/core/profiler.h`: `UZ_PROFILE_FUNCTION`, `UZ_PROFILE_ZONE(name)`, `UZ_PROFILE_FRAME_MARK`, `UZ_PROFILE_VALUE`, `UZ_PROFILE_MESSAGE` — all no-ops when `UZ_PROFILING=0`
- [ ] 2.4 Insert `UZ_PROFILE_FUNCTION()` in: render loop, JPEG encode, WebSocket send, IPC poll
- [ ] 2.5 Insert `UZ_PROFILE_FRAME_MARK()` at end of each rendered frame
- [ ] 2.6 Add `UZ_PROFILE_VALUE("FPS", fps)` and `UZ_PROFILE_VALUE("FrameMS", frame_ms)`

## 3. GPU Profiler: Tracy GPU + PIX

- [ ] 3.1 Add `winpixeventruntime` to `vcpkg.json`
- [ ] 3.2 Create `TracyD3D12Context` in renderer startup (when `UZ_PROFILING=1`)
- [ ] 3.3 Wrap all major render passes with `TracyD3D12Zone`: shadow pass, G-Buffer, lighting, post-process, JPEG readback
- [ ] 3.4 Add `PIXBeginEvent`/`PIXEndEvent` markers to same render passes (always-on in Debug/Profile)
- [ ] 3.5 IPC handler `engine.capture_frame`: calls `PIXCaptureNextFrame()`
- [ ] 3.6 IPC handler `engine.capture_renderdoc`: calls RenderDoc API `StartFrameCapture` (if RenderDoc DLL loaded)
- [ ] 3.7 DX12 timestamp queries: `ID3D12QueryHeap` for per-pass GPU time reported to `FrameStats`

## 4. Memory Debugging

- [ ] 4.1 CMakeLists: `option(UZ_ASAN ...)` adds `/fsanitize=address` to Debug target
- [ ] 4.2 `Arena` struct: in `UZ_DEBUG` builds, track per-allocation records (size, file, line) and high-water mark
- [ ] 4.3 `Arena::dump_stats()` prints allocation summary on `reset()` if `UZ_DEBUG && debug_validate_=true`
- [ ] 4.4 IPC handler `engine.dump_arena_stats`: calls `dump_stats()` on all arenas, streams JSON to extension
- [ ] 4.5 Enable ASAN CI build: add CMake preset `Debug-ASAN` with `UZ_ASAN=ON`

## 5. Debug Overlay: ImGui

- [ ] 5.1 Add `imgui[dx12-binding,win32-binding]` to `vcpkg.json`
- [ ] 5.2 `DebugOverlay::init(device, rtv_format)` — init ImGui DX12 backend, descriptor heap for fonts
- [ ] 5.3 `DebugOverlay::render(cmd_list, FrameStats&)` — render ImGui into command list before JPEG readback
- [ ] 5.4 `DebugOverlay` panel contents: FPS graph (128-sample ring), GPU ms, draw calls, triangle count, arena usage (frame + level), JPEG size, WS clients
- [ ] 5.5 IPC handler `engine.debugOverlay(enabled: bool)` — toggle overlay at runtime
- [ ] 5.6 Hotkey (IPC-only in editor): `Ctrl+Shift+~` → toggle overlay via VSCode command
- [ ] 5.7 `UZ_SHIPPING=1` completely removes ImGui from binary (`#ifndef UZ_SHIPPING`)

## 6. Unit Tests: doctest + CTest

- [ ] 6.1 Add `doctest` to `vcpkg.json`
- [ ] 6.2 `tests/CMakeLists.txt`: `add_executable(UzEngine_Tests ...)`, `enable_testing()`, `add_test(...)`
- [ ] 6.3 `tests/main.cpp`: `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`
- [ ] 6.4 `tests/test_ring_buffer.cpp`: push/pop, wrap-around, full/empty conditions
- [ ] 6.5 `tests/test_arena.cpp`: alloc, reset, out-of-bounds detection in UZ_DEBUG
- [ ] 6.6 `tests/test_jpeg_encoder.cpp`: SOI marker, quality vs size, RGBA input
- [ ] 6.7 `tests/test_ipc_registry.cpp`: register handler, dispatch, unknown fn returns error JSON
- [ ] 6.8 `tests/test_scene_loader.cpp` (after Phase 1 scene system): roundtrip save/load `.uzscene`
- [ ] 6.9 CI: `ctest --output-on-failure` must pass with 0 failures

## 7. IPC Debug Commands

- [ ] 7.1 `engine.debugOverlay(enabled)` — implemented in task 5.5
- [ ] 7.2 `engine.capture_frame()` — implemented in task 3.5
- [ ] 7.3 `engine.capture_renderdoc()` — implemented in task 3.6
- [ ] 7.4 `engine.dump_arena_stats()` — implemented in task 4.4
- [ ] 7.5 `engine.set_profiling(enabled)` — enable/disable Tracy capture session at runtime
- [ ] 7.6 `engine.run_tests()` — spawns `UzEngine_Tests.exe`, streams stdout back as IPC event stream

## 8. VSCode Extension Integration

- [ ] 8.1 Output channel "UzEngine Debug": receive `engine.dump_arena_stats` and `engine.run_tests` output
- [ ] 8.2 Status bar item: show current FPS + GPU ms (received from engine.status event stream)
- [ ] 8.3 Command "UzEngine: Capture Frame" → sends `engine.capture_frame` IPC
- [ ] 8.4 Command "UzEngine: Run Tests" → sends `engine.run_tests`, streams results to output channel

## 9. Testing

- [ ] 9.1 ASSERT triggers debugger break on false condition in Debug build
- [ ] 9.2 ASSERT compiles to nothing in Release build (confirmed via disassembly)
- [ ] 9.3 Tracy profiler: zones visible in Tracy UI, GPU zones match PIX GPU timeline
- [ ] 9.4 ImGui overlay visible in VSCode stream viewport at 30+ FPS
- [ ] 9.5 ASAN catches a deliberate out-of-bounds write in test
- [ ] 9.6 `ctest` passes 100% of tests on clean checkout
- [ ] 9.7 PIX capture triggered via IPC opens in PIX UI with correct frame data
