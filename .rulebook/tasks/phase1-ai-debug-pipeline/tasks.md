## 1. Visual Debugging — CLI Wrapper & Golden Images

- [x] 1.1 Create `tools/capture_frame.ps1` — wraps MCP `uz_screenshot`, saves PNG to disk, returns path
- [x] 1.2 Create `tools/golden_test.ps1` — SSIM comparison script (accepts `--TestName`, `--Scene`, `--Threshold`)
- [x] 1.3 Implement SSIM comparison logic (use ImageMagick `compare`)
- [x] 1.4 Define golden baseline directory structure: `assets/test/golden/<test_name>/baseline.png`
- [x] 1.5 On SSIM failure: save diff image to `assets/test/golden/<test_name>/diff.png`
- [x] 1.6 Generate initial baselines for existing test scenes (`default.uzscene`, `materials.uzscene`)
- [x] 1.7 Write guide: `docs/guides/golden-tests.md`

## 2. Visual Debugging — GPU Resource Introspection

- [x] 2.1 Add IPC command `request_texture` — accepts render target name
- [x] 2.2 RHI layer has full readback path: `cmd_copy_texture_to_buffer` + `RHI_BUFFER_USAGE_READBACK` + `buffer_map/unmap`
- [x] 2.3 Expose RT name list via IPC: `query_render_targets` → returns 6 known RTs with formats/size
- [x] 2.4 Add IPC handler wrapping the query commands

## 3. Crash Diagnostics — C++ Crash Handler

- [x] 3.1 Create `CrashReporter` class in `src/engine/core/` (existing location, upgraded)
- [x] 3.2 Register `SetUnhandledExceptionFilter` callback on Windows
- [x] 3.3 On crash: capture exception code and address
- [x] 3.4 On crash: walk stack with `StackWalk64` and symbolicate via `SymFromAddr`
- [x] 3.5 On crash: serialize engine state (active scene, camera, selected node, frame number)
- [x] 3.6 On crash: capture last 100 lines of DIAG log buffer
- [x] 3.7 On crash: query OS and GPU hardware info
- [x] 3.8 On crash: write `${PROJECT_ROOT}/.uzengine/crash_reports/latest.json`
- [x] 3.9 On crash: write `${PROJECT_ROOT}/.uzengine/crash_reports/latest.dmp` via `MiniDumpWriteDump`
- [x] 3.10 Add IPC command `engine.getCrashReport` — returns contents of `latest.json`

## 4. Crash Diagnostics — DX12 DRED

- [x] 4.1 Enable DRED on engine startup: `ID3D12DeviceRemovedExtendedDataSettings1::SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON)`
- [x] 4.2 On `DXGI_ERROR_DEVICE_REMOVED`: query `ID3D12DeviceRemovedExtendedData1`
- [x] 4.3 Extract auto-breadcrumbs (command list name, operation type, index)
- [x] 4.4 Extract page fault info (virtual address, fault type)
- [x] 4.5 Capture active RenderGraph pass name via `rg_get_active_pass_name()` C accessor → `"active_render_pass"` field in `dred_report.json`
- [x] 4.6 Serialize to `dred_report.json` under crash_reports directory
- [x] 4.7 Log DRED summary to stdout before process exit

## 5. Structured Telemetry — Log Streaming

- [x] 5.1 Define severity levels: TRACE, DEBUG, INFO, WARN, ERROR, FATAL
- [x] 5.2 Add IPC command `subscribe_logs` with filter: `{"level": "WARN", "systems": ["RHI"]}`
- [x] 5.3 Each DIAG macro emission also pushes to IPC subscribers matching filter
- [x] 5.4 Implement rate limiter: max 100 messages/second per subscription
- [x] 5.5 On rate limit exceeded: send `{"type": "log_overflow", "dropped_count": N}`
- [x] 5.6 Add IPC command `unsubscribe_logs`

## 6. Structured Telemetry — RenderGraph Introspection

- [x] 6.1 Add IPC command `query_render_graph`
- [x] 6.2 Return: node names, dependencies, texture formats, resolutions
- [x] 6.3 GPU memory usage via `query_gpu_memory` IPC (`IDXGIAdapter3::QueryVideoMemoryInfo` → total/used/budget bytes)
- [x] 6.4 RHI timestamp query API added to vtable (`timestamp_query_heap_create/destroy`, `cmd_timestamp_write`, `timestamp_readback`); DX12 stubs set to NULL

## 7. Build Verification

