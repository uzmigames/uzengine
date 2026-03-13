# Proposal: Platform File I/O (Phase 4.6.0)

## Why
The engine currently uses `std::ifstream`/`std::ofstream` scattered across code with no abstraction for async I/O, memory-mapped files, or file watching. Asset hot-reload requires `ReadDirectoryChangesW` (Windows) / `inotify` (Linux); streaming requires async overlapped I/O; shader cache requires memory-mapped access. UE5's `IPlatformFile` vtable and Godot's `FileAccess` hierarchy both abstract this. This is P1 — every subsystem (assets, shaders, config, scenes) depends on file I/O.

## What Changes

### New Systems
1. **IPlatformFile interface** — vtable with Read, Write, Exists, GetSize, Enumerate, MMap, Watch
2. **WindowsFile** — implementation using `CreateFileW`, `ReadFile`, overlapped I/O for async
3. **MemoryMappedFile** — `CreateFileMapping` + `MapViewOfFile` wrapper for large file access
4. **FileWatcher** — `ReadDirectoryChangesW` with callback on file create/modify/delete
5. **PlatformPaths** — `GetUserDataDir()`, `GetAppDataDir()`, `GetTempDir()`, `GetExeDir()` (extend existing)

### Refactoring
- Replace all `std::ifstream`/`std::ofstream` usage with `IPlatformFile` calls
- Existing `uz::GetExeDir()` integrated into `PlatformPaths`

## Impact
- Affected specs: `docs/engine/07-platform/`
- Affected code: `runtime/engine/core/paths/`, all asset loading code
- Breaking change: YES — std:: file calls replaced with engine API
- User benefit: Async file I/O, hot-reload, memory-mapped large files
- Enables: Asset hot-reload (shaders, textures), streaming, VFS
