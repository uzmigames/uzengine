## 1. Implementation
- [x] 1.1 Define file I/O API (FileReadAll, FileWriteAll, FileExists, FileGetSize, DirectoryEnumerate, FileDelete, DirectoryCreate) <!-- file_system.h -->
- [x] 1.2 Implement `WindowsFile` using `CreateFileW` / `ReadFile` / `WriteFile` <!-- file_system.cpp -->
- [ ] 1.3 Implement async file read via overlapped I/O with completion callback <!-- deferred: not blocking -->
- [x] 1.4 Implement `MemoryMappedFile` class wrapping `CreateFileMapping` + `MapViewOfFile` <!-- memory_mapped_file.h/.cpp -->
- [x] 1.5 Implement `FileWatcher` using `ReadDirectoryChangesW` on a background thread <!-- file_watcher.h/.cpp -->
- [x] 1.6 Add `FileWatchEvent` struct (path, eventType: Created/Modified/Deleted/Renamed) <!-- file_watcher.h -->
- [x] 1.7 Implement platform paths: `GetUserDataDir()`, `GetAppDataDir()`, `GetTempDir()` <!-- file_system.h/.cpp -->
- [x] 1.8 Integrate existing `uz::GetExeDir()` — already in core/paths, file_system uses same pattern <!-- no change needed -->
- [x] 1.9 Add directory enumeration with glob pattern support (`DirectoryEnumerate` + `DirectoryEnumerateRecursive`) <!-- file_system.cpp -->
- [ ] 1.10 Replace all `std::ifstream`/`std::ofstream` in engine code with file_system API <!-- separate migration task -->

## 2. Testing
- [x] 2.1 Test file read/write round-trip (binary + text) <!-- 37 FileSystemTest cases -->
- [x] 2.2 Test `FileExists()` returns true/false correctly
- [x] 2.3 Test `FileGetSize()` returns correct byte count
- [x] 2.4 Test directory enumeration with glob filter + recursive
- [x] 2.5 Test memory-mapped file read matches normal read <!-- 10 MemoryMappedFileTest cases -->
- [ ] 2.6 Test async file read delivers data via callback <!-- blocked on 1.3 -->
- [x] 2.7 Test FileWatcher detects file creation via callback <!-- 17 FileWatcherTest cases -->
- [x] 2.8 Test platform paths return non-empty, existing directories
- [ ] 2.9 Test path with Unicode characters <!-- not yet covered -->
- [ ] 2.10 Verify 95%+ coverage on all new code
- [x] 2.11 Build succeeds (zig build — 0 errors)

## 3. Documentation
- [x] 3.1 Update `docs/engine/07-platform/07-file-io.md` <!-- 560 lines, complete API docs -->
- [x] 3.2 Update `docs/engine/07-platform/08-file-watcher.md` <!-- 316 lines, thread safety, usage patterns -->
