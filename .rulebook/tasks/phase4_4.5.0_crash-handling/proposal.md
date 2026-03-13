# Proposal: Crash Handling & Diagnostics (Phase 4.5.0)

## Why
When the engine crashes, developers currently get a generic Windows error dialog with no useful information. Production engines capture crash context: stack trace, GPU state (DRED), frame number, active scene, and write a minidump + human-readable crash log. UE5's crash reporter captures SEH exceptions + MiniDump + DRED breadcrumbs; Godot captures SEH with `CrashHandlerException` writing to log. This is P1 — without it, GPU crashes on user machines are undiagnosable.

## What Changes

### New Systems
1. **SEH handler** — `SetUnhandledExceptionFilter` capturing access violations, stack overflows, etc.
2. **MiniDump writer** — `MiniDumpWriteDump` with `MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory`
3. **Stack walker** — `CaptureStackBackTrace` + DbgHelp `SymFromAddr` for symbolicated traces
4. **Crash context** — struct capturing frame number, active scene, GPU adapter, last RHI operation, DRED state
5. **Crash log** — human-readable `.txt` + machine-parseable `.json` written to crash directory
6. **DRED integration** — wire existing RHI DRED data into crash context on GPU device-removed

### Refactoring
- `UZ_CHECK`/`UZ_ENSURE` macros updated to capture stack trace before crashing

## Impact
- Affected specs: `docs/engine/04-observability/05-crash-reporting.md`
- Affected code: `runtime/platform/windows/`, `runtime/engine/core/`
- Breaking change: NO — additive
- User benefit: Actionable crash reports with stack traces and GPU state
- Enables: Remote crash reporting, QA bug filing with crash dumps
