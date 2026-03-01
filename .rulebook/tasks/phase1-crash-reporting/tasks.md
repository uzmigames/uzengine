# Phase 1 — Crash Reporting & Error Telemetry: Implementation Tasks

## Deliverables

- `CrashReportingSystem` — Exception handler installation and crash capture
- Crash report directory structure — `%APPDATA%/UzEngine/<project>/crashes/`
- `crash.json` — Structured crash data (metadata, exception, callstack, system info, engine state)
- `crash.dmp` — Windows minidump (full memory snapshot)
- `engine.log` — Engine log tail (last 1000 lines)
- `state.json` — Engine state snapshot (scene, memory, resources)
- IPC handler — `engine.get_last_crash` for editor notification
- System info capture — OS, CPU, GPU, RAM
- Engine state capture — Scene, memory, resources, Lua state
- IPC command history — Last 10 executed commands
- Lua crash info — Error message, stack trace, active scripts
- Test coverage: 80%+

---

## Task Breakdown

### 1. Exception Handler Installation

**Responsibility**: Install global exception handler at engine startup

**API**:
```cpp
// src/core/crash_reporting.h

namespace crash_reporting {

// Install handlers at engine startup
void install_handler();

// Manually trigger crash report (for fatal errors)
void report_fatal_error(std::string_view format, ...);

// Get path to last crash report
std::optional<std::filesystem::path> get_last_crash_path();

// Read and parse crash.json from a crash report
std::optional<json> read_crash_json(const std::filesystem::path& crash_zip);

}  // namespace crash_reporting
```

**Subtasks**:
1. Windows SEH handler
   - Install via `SetUnhandledExceptionFilter()`
   - Intercept access violations, divide-by-zero, stack overflow, etc.

2. C++ std::terminate handler
   - Install via `std::set_terminate()`
   - Catch C++ exceptions that aren't caught elsewhere

