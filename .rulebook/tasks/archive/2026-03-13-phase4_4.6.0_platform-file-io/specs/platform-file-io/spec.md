# Platform File I/O — Spec Delta

## ADDED Requirements

### Requirement: IPlatformFile Interface
The engine MUST define an `IPlatformFile` vtable interface abstracting all file operations. Engine code above the platform layer SHALL NEVER use `std::ifstream`, `std::ofstream`, `fopen`, or OS file APIs directly.

#### Scenario: File Read
Given a file `test.txt` exists with content "hello"
When `file->ReadAll("test.txt")` is called
Then a byte buffer containing "hello" is returned

#### Scenario: File Not Found
Given no file at path `missing.txt`
When `file->ReadAll("missing.txt")` is called
Then an empty buffer is returned
And an error is logged with the resolved path

### Requirement: Async File I/O
The platform file system MUST support asynchronous read operations that do not block the calling thread.

#### Scenario: Async Read Completion
Given a 10MB file on disk
When `file->ReadAsync("large.bin", callback)` is called
Then the function returns immediately
And `callback(buffer)` is invoked on completion with the file contents

### Requirement: Memory-Mapped Files
The platform MUST support memory-mapping files for zero-copy read access to large files.

#### Scenario: Map Large File
Given a 100MB file on disk
When `MemoryMappedFile::Open("large.pak")` is called
Then a pointer to the mapped region is returned
And reading bytes at any offset does not require additional I/O calls

### Requirement: File Watcher
The platform MUST support watching directories for file changes and delivering events via callback.

#### Scenario: Shader File Modified
Given a FileWatcher watching `shaders/` directory
When `shaders/pbr.hlsl` is saved externally
Then `OnFileChanged(path, FileWatchEvent::Modified)` callback fires within 100ms

### Requirement: Platform Paths
The platform MUST provide functions for standard OS directories.

#### Scenario: User Data Directory
Given a Windows system with user "Bolado"
When `GetUserDataDir()` is called
Then a path like `C:/Users/Bolado/AppData/Local/UzEngine/` is returned
And the directory exists (created if needed)
