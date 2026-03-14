# Platform Layer

Platform abstraction — window, input, file I/O, file watching, process/DLL management, and OS services.

> **Status**: Planned (Phase 4.3–4.11.0)
> **Source**: `runtime/platform/`

| Document | Topic |
|----------|-------|
| [00-platform-abstraction.md](00-platform-abstraction.md) | Platform interface design, `IPlatform` vtable, backend registry |
| [01-window-system.md](01-window-system.md) | Window creation, resize, fullscreen, HWND embedding |
| [02-input-system.md](02-input-system.md) | Raw input, keyboard, mouse, gamepad |
| [03-multi-monitor-dpi.md](03-multi-monitor-dpi.md) | Multi-monitor support, DPI scaling, per-monitor DPI |
| [04-job-system.md](04-job-system.md) | Thread pool, job graph, fiber-based task scheduling |
| [05-clipboard-drag-drop.md](05-clipboard-drag-drop.md) | Clipboard read/write, drag-and-drop OLE integration |
| [06-crash-handling.md](06-crash-handling.md) | SEH/signal handlers, stack capture, minidump writing |
| [07-file-io.md](07-file-io.md) | Async file I/O, memory-mapped files, directory iteration |
| [08-file-watcher.md](08-file-watcher.md) | Directory change notification (ReadDirectoryChangesW) |
| [09-process-dll.md](09-process-dll.md) | Child process launch, DLL load/unload, hot-reload support |
| [10-system-information.md](10-system-information.md) | CPU/GPU info, VRAM query, OS version |
| [11-native-dialogs.md](11-native-dialogs.md) | File open/save dialogs, message boxes, IFileDialog |
| [12-os-integration.md](12-os-integration.md) | Shell integration, system tray, jumplist, protocol handlers |
| [13-linux-platform.md](13-linux-platform.md) | Linux platform backend (planned) |
| [14-platform-registry.md](14-platform-registry.md) | Platform backend registration and selection |
| [15-macos-platform.md](15-macos-platform.md) | macOS platform backend (planned) |
| [16-path-resolution.md](16-path-resolution.md) | Path resolution API — `uz::GetExeDir()`, canonical path normalization |
