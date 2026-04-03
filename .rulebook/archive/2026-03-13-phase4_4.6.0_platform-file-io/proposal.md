# Platform File I/O

## Why

The engine needs a unified file I/O API that wraps Win32 file operations (CreateFileW, ReadDirectoryChangesW) behind a clean interface, enabling memory-mapped files, file watching for hot-reload, and platform path resolution.

## Summary

Implement FileSystem API (read/write/exists/enumerate/delete), MemoryMappedFile, FileWatcher with ReadDirectoryChangesW, and platform path utilities (GetUserDataDir, GetAppDataDir, GetTempDir).

## Impact

- **Core**: All file access routes through this API
- **Hot-reload**: FileWatcher enables shader/script/asset hot-reload
- **Asset pipeline**: Memory-mapped files for large asset loading

## Acceptance Criteria

1. FileReadAll/FileWriteAll round-trip binary and text correctly
2. MemoryMappedFile reads match normal reads
3. FileWatcher detects creation/modification/deletion events
4. Platform paths return valid, existing directories
5. 95%+ test coverage (64 tests)
