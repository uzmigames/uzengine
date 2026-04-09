# 4.12.0 — Profiling IPC Bridge (VSCode Extension)

## Metadata
- **Phase**: 4 (Platform & Editor Infrastructure)
- **Status**: planned
- **Priority**: medium
- **Effort**: medium (3–5 days)
- **Dependencies**:
  - 1-5-0-profiling (Phases 1–5 complete — Tracy, CPU capture, GPU profilers, stat counters)
  - IPC protocol infrastructure (message transport between runtime and VSCode extension)

## Context

Phases 1–5 of the profiling system are complete: Tracy CPU instrumentation (114+ files), GPU timestamp profilers (DX12/DX11/Vulkan/Metal), stat counters (40+ stats, 7 groups), and `.cpuprofile` export. What's missing is the **IPC bridge** that lets the VSCode extension remotely control profiling and display live stats.

---

## 1. IPC Message Definitions

- [ ] 1.1 Define `profile.start` message — triggers `ProfileCapture::Get().StartCapture()`
- [ ] 1.2 Define `profile.stop` message — triggers `StopAndExport(path)`, responds with `.cpuprofile` file path
- [ ] 1.3 Define `profile.status` message — returns current capture state (idle/capturing/exporting)
- [ ] 1.4 Define `stats.snapshot` message — periodic frame stats payload: `{ fps, frameTimeMs, drawCalls, triangles, gpuTimeMs, cacheHits, vramUsedMB }`

## 2. C++ Runtime Handlers

- [ ] 2.1 Handle `profile.start` IPC message → start ProfileCapture
- [ ] 2.2 Handle `profile.stop` IPC message → export `.cpuprofile` + send path in response
- [ ] 2.3 Handle `profile.status` IPC message → return capture state
- [ ] 2.4 Implement periodic `stats.snapshot` sender (every N frames, configurable)
- [ ] 2.5 Include GPU pass timing breakdown in stats snapshot

## 3. VSCode Extension Commands

- [ ] 3.1 Register `uzengine.startProfile` command (command palette + keybinding)
- [ ] 3.2 Register `uzengine.stopProfile` command — on stop, auto-open `.cpuprofile` via `vscode.open` (built-in Flame Chart viewer)
- [ ] 3.3 Status bar item showing live FPS + GPU ms (updated per-second via `stats.snapshot`)
- [ ] 3.4 Optional: WebView panel for realtime stat overlay (draw calls, triangles, VRAM, cache hits)

## 4. GPU Timing Report

- [ ] 4.1 Log-based GPU timing report (per-pass ms breakdown on shutdown or on-demand)
- [ ] 4.2 Feed GPU timings into Tracy GPU zones (when Tracy enabled)
- [ ] 4.3 Include GPU pass timings in `stats.snapshot` IPC message
- [ ] 4.4 Include GPU lanes in `.cpuprofile` export as synthetic nodes (GPU thread)

## 5. Testing

- [ ] 5.1 Test IPC `profile.start` / `profile.stop` round-trip
- [ ] 5.2 Test `stats.snapshot` schema validation
- [ ] 5.3 Test `.cpuprofile` auto-open in VSCode after stop
- [ ] 5.4 Test GPU timing data presence in stats snapshot

## Success Criteria

- [ ] Extension can start/stop profiling via command palette, auto-opens `.cpuprofile` result
- [ ] Extension shows live FPS + GPU ms in status bar via IPC stats
- [ ] GPU per-pass timing breakdown available in stats snapshot
- [ ] All IPC messages have schema validation tests

## 6. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 6.1 Update or create documentation covering the implementation
- [ ] 6.2 Write tests covering the new behavior
- [ ] 6.3 Run tests and confirm they pass
