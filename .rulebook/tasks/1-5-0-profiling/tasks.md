# 1.5.0 — Profiling & Instrumentation

## Phase 1: Tracy Build Integration
- [ ] 1.1 Link Tracy client library in build.zig (conditional on `UZ_PROFILING_ENABLED`)
- [ ] 1.2 Add `zig build -Dprofiling=true` option to enable/disable Tracy
- [ ] 1.3 Verify Tracy macros expand to real calls when enabled, no-op when disabled
- [ ] 1.4 Add `UZ_PROFILE_GPU_SCOPE` macro bridging Tracy GPU zones with DX12 timestamps

## Phase 2: CPU Instrumentation (C++ Runtime)
- [ ] 2.1 Instrument RHI layer: CreateDevice, CreateSwapChain, CreatePipeline, CreateBuffer, CreateShader
- [ ] 2.2 Instrument ShaderPipeline: LoadBytecode, CompileShader, Mount, PollHotReload
- [ ] 2.3 Instrument VFS: Read, Exists (LooseFileVFS, PakVFS, LayeredVFS)
- [ ] 2.4 Instrument AssetCache: Store, Lookup, ComputeKey
- [ ] 2.5 Instrument render loop: BeginFrame, Draw, Present, fence waits
- [ ] 2.6 Instrument Window: Create, PumpMessages, Resize
- [ ] 2.7 Add `UZ_PROFILE_FRAME_MARK()` at frame boundaries in render loop

## Phase 3: GPU Profiler Wiring
- [ ] 3.1 Wire `DX12GpuProfiler::Init()` into DX12Device initialization
- [ ] 3.2 Wire `BeginFrame`/`EndFrame` into CommandList recording
- [ ] 3.3 Wire `ResolveFrame` after fence wait (read timestamps from N-2)
- [ ] 3.4 Add GPU timestamp scopes: Clear, DrawCalls, Present barriers
- [ ] 3.5 Expose GPU timing results via `DX12Device::GetGpuProfiler()`

## Phase 4: Stat Counter Activation
- [ ] 4.1 Define stat counters: DrawCalls, Triangles, BufferUploads, ShaderCompiles, CacheHits, CacheMisses, VFSReads, PipelineCreations
- [ ] 4.2 Wire `UZ_STAT_INCREMENT` at all relevant callsites
- [ ] 4.3 Add per-frame stat reset at frame boundary
- [ ] 4.4 Add stat summary logging (optional, on shutdown or interval)

## Phase 5: .cpuprofile Export (C++ Standalone Capture)
- [ ] 5.1 Implement `ProfileCapture` — lightweight ring buffer of scope enter/exit events with `QueryPerformanceCounter` timestamps
- [ ] 5.2 No Tracy dependency — standalone capture always available (even in release)
- [ ] 5.3 Export to `.cpuprofile` JSON format (Chrome DevTools Protocol / VSCode Flame Chart)
- [ ] 5.4 Required fields: `nodes[]` (id, callFrame {functionName, scriptId, url, lineNumber, columnNumber}, children[]), `startTime`, `endTime`, `samples[]`, `timeDeltas[]`
- [ ] 5.5 Map C++ scopes to synthetic `scriptId`/`url` using `__FILE__` and `__LINE__` from macro callsite
- [ ] 5.6 Trigger: C++ API `ProfileCapture::StartCapture()` / `StopAndExport(path)`
- [ ] 5.7 Trigger: keyboard shortcut in demo (F11 toggle start/stop)

## Phase 6: Extension Profiling Bridge (IPC → VSCode)
- [ ] 6.1 Define IPC messages: `profile.start`, `profile.stop`, `profile.status`, `profile.data`
- [ ] 6.2 C++ side: handle `profile.start` → start ProfileCapture, `profile.stop` → export + send `.cpuprofile` path
- [ ] 6.3 Extension side: command `uzengine.startProfile` / `uzengine.stopProfile`
- [ ] 6.4 Extension: on stop, auto-open `.cpuprofile` in VSCode via `vscode.open` (built-in Flame Chart viewer)
- [ ] 6.5 Extension: realtime stat overlay panel (WebView) — receives stat snapshots via IPC every N frames
- [ ] 6.6 IPC message `stats.snapshot`: { fps, frameTimeMs, drawCalls, triangles, gpuTimeMs, cacheHits, vramUsedMB }
- [ ] 6.7 Extension: status bar item showing live FPS + GPU ms (updated per-second via `stats.snapshot`)

## Phase 7: GPU Timing Report
- [ ] 7.1 Log-based GPU timing report (per-pass ms breakdown at shutdown or on-demand)
- [ ] 7.2 Feed GPU timings into Tracy GPU zones (when Tracy enabled)
- [ ] 7.3 Include GPU pass timings in `stats.snapshot` IPC message
- [ ] 7.4 Include GPU lanes in `.cpuprofile` export as synthetic nodes (GPU thread)

## Phase 8: Triangle Demo Integration
- [ ] 8.1 Wire GPU profiler into triangle demo render loop
- [ ] 8.2 Add CPU profile scopes to demo initialization and render
- [ ] 8.3 Add stat counters (draw calls, frames rendered)
- [ ] 8.4 F11 to toggle `.cpuprofile` capture, log GPU timing summary on exit
- [ ] 8.5 Print stat summary to log on exit (draw calls, frames, avg frame time, cache hits)

## Phase 9: Tests
- [ ] 9.1 Tests: StatCounter thread safety (concurrent increments from N threads, reset, get)
- [ ] 9.2 Tests: ProfileCapture — start/stop, scope events recorded, ring buffer overflow
- [ ] 9.3 Tests: .cpuprofile export — valid JSON, nodes/samples/timeDeltas structure, opens in VSCode
- [ ] 9.4 Tests: GPU profiler Init/BeginFrame/EndFrame (mock device or skip on CI)
- [ ] 9.5 Tests: UZ_PROFILE_SCOPE macro expansion (enabled vs disabled builds)
- [ ] 9.6 Tests: IPC profile messages — start/stop round-trip, stats.snapshot schema

## Success Criteria
- [ ] All engine systems instrumented with `UZ_PROFILE_SCOPE` / `UZ_PROFILE_FUNCTION`
- [ ] GPU timestamps captured per-pass with ms resolution
- [ ] Stat counters tracked: draw calls, triangles, cache hits, shader compiles, VFS reads
- [ ] `.cpuprofile` export works and opens natively in VSCode Flame Chart viewer
- [ ] Extension can start/stop profiling via command palette, auto-opens result
- [ ] Extension shows live FPS + GPU ms in status bar via IPC stats
- [ ] Tracy live profiling works when `UZ_PROFILING_ENABLED` is set
- [ ] Zero overhead when profiling disabled (all macros compile to no-ops)
- [ ] 95%+ test coverage on new profiling code
