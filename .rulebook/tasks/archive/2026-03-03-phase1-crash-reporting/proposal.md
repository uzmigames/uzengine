# Proposal: phase1-crash-reporting

## Why

UzEngine currently has no crash handling. When the engine or game crashes, the process silently exits with a Windows error dialog and all runtime state is lost. Developers have no call stack, no exception code, and no snapshot of what the engine was doing — making root-cause analysis luck-based and time-consuming.

This problem compounds with AI-assisted development. When Claude drives the engine via IPC (scene generation, shader assist, script generation), a crash during an AI-initiated operation produces no actionable data. The only option is to reproduce manually, which undermines the entire AI-native workflow.

Crash reporting via Windows Structured Exception Handling (SEH) and `MiniDumpWriteDump` is the industry-standard solution on Windows. It captures the full call stack, exception code, module list, and memory at the moment of crash, packaged alongside a structured JSON log of engine state. The VSCode extension monitors for this output and notifies the developer immediately, closing the feedback loop between crash and root cause.

## What Changes

- Register `SetUnhandledExceptionFilter` in `src/engine/crash/crash_handler.c` at engine startup — intercepts all unhandled SEH exceptions before the OS dialog appears; handler runs on the faulting thread with a statically allocated 64 KB emergency stack to avoid stack-overflow scenarios
- Call `MiniDumpWriteDump` in the SEH handler — writes a minidump to `%APPDATA%/UzEngine/crashes/<timestamp>.dmp` using `MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithThreadInfo`; links against `dbghelp.lib`
- Write a JSON crash log in `src/engine/crash/crash_json.c` — fields: `timestamp`, `exception_code` (hex), `exception_address`, `module_name`, `call_stack[]` (symbol + offset), `system_info` (OS version, CPU, GPU, RAM), `engine_state` (active scene path, arena usage, resource counts), `lua_state` (error message and Lua call stack if Lua was active), `ipc_history` (last 10 IPC commands executed), `log_tail` (last 1000 lines of `engine.log`)
- Add a ring buffer in `src/engine/ipc/ipc_history.c` — capacity 10, stores the last IPC command name and args as JSON strings; the crash handler reads this without allocating
- Add Lua state capture in `src/engine/crash/crash_lua.c` — on crash, calls `lua_getinfo` to walk the Lua call stack and writes each frame (source, line, function name) into the crash JSON; safely no-ops if Lua was not initialized
- Compress crash artifacts in `src/engine/crash/crash_zip.c` — packages `.dmp` + `.json` + `engine.log` into a single `<timestamp>.zip` using miniz (single-header, already in vcpkg)
- Add IPC handler `engine.get_last_crash` in `src/engine/ipc/ipc_handlers.c` — returns the path to the most recent crash ZIP and a summary of the crash JSON fields
- Add VSCode extension crash watcher in `vscode-extension/src/crashWatcher.ts` — polls `%APPDATA%/UzEngine/crashes/` for new ZIP files; shows a VS Code notification with "Open Crash Log" button that opens the JSON in a new editor tab
- Guard minidump generation behind `#ifndef UZ_BUILD_SHIPPING` — Shipping builds still write the JSON log but skip the `.dmp` file to avoid shipping debug symbols

## Impact

- Affected specs: `docs/specs/crash-reporting.md`
- Affected code: `src/engine/crash/`, `src/engine/ipc/ipc_handlers.c`, `src/engine/ipc/ipc_history.c`, `vscode-extension/src/crashWatcher.ts`
- Breaking change: NO
- User benefit: Every crash produces a structured report with call stack, engine state, and Lua context; the VSCode extension surfaces it immediately; Claude can retrieve crash data via IPC to assist with root-cause analysis
