# Proposal: Process & DLL Management (Phase 4.7.0)

## Why
The engine needs to dynamically load plugins (game.dll hot-reload, render backend DLLs), spawn child processes (asset cooker, shader compiler), and share memory for IPC. Currently DXC DLLs are loaded via raw `LoadLibraryW` calls in RHI code. UE5's `FPlatformProcess` provides `GetDllHandle`/`CreateProc`/`CreatePipe`; Godot has `OS::open_dynamic_library`. This is P2 — required for plugin system and game.dll hot-reload.

## What Changes

### New Systems
1. **DLL loading** — `LoadDll(path)`, `FreeDll(handle)`, `GetDllSymbol(handle, name)` via `IPlatformProcess`
2. **Process creation** — `CreateProcess(exe, args, workDir)` with stdin/stdout pipe capture
3. **Shared memory** — `CreateSharedMemory(name, size)`, `OpenSharedMemory(name)` for IPC
4. **Environment variables** — `GetEnv(name)`, `SetEnv(name, value)`

### Refactoring
- DXC DLL loading in RHI switched to `IPlatformProcess::LoadDll`

## Impact
- Affected specs: `docs/engine/07-platform/`
- Affected code: `runtime/platform/windows/`, RHI DXC loading
- Breaking change: NO — additive, RHI internal refactor
- User benefit: Plugin hot-reload, external tool integration
- Enables: game.dll hot-reload, asset cooker subprocess, plugin system
