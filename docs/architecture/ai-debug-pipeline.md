# AI-First Automated Debugging Pipeline

**Document Version**: 2.0
**Date**: 2026-03-06
**Classification**: Architecture Standard & Implementation Spec
**Status**: Active

## 1. Executive Summary

The current development workflow is bottlenecked by a reliance on manual user intervention for visual verification (screenshots) and crash diagnostics. To enable rapid, autonomous development by AI agents (CLI and VS Code extensions), UzEngine must expose automated, programmatic interfaces for **Crash Diagnostics**, **Visual Introspection**, **Real-time Telemetry**, and **Build Verification**.

This document specifies the architecture for an "AI-First Debugging Pipeline" that eliminates the need for manual user screenshots and opaque crash reports.

---

## 2. Current State (Already Implemented)

Before defining new work, we document what already exists to avoid duplication.

| Capability | Implementation | Status |
| :--- | :--- | :--- |
| Frame Capture (PNG, base64) | MCP `uz_screenshot` / IPC `engine.screenshot` | ✅ Done |
| Real-time Metrics | MCP `uz_status` (fps, frame count, uptime, resolution) | ✅ Done |
| Engine Function Calls | MCP `uz_call` (generic IPC dispatch) | ✅ Done |
| Shader Hot-Reload | IPC `shader.reload` | ✅ Done |
| Scene Load/Reload | IPC `scene.load` | ✅ Done |
| File Read/Write | MCP `uz_read_file` / `uz_write_file` | ✅ Done |
| Engine Restart | MCP `uz_restart` (kill + relaunch + wait) | ✅ Done |
| Headless Mode | `--headless` CLI flag | ✅ Done |
| Structured Crash Reports | `.uzengine/crash_reports/latest.json` | ❌ Missing |
| GPU Crash Telemetry (DRED) | DX12 Device Removed Extended Data | ❌ Missing |
| Golden Image Regression Tests | Automated baseline comparison | ❌ Missing |
| Structured Log Streaming | JSON log emission over IPC | ❌ Missing |
| Build Error Parsing | CMake/MSVC structured error output | ❌ Missing |
| GPU Resource Introspection | Intermediate buffer extraction | ❌ Missing |

---

## 3. Core Objectives

1. **Automated Visual Introspection:** AI agents must programmatically request and receive visual snapshots of the engine's current state without user involvement.
2. **Structured Crash Diagnostics:** When the engine crashes (e.g., DX12 Device Removed, TDR), the engine must generate structured, machine-readable telemetry (call stacks, GPU state, minidumps, last logs) that agents can automatically consume.
3. **Build Verification:** When the C++ build fails, AI agents must be able to parse structured compiler errors, apply fixes, and rebuild — without manual intervention.
4. **Headless & CI-Ready:** All debugging features must work in headless/CI environments and be triggered via IPC (WebSockets) or CLI arguments.

---

## 4. Pillar 1: Automated Visual Debugging

### 4.1. IPC Frame Request (Already Implemented)

The engine already supports frame capture via MCP/IPC:

```json
// MCP: uz_screenshot → returns base64 PNG
// IPC: engine.screenshot → saves to disk, returns path
```

**Remaining work**: Create a CLI wrapper script `tools/capture_frame.ps1` that wraps the MCP call for use in CI/scripts.

### 4.2. Automated Render Tests (Golden Images)

For renderer development, we establish a "Golden Image" regression testing suite.

**Spec:**
- **Baseline directory:** `assets/test/golden/<test_name>/`
- **Image format:** PNG, fixed resolution (1920×1080)
- **Comparison method:** SSIM (Structural Similarity Index) with configurable threshold (default: 0.98)
- **Fallback:** Per-pixel absolute difference with max 0.5% deviation tolerance

**Flow:**
1. AI writes or loads a test scene (`.uzscene` file).
2. Engine runs headlessly for N frames (configurable, default 10).
3. Engine captures the final frame via `engine.screenshot`.
4. Comparison tool (`tools/golden_test.ps1`) compares output to `assets/test/golden/<test_name>/baseline.png`.
5. If SSIM < threshold → test fails, diff image saved to `assets/test/golden/<test_name>/diff.png`.
6. AI analyzes the diff image via Vision API to understand what changed.

**CI integration:**
```powershell
# tools/golden_test.ps1 -TestName "pbr_spheres" -Scene "assets/scenes/test_pbr.uzscene"
# Exits with code 0 on pass, 1 on fail
```

### 4.3. GPU Resource Introspection

