# Crash Handling & Diagnostics — Spec Delta

## ADDED Requirements

### Requirement: SEH Exception Handler
The engine MUST install a top-level SEH exception handler via `SetUnhandledExceptionFilter` that captures crash context and writes diagnostics before terminating.

#### Scenario: Access Violation Crash
Given the crash handler is installed
When an access violation occurs (0xC0000005)
Then a MiniDump is written to the crash directory
And a human-readable crash log with stack trace is written
And a JSON crash log is written
And the process terminates after writing all files

### Requirement: MiniDump Generation
The crash handler MUST write a MiniDump file using `MiniDumpWriteDump` with sufficient context for post-mortem debugging.

#### Scenario: MiniDump Contains Thread Info
Given a crash occurs
When the MiniDump is written
Then the dump includes all thread stacks and indirectly referenced memory
And the file is loadable by WinDbg/Visual Studio

### Requirement: Stack Walking
The engine MUST provide a `CaptureStackTrace()` function returning symbolicated stack frames (function name, file, line).

#### Scenario: Symbolicated Stack Trace
Given debug symbols are available
When `CaptureStackTrace(16)` is called
Then up to 16 frames are returned with function names resolved via DbgHelp
And each frame includes file path and line number when available

### Requirement: Crash Context Capture
The crash handler MUST capture engine state (frame number, scene, GPU info, DRED breadcrumbs) alongside the OS exception data.

#### Scenario: GPU Device Removed With DRED
Given DRED is enabled and the GPU triggers device-removed
When the crash handler fires
Then the crash context includes the last N DRED breadcrumb operations
And the GPU adapter name and driver version are recorded

### Requirement: Crash Log Output
The crash handler MUST write both human-readable `.txt` and machine-parseable `.json` logs.

#### Scenario: Crash Log Directory
Given a crash occurs at 2026-03-15 14:30:05
When crash files are written
Then they are placed in `{userData}/UzEngine/crashes/2026-03-15_143005/`
And the directory contains `crash.txt`, `crash.json`, and `minidump.dmp`