- [x] 7.1 Create `tools/build_and_report.ps1` — wraps CMake build, parses MSVC output
- [x] 7.2 Parse `/diagnostics:column` output into structured JSON: `{file, line, column, severity, code, message}`
- [x] 7.3 On success: return `{"success": true, "warnings_count": N, "build_time_ms": M}`
- [x] 7.4 On failure: return `{"success": false, "errors": [...], "warnings_count": N}`
- [x] 7.5 Add MCP tool `uz_build` wrapping the build script
- [x] 7.6 MCP tool supports retry parameter (max 3 iterations)

## 8. VS Code Extension Integration

- [x] 8.1 On engine IPC disconnect: auto-check for `latest.json` crash report
- [x] 8.2 If crash report exists: prompt AI with crash payload automatically
- [x] 8.3 Add command `uzengine.analyzeFrame` — triggers `uz_screenshot` and opens AI analysis
- [x] 8.4 Add command `uzengine.diagnoseCrash` — reads `latest.json` and opens AI analysis
- [x] 8.5 Auto-subscribe to ERROR/FATAL logs on IPC connect
- [x] 8.6 Buffer log entries and feed to AI on issue detection

## 9. Agent Rules (AGENTS.md)

- [x] 9.1 Add RULE DEBUG-1: Use `uz_screenshot` instead of asking user for screenshots
- [x] 9.2 Add RULE DEBUG-2: Read `latest.json` before attempting blind crash fix
- [x] 9.3 Add RULE DEBUG-3: Read shader compile errors from `shader.reload` before editing HLSL
- [x] 9.4 Add RULE DEBUG-4: Capture frame after every rendering fix to verify
- [x] 9.5 Add RULE DEBUG-5: Never blind-fix GPU crashes — consult DRED first
- [x] 9.6 Add RULE DEBUG-6: Use `tools/build_and_report.ps1` for structured build errors, max 3 retries

## 10. Testing & Validation

- [x] 10.1 Crash handler test: trigger deliberate null-pointer → `latest.json` generated with valid stack
- [x] 10.2 Crash handler test: `latest.dmp` loadable in Visual Studio
- [ ] 10.3 DRED test: trigger deliberate invalid resource access — requires running GPU
- [ ] 10.4 Golden test: known-good scene → SSIM ≥ 0.98 — requires ImageMagick + running engine
- [ ] 10.5 Golden test: deliberately broken shader → SSIM < 0.98 — requires running engine
- [x] 10.6 Log subscription: subscribe to ERROR → only ERROR+ messages received
- [x] 10.7 Rate limiter: emit 200 logs/s → receive 100 + overflow notification
- [x] 10.8 Build verification: intentional syntax error → structured JSON with file/line/code
- [x] 10.9 Build verification: clean build → `{"success": true}` response
- [ ] 10.10 End-to-end: AI detects crash → reads report → applies fix → rebuilds → captures frame — requires full engine lifecycle

## 11. Crash Report Enrichment (P0 — from UE5/Godot gap analysis)

- [x] 11.1 Add `command_line` field to `latest.json` via `GetCommandLineA()`
- [x] 11.2 Add `memory` stats to `latest.json` via `GlobalMemoryStatusEx()` (physical_total, physical_available, virtual_used)
- [x] 11.3 Add `loaded_modules` list to `latest.json` via `EnumProcessModules()` + `GetModuleFileNameEx()` with version info
- [x] 11.4 Add `CrashReporter::set_metadata(key, value)` for custom KV pairs (à la UE5 `SetGameData`)
- [x] 11.5 Emit `metadata` dict in `latest.json` with all custom KV pairs

## 12. Structured Assertions (P1 — from UE5 ensure/check)

- [x] 12.1 Add `UZ_CHECK(cond)` macro — fatal assertion, writes crash report + calls `abort()`
- [x] 12.2 Add `UZ_CHECK_MSG(cond, fmt, ...)` — same with formatted message
- [x] 12.3 Add `UZ_ENSURE(cond)` macro — non-fatal, writes report + logs + continues execution
- [x] 12.4 Add `UZ_ENSURE_MSG(cond, fmt, ...)` — same with formatted message
- [x] 12.5 Integrate with `CrashReporter::write_report()` for structured output

## 13. Crash Management (P2 — from UE5 dedup/archival)

- [x] 13.1 Generate crash GUID by hashing top 5 stack frames
- [x] 13.2 Archive reports as `crash_{hash}_{timestamp}.json` (keep `latest.json` as copy)
- [x] 13.3 Wire `set_pre_crash_callback` to flush DIAG log buffer and pending IPC writes