Extend IPC to support intermediate buffer extraction. AI can request not just the final backbuffer, but specific render targets:

```json
{"type": "request_texture", "name": "GBuffer_Normal", "format": "png"}
```

This allows AI to debug specific passes (normals, albedo, roughness, depth) without guessing.

**Implementation note:** Requires readback support per-RT in the RHI layer. Buffer names are derived from the RenderGraph node names.

---

## 5. Pillar 2: Structured Crash Diagnostics

When a crash occurs, especially a GPU crash (TDR), standard output is insufficient. The AI needs structured, machine-readable context.

### 5.1. Unhandled Exception & Signal Handler

Implement a global crash handler (`SetUnhandledExceptionFilter` on Windows, `sigaction` on Linux).

**On crash, generate two files in `${PROJECT_ROOT}/.uzengine/crash_reports/`:**

1. **`latest.json`** — Machine-readable crash report:
```json
{
  "timestamp": "2026-03-06T00:00:00Z",
  "crash_type": "EXCEPTION_ACCESS_VIOLATION",
  "os": { "name": "Windows 11", "build": "26100" },
  "hardware": { "gpu": "RTX 4070", "driver": "572.16", "ram_gb": 32 },
  "cpu_callstack": [
    { "module": "UzEngine.exe", "function": "Renderer::submitDrawCall", "file": "renderer.cpp", "line": 342 },
    { "module": "UzEngine.exe", "function": "RenderGraph::execute", "file": "render_graph.cpp", "line": 128 }
  ],
  "engine_state": {
    "scene": "assets/scenes/materials.uzscene",
    "camera_position": [0, 5, -10],
    "selected_node": "Sphere_PBR_01",
    "frame_number": 4521
  },
  "log_tail": ["... last 100 DIAG log lines ..."],
  "gpu_telemetry": null
}
```

2. **`latest.dmp`** — Windows MiniDump (via `MiniDumpWriteDump`) for offline reproduction and deeper analysis with WinDbg/Visual Studio.

### 5.2. GPU Crash Telemetry (DRED)

For rendering crashes, standard C++ call stacks are often useless. We implement Device Removed Extended Data (DRED) for DX12.

**On `DXGI_ERROR_DEVICE_REMOVED`:**
1. Call `ID3D12Device::GetDeviceRemovedReason()`.
2. Query `ID3D12DeviceRemovedExtendedData1` for auto-breadcrumbs and page fault info.
3. Extract the GPU page fault address and faulting operation.
4. Dump the active RenderGraph execution state (which pass was running, which resources were bound).
5. Save to `latest.json` under a `gpu_telemetry` node:

```json
{
  "gpu_telemetry": {
    "removed_reason": "DXGI_ERROR_DEVICE_HUNG",
    "dred_breadcrumbs": [
      { "command_list": "MainCL", "op": "DrawInstanced", "index": 42 },
      { "command_list": "MainCL", "op": "ResourceBarrier", "index": 43 }
    ],
    "page_fault": { "address": "0x00007FF800000000", "type": "GPU_READ" },
    "active_pass": "PBR_GBuffer",
    "bound_resources": ["albedo_rt", "normal_rt", "depth_stencil"]
  }
}
```

### 5.3. AI Auto-Recovery Loop

How AI agents use crash diagnostics:
1. AI runs tests or the user reports a crash.
2. AI reads `${PROJECT_ROOT}/.uzengine/crash_reports/latest.json`.
3. AI analyzes the call stack, DRED breadcrumbs, and GPU state.
4. AI applies a fix and re-runs via `uz_restart`.
5. If fix succeeds → AI captures a frame to verify visual correctness.

---

## 6. Pillar 3: Real-time Telemetry Stream

Instead of tailing a messy text log, the engine emits structured telemetry over IPC.

### 6.1. Structured Logging

Upgrade `DIAG` macros to support JSON emission over IPC with **subscription-based filtering**:

**Subscribe request:**
```json
{"type": "subscribe_logs", "filter": {"level": "WARN", "systems": ["RHI", "Renderer"]}}
```

**Log entry format:**
```json
{"level": "ERROR", "system": "RHI", "message": "Failed to compile shader 'pbr'", "file": "shader_lib.cpp", "line": 42, "timestamp": 1709683200}
```

**Severity levels (ordered):** `TRACE` → `DEBUG` → `INFO` → `WARN` → `ERROR` → `FATAL`

