## 1. CrashReporter Class (`src/engine/core/crash_reporter.h/.cpp`)

- [x] 1.1 `CrashReporter::install(output_dir)` — installs signal/exception handlers at startup
- [x] 1.2 `CrashReporter::uninstall()` — restores previous handlers (for tests)
- [x] 1.3 `CrashReporter::set_output_dir(dir)` / `output_dir()` — configurable report directory
- [x] 1.4 `CrashReporter::write_report(reason, details)` — manual crash report, returns file path
- [x] 1.5 `CrashReporter::is_installed()` — query installation state
- [x] 1.6 `CrashReporter::last_report_path()` — path to most recent written report
- [x] 1.7 Pre-crash callback support via `set_pre_crash_callback(fn)`

## 2. Platform Backends

- [x] 2.1 Linux: `sigaction()` with `SA_SIGINFO|SA_RESETHAND` for SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS
- [x] 2.2 Linux: async-signal-safe write via `open()/write()/close()` (no malloc in signal handler)
- [x] 2.3 Linux: `backtrace()` for stack trace capture (up to 64 frames)
- [x] 2.4 Windows: `SetUnhandledExceptionFilter` stub (compiles, no DX12 dep)

## 3. Crash Report Format (plain-text)

- [x] 3.1 `[crash]` section: timestamp, signal name, fault address
- [x] 3.2 `[stack]` section: hex addresses (symbolic if backtrace resolves)
- [x] 3.3 `[engine]` section: version, build config

## 4. Tests (`tests/core/test_crash_reporter.cpp`)

- [x] 4.1 31 tests: install/uninstall, output_dir, write_report, is_installed, last_report_path, pre-crash callback, signal handler (SIGABRT via kill), report file contents
- [x] 4.2 All 31 tests pass — 1117/1117 total after integration

## 5. CMakeLists.txt Integration

- [x] 5.1 `src/engine/core/crash_reporter.cpp` added to `UzEngineTests`
- [x] 5.2 `tests/core/test_crash_reporter.cpp` added to `UzEngineTests`

## Deferred (from original proposal — Phase 4+)

- [ ] D1. Minidump generation (`MiniDumpWriteDump`) — Windows only, Phase 4+
- [ ] D2. ZIP packaging of crash report + log tail — Phase 4+
- [ ] D3. `engine.get_last_crash` IPC handler — Phase 4+
- [ ] D4. System info capture (CPU/GPU/RAM) — Phase 4+
- [ ] D5. Engine state snapshot in report — Phase 4+
- [ ] D6. Lua state capture — Phase 2 (scripting)
- [ ] D7. IPC command history tracking — Phase 4+
- [ ] D8. VSCode extension crash notification UI — Phase 4+
