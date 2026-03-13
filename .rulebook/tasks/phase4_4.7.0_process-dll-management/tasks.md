## 1. Implementation
- [x] 1.1 Implement `LoadDll(path)` returning `NativeDllHandle` via `LoadLibraryW` <!-- dll_loader.h/.cpp -->
- [x] 1.2 Implement `FreeDll(handle)` via `FreeLibrary`
- [x] 1.3 Implement `GetDllSymbol(handle, name)` returning `void*` via `GetProcAddress`
- [x] 1.4 Implement `ProcessCreate(exe, args, workDir)` returning process handle + pipes <!-- process.h/.cpp -->
- [x] 1.5 Implement stdout/stderr pipe reading from child process (non-blocking via PeekNamedPipe)
- [x] 1.6 Implement `ProcessWait(handle, timeout)` and `ProcessTerminate(handle)`
- [x] 1.7 Implement `SharedMemoryCreate(name, size)` via `CreateFileMappingW` + `MapViewOfFile` <!-- shared_memory.h/.cpp -->
- [x] 1.8 Implement `SharedMemoryOpen(name)` for cross-process memory access
- [x] 1.9 Implement `GetEnv(name)` and `SetEnv(name, value)` <!-- environment.h/.cpp -->
- [ ] 1.10 Refactor RHI DXC loading to use `LoadDll` <!-- separate refactor task -->

## 2. Testing
- [x] 2.1 Test DLL load/symbol/free cycle with a test DLL
- [x] 2.2 Test `GetDllSymbol` returns null for non-existent symbol
- [x] 2.3 Test `LoadDll` returns null handle for non-existent path (no crash)
- [x] 2.4 Test process creation with stdout pipe capture (spawn `cmd /c echo hello`)
- [x] 2.5 Test shared memory write in one mapping is visible in another
- [x] 2.6 Test environment variable set/get round-trip
- [ ] 2.7 Verify 95%+ coverage on all new code
- [x] 2.8 Build succeeds (zig build — 0 errors)

## 3. Documentation
- [x] 3.1 Create `docs/engine/07-platform/09-process-dll.md`