**Rate limiting:** Max 100 messages/second per subscription. If exceeded, a summary message is sent: `{"type": "log_overflow", "dropped_count": 42}`.

The VS Code extension buffers these and feeds them cleanly to the AI when an issue is detected.

### 6.2. Render Graph Introspection API

AI can query the active RenderGraph structure:
```json
{"type": "query_render_graph"}
```

**Returns:** Nodes, dependencies, texture formats, resolution, memory usage per resource, and execution timing.

---

## 7. Pillar 4: Automated Build Verification

The pipeline assumes the engine runs. But when the C++ build breaks, AI needs a structured path to recovery.

### 7.1. Build Error Parsing

Create `tools/build_and_report.ps1` that wraps CMake/MSBuild and outputs structured JSON:

```json
{
  "success": false,
  "errors": [
    {
      "file": "src/engine/renderer/renderer.cpp",
      "line": 142,
      "column": 15,
      "severity": "error",
      "code": "C2065",
      "message": "'m_renderGraph': undeclared identifier"
    }
  ],
  "warnings_count": 3,
  "build_time_ms": 12500
}
```

### 7.2. AI Build Recovery Loop

1. AI makes code changes.
2. AI runs `tools/build_and_report.ps1`.
3. On failure → AI reads structured errors, applies fix, rebuilds.
4. On success → AI runs engine and captures frame for visual verification.
5. Max 3 retry iterations before escalating to the user.

---

## 8. Implementation Roadmap

### Phase 1: Eyes — Visual Capture Enhancement `[LOW effort]`
- [ ] Create CLI wrapper `tools/capture_frame.ps1` around existing MCP `uz_screenshot`.
- [ ] Implement SSIM-based golden image comparison in `tools/golden_test.ps1`.
- [ ] Create baseline golden images for existing test scenes.
- [ ] Document golden test workflow in `docs/guides/golden-tests.md`.

### Phase 2: Brain — Crash Dumps `[HIGH effort]`
- [ ] Implement global C++ crash handler (`CrashReporter` class).
- [ ] Generate `latest.json` with symbolicated call stack on crash.
- [ ] Generate `latest.dmp` via `MiniDumpWriteDump`.
- [ ] Implement DX12 DRED breadcrumb capture on device removal.
- [ ] Serialize GPU telemetry to `latest.json` → `gpu_telemetry` node.
- [ ] Add IPC command `engine.getCrashReport` to retrieve last crash JSON.

### Phase 3: Nervous System — Structured Telemetry `[MED effort]`
- [ ] Implement subscription-based log filtering over IPC.
- [ ] Add rate limiting (100 msg/s cap) with overflow notification.
- [ ] Implement `query_render_graph` IPC command.
- [ ] Update VS Code extension to auto-subscribe to ERROR/FATAL logs.

### Phase 4: Immune System — Build Verification `[MED effort]`
- [ ] Create `tools/build_and_report.ps1` with JSON error output.
- [ ] Add MCP tool `uz_build` that wraps the build script.
- [ ] Implement retry loop logic in MCP server.

### Phase 5: Extension Integration `[MED effort]`
- [ ] Update VS Code extension to auto-read crash reports on engine disconnect.
- [ ] Extension auto-prompts AI with crash JSON payload.
- [ ] Extension provides UI button "AI: Analyze Current Frame".
- [ ] Extension provides UI button "AI: Diagnose Last Crash".

**Dependencies:** Phase 2 blocks Phase 5. Phases 1, 3, 4 are independent.

---

## 9. Rules for AI Agents

These rules should be added to `AGENTS.md`:

* **RULE DEBUG-1**: When encountering a rendering issue, do not ask the user for a screenshot. Use `uz_screenshot` (MCP) or `tools/capture_frame.ps1` (CLI) to capture and analyze the frame yourself.
* **RULE DEBUG-2**: If the engine crashes, immediately read `${PROJECT_ROOT}/.uzengine/crash_reports/latest.json` before attempting a blind fix.
* **RULE DEBUG-3**: When a shader fails compilation, read the structured error from `DIAG` / `shader.reload` response before editing the `.hlsl`.
* **RULE DEBUG-4**: After any rendering fix, capture a frame and compare with the previous state to verify the fix worked.
* **RULE DEBUG-5**: Never attempt a blind fix for a GPU crash. Always consult DRED telemetry first (`latest.json` → `gpu_telemetry`).
* **RULE DEBUG-6**: When the build fails, use `tools/build_and_report.ps1` to get structured errors. Max 3 retry iterations before escalating to user.
