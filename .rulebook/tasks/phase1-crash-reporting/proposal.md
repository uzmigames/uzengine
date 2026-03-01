# Phase 1 — Crash Reporting & Error Telemetry

## Context

When an engine or game crashes, developers need to understand **why**. Without crash reporting:
- Crashes are silent or unexplained (just "program has stopped working")
- Root cause is invisible (was it memory corruption? stack overflow? null dereference?)
- Reproduction requires re-running the exact same steps (luck-based debugging)
- AI-assisted debugging is impossible (Claude can't see the crash data)

A **comprehensive crash reporting system** captures:
- **Windows minidump** → full memory snapshot for debugger inspection
- **Call stack** → which function crashed and how it got there
- **Exception code** → ACCESS_VIOLATION, STACK_OVERFLOW, DIVIDE_BY_ZERO, etc.
- **System info** → OS, CPU, GPU, RAM (to debug platform-specific issues)
- **Engine state** → active scene, memory usage, resource counts
- **Lua state** (if applicable) → error message, script stack trace
- **Recent IPC commands** → what operations preceded the crash
- **Log tail** → last 1000 lines of engine.log

This data is packaged into a **ZIP file** and saved to disk. The VSCode extension detects crashes and notifies the developer.

## Goal

Implement a **crash reporting system** with:
- **SEH handler** (Windows): Intercept unhandled exceptions
- **Minidump capture**: Windows MiniDumpWriteDump for memory snapshot
- **Crash JSON log**: Structured crash data (timestamp, exception code, callstack, system info, engine state)
- **Log tail**: Last 1000 lines of engine.log
- **System info**: OS version, CPU, GPU, RAM
- **Engine state snapshot**: Scene, memory, resources, Lua state
- **ZIP packaging**: All data in one compressed file
- **IPC integration**: `engine.get_last_crash` handler for editor notification
- **Lua state capture**: Error message, stack trace, active scripts
- **IPC history**: Last 10 executed IPC commands
- **Build config awareness**: Disable minidump in Shipping builds (optional)

## Why Phase 1?

Crash reporting is a **day-1 prerequisite** for development:
- Phase 0–1 work will produce crashes; need to debug them
- AI-assisted development (Claude running commands via IPC) needs crash logs for root cause analysis
- Game developers using UzEngine will encounter crashes; need way to report them
- Early adoption prevents hard-to-diagnose crashes from accumulating

## Dependencies

- **Phase 0** (IPC/MCP server): ✅ Complete (needs minidump IPC handler)
- **Windows API**: dbghelp.h (MiniDumpWriteDump), Windows.h (SEH)
- **External libs**: None (standard Windows APIs)

## Spec

[docs/specs/crash-reporting.md](../../../docs/specs/crash-reporting.md)

