## 1. Implementation
- [x] 1.1 Implement `SetUnhandledExceptionFilter` handler at engine startup <!-- crash_handler.cpp: InstallCrashHandler() -->
- [x] 1.2 Implement `MiniDumpWriteDump` writing `.dmp` to crash directory <!-- WriteMiniDumpFile() in crash_handler.cpp -->
- [x] 1.3 Implement stack walker using `CaptureStackBackTrace` + DbgHelp `SymFromAddr`/`SymGetLineFromAddr64` <!-- stack_walker.h/.cpp -->
- [x] 1.4 Define `CrashContext` struct (frameNumber, sceneName, gpuAdapter, lastRhiOp, gpuDeviceRemoved, exceptionCode, exceptionAddress) <!-- crash_handler.h -->
- [x] 1.5 Populate `CrashContext` from SEH `EXCEPTION_POINTERS` and engine globals <!-- PopulateCrashContext() -->
- [x] 1.6 Write human-readable crash log (`.txt`) with stack trace, context, OS/GPU info <!-- WriteCrashLogTxt() -->
- [x] 1.7 Write machine-parseable crash log (`.json`) with same data structured for tooling <!-- WriteCrashLogJson() -->
- [ ] 1.8 Wire DRED breadcrumbs from RHI into crash context on `DXGI_ERROR_DEVICE_REMOVED` <!-- deferred: needs RHI DRED refactor -->
- [x] 1.9 Create crash output directory at `{exe_dir}/crashes/{timestamp}/` <!-- CreateCrashDirectory() -->
- [ ] 1.10 Update `UZ_CHECK`/`UZ_ENSURE` to capture stack trace in failure message <!-- separate task -->
- [x] 1.11 Add `InstallCrashHandler()` / `UninstallCrashHandler()` to platform API <!-- crash_handler.h -->

## 2. Testing
- [x] 2.1 Test stack walker produces valid symbolicated frames for known function <!-- 29 tests in test_crash_handler.cpp -->
- [x] 2.2 Test `CrashContext` struct contract and state mutators (14 tests)
- [x] 2.3 Test FormatStackTrace with various inputs (7 tests)
- [x] 2.4 Test crash handler lifecycle: install/uninstall/idempotent (4 tests)
- [ ] 2.5 Test `UZ_CHECK` failure includes stack trace in log output <!-- blocked on 1.10 -->
- [ ] 2.6 Test MiniDump file is valid (loadable by WinDbg — manual verification)
- [ ] 2.7 Verify 95%+ coverage on all new code

## 3. Documentation
- [x] 3.1 Update `docs/engine/07-platform/06-crash-handling.md` with implementation details <!-- 648 lines, complete -->
- [x] 3.2 Crash handler integration guide included in 3.1 (installation, per-frame updates, exception safety)
