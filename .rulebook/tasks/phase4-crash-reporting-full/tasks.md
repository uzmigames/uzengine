## Context

Extends `phase1-crash-reporting` (basic signal handler + plain-text report).
Adds full crash telemetry: minidump, ZIP packaging, IPC handler, system info, engine state.

## 1. Minidump Generation (Windows)

- [ ] 1.1 Add `DbgHelp.lib` to Windows link libraries in CMakeLists.txt
- [ ] 1.2 `windows_minidump.cpp` — wrap `MiniDumpWriteDump()` with full heap + module dump type
- [ ] 1.3 Write `.dmp` file alongside crash report directory
- [ ] 1.4 `UZ_MINIDUMP` CMake option (default ON, disable in Shipping)
- [ ] 1.5 Tests: verify minidump file created, readable by WinDbg/VS

## 2. ZIP Crash Package

- [ ] 2.1 Add `zlib` or `libzip` to `vcpkg.json`
- [ ] 2.2 `crash_packaging.cpp` — create ZIP from: `crash.txt`, `crash.dmp`, `engine.log` tail (1000 lines)
- [ ] 2.3 Output path: `{output_dir}/crash_{timestamp}.zip`
- [ ] 2.4 Cleanup: keep last 50 ZIPs, delete older than 30 days (run on startup)
- [ ] 2.5 Tests: ZIP created, structure correct, compression working

## 3. System Info Capture

- [ ] 3.1 `system_info.h/cpp` — OS name/version/build, CPU brand/cores/freq, GPU name/VRAM, RAM total/available
- [ ] 3.2 Linux: `/proc/cpuinfo`, `/proc/meminfo`, `uname()`; Windows: CPUID, `GlobalMemoryStatusEx`, DXGI adapter
- [ ] 3.3 Include `system_info` section in crash report
- [ ] 3.4 Tests: all fields populated, values reasonable

## 4. Engine State Snapshot in Report

- [ ] 4.1 Extend `CrashReporter::write_report()` to include engine state callback
- [ ] 4.2 State includes: scene path, node count, arena usage (frame/level/permanent), active viewport count
- [ ] 4.3 `set_engine_state_callback(fn)` — registered at engine startup
- [ ] 4.4 Tests: state section present in report, callback invoked

## 5. IPC Command History

- [ ] 5.1 `ipc_history.h/cpp` — ring buffer of last 10 IPC commands (name + timestamp)
- [ ] 5.2 Hook into `IpcRegistry::dispatch()` to record each call
- [ ] 5.3 `IpcHistory::snapshot()` → include in crash report
- [ ] 5.4 Tests: history records correct, wraps at 10, included in report

## 6. `engine.get_last_crash` IPC Handler

- [ ] 6.1 Register handler in main IPC server
- [ ] 6.2 Returns: `{ok, crash_path, crash_data:{timestamp, signal, stack_frames}}`
- [ ] 6.3 Returns `{ok:false, error:"No crash report found"}` when none exists
- [ ] 6.4 Tests: handler callable, returns correct structure

## 7. VSCode Extension Crash Notification

- [ ] 7.1 Extension calls `engine.get_last_crash` on connection
- [ ] 7.2 If crash found: show VSCode notification with "Open Report" button
- [ ] 7.3 "Open Report" opens crash ZIP in OS file explorer
- [ ] 7.4 Tests: notification shown, button triggers correct action

## 8. Lua State Capture (coordinate with phase2-lua-scripting)

- [ ] 8.1 `lua_crash_info.h/cpp` — capture Lua error message + stack trace via `lua_getstack`/`lua_getinfo`
- [ ] 8.2 List active loaded scripts
- [ ] 8.3 Include `[lua]` section in crash report when Lua VM is active
- [ ] 8.4 Tests: Lua error captured, stack trace correct

## 9. Tests

- [ ] 9.1 All new modules have ≥95% line + branch coverage
- [ ] 9.2 No regressions in existing 31 crash reporter tests
