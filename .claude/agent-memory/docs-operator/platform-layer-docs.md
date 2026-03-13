---
name: Platform Layer Documentation (Phase 4.3-4.11.0)
description: 11 new comprehensive platform subsystem documentation files created for 07-platform/ directory
type: project
---

## Completed: Platform Layer Documentation (2026-03-12)

**Created**: 11 comprehensive markdown documentation files in `docs/engine/07-platform/`
**Status**: All files Planned (Phase 4.3-4.11.0)
**Source**: Rulebook task specifications (phase4_4.*.0)
**Total Lines**: ~2,700 lines of documentation

## Files Created

### 1. 00-platform-abstraction.md (262 lines)
**Phase**: 4.11.0 (Platform Abstraction Interface - foundational)
**Content**:
- IPlatformWindow, IPlatformInput, IPlatformFile, IPlatformProcess, IPlatformMisc vtable interfaces
- Platform factory pattern for runtime creation
- NativeWindowHandle opaque type pattern
- Clean dependency inversion (high-level code depends on abstractions, not Win32/X11)
**Key APIs**:
- Create(), PumpMessages(), IsOpen(), WasResized()
- GetKeyState(), GetMousePosition()
- Read(), Write(), Exists(), GetSize(), Watch()
- LoadDll(), GetDllSymbol(), CreateProcess()
- GetCpuInfo(), GetMemoryInfo(), GetOsInfo()

### 2. 03-multi-monitor-dpi.md (192 lines)
**Phase**: 4.3.0 (Multi-Monitor & DPI Awareness)
**Content**:
- Per-monitor DPI awareness V2 setup
- MonitorInfo struct (name, rect, work_area, dpi_scale, refresh_rate, is_primary)
- Monitor enumeration via EnumerateMonitors()
- WM_DPICHANGED handling for dynamic DPI changes
- Fullscreen targeting specific monitor
**Key Details**: DPI levels (unaware, system-aware, per-monitor-aware-v2), UI scaling patterns

### 3. 05-clipboard-drag-drop.md (208 lines)
**Phase**: 4.4.0 (Clipboard & Drag-Drop)
**Content**:
- Clipboard API (SetClipboardText, GetClipboardText, custom formats)
- OLE IDropTarget COM interface for drag-drop
- WM_DROPFILES fallback for simple file drops
- OnFilesDropped, OnDragEnter/Over/Leave callbacks
**Key APIs**: RegisterDragDrop(), CoInitializeEx()

### 4. 06-crash-handling.md (222 lines)
**Phase**: 4.5.0 (Crash Handling & Diagnostics)
**Content**:
- SEH handler via SetUnhandledExceptionFilter()
- MiniDump writer with MiniDumpWriteDump()
- Stack walking with CaptureStackBackTrace() + DbgHelp
- CrashContext struct (frame_number, active_scene, gpu_adapter, last_rhi_operation)
- Human-readable .txt + machine-parseable .json crash logs
**Key Patterns**: Exception filter → capture context → write minidump → write logs

### 5. 07-file-io.md (202 lines)
**Phase**: 4.6.0 (Platform File I/O)
**Content**:
- IPlatformFile abstraction (Read, Write, Exists, GetSize, Enumerate, MMap, Watch)
- FileInfo struct (path, name, size, modified_ms, is_directory)
- Memory-mapped files via MMap() for large file zero-copy access
- Async I/O with ReadAsync(callback) pattern
- Platform paths (GetUserDataDir, GetCacheDir, GetTempDir, GetExeDir)
**Key Pattern**: Read() returns empty vector on error, MMap() returns MemoryMappedFile RAII wrapper

### 6. 08-file-watcher.md (251 lines)
**Phase**: 4.6.0 (File Watching / Hot-Reload)
**Content**:
- ReadDirectoryChangesW implementation (Windows) with overlapped I/O
- inotify implementation (Linux) with epoll/read loop
- FileWatchEvent enum (Created, Modified, Deleted, Renamed)
- FileWatchCallback for event handling
- Watch(dir, callback) returns FileWatchHandle for cleanup
**Key Pattern**: Hot-reload by marking shader/asset dirty, recompile on next frame

### 7. 09-process-dll.md (237 lines)
**Phase**: 4.7.0 (Process & DLL Management)
**Content**:
- DLL loading (LoadDll, GetDllSymbol, FreeDll)
- Process creation with ProcessDesc (exe, args, work_dir, capture_stdout/stderr)
- Pipes for child process I/O capture
- CaptureProcessOutput() for stdout/stderr
- Environment variables (GetEnv, SetEnv)
- Shared memory for IPC (CreateSharedMemory, OpenSharedMemory)
**Use Cases**: Game.dll hot-reload, asset cooker subprocess, shader compiler spawning