3. Handler execution
   - On exception: trigger crash capture process
   - Call `capture_crash(exception_info)`
   - Exit process (don't try to recover)

4. Last crash tracking
   - Write crash path to a known location for IPC handler to find
   - Format: `%APPDATA%/UzEngine/<project>/crashes/crash_<timestamp>.zip`

**Files**:
- `src/core/crash_reporting.h/cpp` — Main implementation
- `src/platform/windows_crash.cpp` — Windows-specific SEH code
- `src/core/crash_reporting_test.cpp` — Unit tests

**Acceptance criteria**:
- Exception handler installs without errors
- Handler invoked on segfault, div-by-zero, etc.
- Crash report file created
- Last crash path queryable via `get_last_crash_path()`

---

### 2. Crash Data Capture

**Responsibility**: Gather all crash information into structured data

**Subtasks**:
1. Exception information
   - Exception code (0xC0000005 = ACCESS_VIOLATION, etc.)
   - Exception address (where the fault occurred)
   - Exception flags
   - Parameters (architecture-specific)

2. Call stack unwinding
   - Use Windows StackWalk64 API
   - For each frame:
     - Get address, module name, symbol name
     - Resolve file and line number (if symbols available)
     - Format as human-readable backtrace

3. System information
   - OS: name, version, build number, architecture (x86/x64)
   - CPU: brand, model, cores, frequency
   - GPU: vendor, name, VRAM
   - Memory: total, available

4. Engine state
   - Active scene path
   - Node count in scene
   - Memory usage (frame/level/permanent arenas)
   - Renderer stats (draw calls, triangles, GPU memory)
   - Lua heap size, active scripts

5. IPC history
   - Last 10 executed IPC commands
   - Timestamps, command name, arguments
   - Helps understand what operations preceded the crash

6. Lua state capture
   - If crash in Lua code: error message and stack trace
   - Active scripts
   - Lua version

7. Log tail
   - Read last 1000 lines from engine.log
   - Include in crash report

**Files**:
- `src/core/crash_capture.h/cpp` — Crash data structures and capture functions
- `src/platform/windows_stack_walk.cpp` — StackWalk64 wrapper
- `src/core/system_info.h/cpp` — System info querying
- `src/core/memory_tracking.h/cpp` — Memory snapshot
- `src/scripting/lua_crash_info.h/cpp` — Lua state capture
- `src/core/ipc_history.h/cpp` — IPC command history tracking

**Acceptance criteria**:
- All crash fields populated correctly
- Call stack unwinds properly (resolves function names)
- System info matches actual hardware
- Engine state snapshot accurate
- Lua error captured (if applicable)
- IPC history correct

---

### 3. Minidump Generation

**Responsibility**: Write Windows minidump file

**Subtasks**:
1. MiniDumpWriteDump API
   - Include necessary DLLs (DbgHelp.lib)
   - Configure dump type (full process heap + modules)

2. Minidump file writing
   - Write to `%APPDATA%/.../crash_<timestamp>.dmp`
   - Handle file I/O errors gracefully

3. Symbol loading (optional)
   - Load symbol files (`.pdb`) for better debug info
   - Use SymInitialize, SymEnumModules, etc.

4. Build config awareness
   - CMake option: `UZ_MINIDUMP` (default ON)
   - Disable in Shipping builds if desired
   - Still write JSON + log even without minidump

**Files**:
- `src/platform/windows_minidump.cpp` — MiniDumpWriteDump wrapper
- CMake flag `UZ_MINIDUMP`

**Acceptance criteria**:
- Minidump file created
- File is readable by Visual Studio debugger
- Symbols loaded (if .pdb available)
- Option to disable minidump in Shipping builds

---

### 4. Crash Report Packaging

**Responsibility**: Create ZIP file with all crash data

**Subtasks**:
1. ZIP structure
   - `crash.json` — Structured data
   - `crash.dmp` — Minidump (if enabled)
   - `engine.log` — Log tail
   - `state.json` — Engine state snapshot

2. JSON serialization
   - `crash.json` contains all structured data
   - Use nlohmann/json (already a dependency)
   - Format clearly for manual inspection

3. ZIP compression
   - Use standard ZIP library (e.g., zlib, libzip)
   - Compression ratio ~5–10x
   - File path: `%APPDATA%/UzEngine/<project>/crashes/crash_<timestamp>.zip`

4. File cleanup (optional)
   - Keep last 50 crash reports
   - Delete reports older than 30 days
   - Run on engine startup

**Files**:
- `src/core/crash_packaging.h/cpp` — ZIP creation and management
- vcpkg dependency: zlib (or equivalent)

**Acceptance criteria**:
- ZIP file created correctly
- All crash data included
- ZIP readable by standard tools
- Compression working
- Cleanup prevents unbounded disk usage

---

### 5. IPC Handler: `engine.get_last_crash`

**Responsibility**: Expose crash data to VSCode editor

**Handler signature**:
```cpp
ipc_registry_.register("engine.get_last_crash", [](const json& args) {
    if (auto crash = crash_reporting::get_last_crash_path()) {
        auto crash_data = crash_reporting::read_crash_json(*crash);
        return json{
            {"ok", true},
            {"crash_path", crash->string()},
            {"crash_data", crash_data}
        };
    } else {
        return json{{"ok", false}, {"error", "No crash report found"}};
    }
});
```

**Subtasks**:
1. IPC handler registration
   - Register `engine.get_last_crash` in main IPC server
   - Return crash path and structured data

2. Extension notification
   - Extension calls IPC on engine startup
   - If crash found: show notification
   - User can open crash report in editor

3. Crash report UI
   - VSCode extension shows crash.json in a readable format
   - "Open Report" button opens ZIP in file explorer
   - Click on crash.json to view structured data

**Files**:
- IPC handler in main server code
- Integration with `crash_reporting` namespace

**Acceptance criteria**:
- IPC handler callable from extension
- Returns crash data in JSON format
- Extension can parse and display crash info
- User can navigate to crash ZIP file

---

### 6. System Info Capture

**Responsibility**: Query and capture OS, CPU, GPU, memory info

**API**:
```cpp
// src/core/system_info.h

struct SystemInfo {
    struct {
        std::string name;
        int version_major, version_minor;
        int build;
        bool is_64bit;
    } os;

    struct {
        std::string brand;
        std::string model;
        int physical_cores, logical_cores;
        float frequency_ghz;
    } cpu;

    struct {
        std::string vendor;
        std::string name;
        uint64_t vram_bytes;
    } gpu;

    struct {
        uint64_t total_bytes, available_bytes;
    } memory;
};

SystemInfo get_system_info();
```

**Subtasks**:
1. OS info (Windows)
   - `GetVersionEx()` or RegQuery for Win 10/11 version
   - Build number
   - Architecture (x86 vs x64)

2. CPU info
   - CPUID instruction
   - Brand string (Intel, AMD, etc.)
   - Model
   - Cores and frequency

3. GPU info
   - From DX12 device (adapter name, VRAM from DXGI)
   - Fallback to Windows API

4. Memory info
   - `GlobalMemoryStatusEx()` for total/available RAM

**Files**:
- `src/core/system_info.h/cpp` — Cross-platform interface
- `src/platform/windows_system_info.cpp` — Windows implementation

**Acceptance criteria**:
- All system info fields populated
- Values match actual hardware
- No crashes on varied hardware

---

### 7. Engine State Snapshot

**Responsibility**: Capture engine state at crash time

**Subtasks**:
1. Scene state
   - Active scene path
   - Node count
   - Loaded asset count

2. Memory snapshot
   - Frame arena: allocated, capacity, high water mark
   - Level arena: allocated, capacity
   - Permanent arena: allocated, capacity
   - Lua heap bytes
   - GPU memory usage

3. Renderer stats
   - Draw calls
   - Triangle count
   - GPU memory allocation

4. Resource counts
   - Textures loaded
   - Meshes loaded
   - Materials loaded
   - Shaders loaded
   - Audio sources active
   - Particle emitters
   - Physics bodies

**Files**:
- `src/core/engine_state_snapshot.h/cpp` — Snapshot capture
- Call from crash handler

**Acceptance criteria**:
- Snapshot captures accurate state
- JSON serialization clean
- Performance impact minimal (snapshot is one-time at crash)

---

### 8. Lua State Capture

**Responsibility**: Capture Lua error info when crash in Lua code

**Subtasks**:
1. Error message capture
   - If crash in Lua: call `lua_tostring(L, -1)` to get error
   - Format and include in crash report

2. Stack trace
   - `lua_getstack()` and `lua_getinfo()` to walk Lua stack
   - For each frame: file, line, function name
   - Format as readable traceback

3. Active scripts
   - List all loaded Lua scripts
   - File paths

4. Lua version
   - Record LuaJIT version for diagnostic purposes

**Files**:
- `src/scripting/lua_crash_info.h/cpp` — Lua state capture
- Integration with crash handler

**Acceptance criteria**:
- Lua error message captured
- Stack trace unwinds correctly
- Active scripts listed
- JSON format clean

---

### 9. IPC Command History

**Responsibility**: Track recent IPC commands for crash context

**Subtasks**:
1. History ring buffer
   - Maintain last 10 executed IPC commands
   - Store: timestamp, fn name, args

2. Recording
   - Hook into IPC dispatcher
   - On each IPC call: append to history

3. Crash capture
   - On crash: include history in crash.json
   - Helps understand what operations preceded crash

**Files**:
- `src/core/ipc_history.h/cpp` — History tracking
- Integration with IPC dispatcher

**Acceptance criteria**:
- History recorded correctly
- Timestamps accurate
- Included in crash.json

---

### 10. Documentation

**Subtasks**:
1. Spec (already done): `docs/specs/crash-reporting.md`

2. Guide: How to inspect crash reports
   - Open `.zip` file
   - View `crash.json` in text editor or JSON viewer
   - Open `crash.dmp` in Visual Studio debugger
   - Interpret exception code and callstack

3. Developer guide: Adding crash context
   - Document how to add custom crash info
   - Lua error capture pattern

**Files**:
- `docs/guides/crash-report-analysis.md` — How to use crash reports

**Acceptance criteria**:
- Clear explanation of crash report contents
- Examples of opening minidump in VS
- Guidance on interpreting crash data

---

### 11. Testing

**Test coverage target**: 80%+

**Test types**:

1. **Unit tests** (doctest)
   - System info capture (compare vs. actual system)
   - JSON serialization (crash data → JSON → valid JSON)
   - ZIP creation (file creation, structure)
   - History ring buffer (correct order, max size)

2. **Integration tests** (CTest)
   - Trigger exception → crash capture → ZIP created
   - IPC handler reads last crash
   - Minidump written and readable

3. **Manual tests**
   - Crash engine with null dereference → verify crash.json and minidump
   - Open minidump in Visual Studio → verify symbols resolve
   - Parse crash.json from extension → verify readable

4. **Performance tests**
   - Crash capture time (should be < 1s)
   - ZIP file size (should be < 200 MB)

**Files**:
- Test suites for each module
- Manual test steps documented

**Acceptance criteria**:
- 80%+ code coverage
- All tests pass
- Crash capture deterministic and reliable

---

## Task Order

1. **Exception Handler Installation** (week 1)
   - SEH handler, handler execution
   - Basic crash capture

2. **Crash Data Capture** (week 1–2)
   - All crash info collected
   - System info, engine state, Lua state

3. **Minidump Generation** (week 2)
   - MiniDumpWriteDump integration
   - Symbol loading

4. **ZIP Packaging** (week 2)
   - ZIP file creation and compression
   - File organization

5. **IPC Handler** (week 3)
   - `engine.get_last_crash` registration
   - Extension integration

6. **Testing & Docs** (week 3)
   - Comprehensive testing
   - Documentation

---

## Success Criteria

- [x] Spec complete (`docs/specs/crash-reporting.md`)
- [ ] Exception handler installed and catches crashes
- [ ] Minidump file created on crash
- [ ] `crash.json` contains all necessary data
- [ ] System info captured correctly
- [ ] Engine state snapshot accurate
- [ ] Lua error captured (if applicable)
- [ ] IPC handler `engine.get_last_crash` functional
- [ ] VSCode extension detects and notifies about crashes
- [ ] 80%+ test coverage
- [ ] Crash report ZIP file properly compressed and organized
- [ ] No performance impact on normal execution (handler only on exception)