### 8. 10-system-information.md (250 lines)
**Phase**: 4.8.0 (System Information)
**Content**:
- CpuInfo (brand, physical_cores, logical_cores, cache L1/L2/L3, features: SSE4.2/AVX/AVX2/AVX-512/BMI2)
- MemoryInfo (physical_total, physical_available, virtual_total, virtual_available, page_size)
- OsInfo (name, major/minor/build version, architecture, is_debugger_present)
- GpuInfo (adapter_name, vendor_id, driver_version, dedicated_vram, supports_ray_tracing, supports_mesh_shaders)
- BatteryInfo (is_plugged_in, charge_percent, time_remaining_minutes)
- LocaleInfo (language_code, region_code, currency_code, date_format, number_format)
**Use Cases**: Auto quality presets, crash context enrichment, feature gating, locale-aware formatting

### 9. 11-native-dialogs.md (233 lines)
**Phase**: 4.9.0 (Native Dialogs & OS Integration)
**Content**:
- File dialogs (OpenFileDialog, SaveFileDialog) via IFileDialog COM
- Message boxes via TaskDialogIndirect
- Color picker via ChooseColor
- Dark mode detection (IsDarkModeEnabled)
- Taskbar progress (ITaskbarList3::SetProgressValue, SetProgressState)
- System tray icon (Shell_NotifyIcon)
- Toast notifications (Windows Toast API)
**Key Pattern**: All return immediately, use callbacks for results

### 10. 12-os-integration.md (281 lines)
**Phase**: 4.9.0 (OS Integration - continuation)
**Content**:
- Dark mode theme system with WM_SETTINGCHANGE listener
- Taskbar integration states (Normal, Indeterminate, Error, Paused)
- System tray menu with context entries
- Toast notification with action callbacks
- Window titlebar color (immersive dark mode via DwmSetWindowAttribute)
- Power mode detection (BatterySaver, Normal, HighPerformance)
**Key Pattern**: Auto-apply theme at startup, listen for changes, adaptive FPS based on power mode

### 11. 13-linux-platform.md (341 lines)
**Phase**: 4.10.0 (Linux Platform)
**Content**:
- X11 backend: LinuxWindowX11 (XCreateWindow, XMapWindow, XNextEvent event loop)
- X11 input (XKeyEvent + XKB, XMotionEvent, XInput2 multi-touch)
- Wayland backend (TBD): libwayland, xdg-shell, libdecor
- LinuxFile POSIX implementation (open, read, write, mmap, inotify)
- Signal handler crash handling (SIGSEGV, backtrace())
- XDG paths ($XDG_DATA_HOME, $XDG_CONFIG_HOME, $XDG_CACHE_HOME)
- Dynamic symbol loading (dlopen/dlsym for optional deps)
**Status**: X11 implemented, Wayland marked as TBD

## Documentation Standards Met

✓ All files have Status header (Planned + Phase number)
✓ All files have Purpose section (explain the "why")
✓ All files have Architecture diagram (ASCII art)
✓ All files have API sections with C++ code examples
✓ All files have Engine Comparison table (UE5 vs Godot vs UzEngine)
✓ All files have Rules section (MUST/NEVER requirements)
✓ Rust-style doc comments (///) in code examples
✓ Syntax highlighting for ```cpp code blocks
✓ Cross-references using relative paths

## Integration Notes

- All new files follow format of existing `01-window-system.md`
- Existing files NOT modified: 01-window-system.md, 02-input-system.md, 04-job-system.md
- Main `docs/engine/README.md` updated: Tier 2 entry now shows 14 files + Phase 4.3-4.11.0
- File naming: 00-13 with phase-specific numbering (not strictly sequential)
- Pre-existing `03-file-io.md` (path resolution, 1.2) kept separate from new `07-file-io.md` (platform abstraction)

## Key Design Patterns Documented

1. **Abstraction Inversion**: Engine code uses IPlatform* interfaces, not Win32/X11 types
2. **Factory Pattern**: Platform-specific creation via `Platform::CreateWindow()`, `CreateInput()`, etc.
3. **Opaque Handles**: NativeWindowHandle, DllHandle, ProcessHandle hide platform details
4. **RAII Wrappers**: MemoryMappedFile auto-unmaps on destruction
5. **Callbacks**: FileWatchCallback, FileReadCallback, TrayIconCallback for async operations
6. **Event Structs**: FileWatchEvent, CrashContext, MonitorInfo for data passing

## Coverage Verification

- 11 files created (all requested)
- 0 files modified (as requested)
- 1 navigation update (docs/engine/README.md)
- Total new documentation: ~2,700 lines
- All files under 350 lines (overview docs, not full specs)
- All APIs documented with code examples
- All features compared to UE5 and Godot approaches
- All critical rules documented

## Future Work Notes

- 13-linux-platform.md: Wayland implementation marked TBD (X11 priority, Wayland follow-up)
- File I/O: Could be split if hot-reload patterns become complex
- OS integration: Could add shortcuts, jump lists, Windows Store integration in follow-up
- System info: Could add more GPU vendor detection (Intel Arc, AMD), thermal info
