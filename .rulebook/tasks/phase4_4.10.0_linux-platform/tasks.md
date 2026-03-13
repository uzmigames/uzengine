## 1. Implementation
- [x] 1.1 Implement `LinuxWindow` (X11): `XCreateWindow`, `XMapWindow`, event loop, resize, close <!-- platform_window.h/.cpp -->
- [x] 1.2 Implement `LinuxInput` (X11): keyboard via `XKeyEvent` + XKB keymap, mouse via `XMotionEvent` <!-- input.h/.cpp -->
- [x] 1.3 Implement X11 fullscreen via `_NET_WM_STATE_FULLSCREEN` atom <!-- platform_window.cpp -->
- [ ] 1.4 Implement Vulkan surface creation via `VK_KHR_xlib_surface` <!-- deferred: RHI Vulkan backend needed first -->
- [ ] 1.5 Implement `LinuxWindow` (Wayland): `wl_surface` + `xdg_toplevel` + libdecor <!-- deferred: P3 -->
- [ ] 1.6 Implement `LinuxInput` (Wayland): `wl_keyboard`, `wl_pointer` listeners <!-- deferred: P3 -->
- [ ] 1.7 Implement Vulkan surface creation via `VK_KHR_wayland_surface` <!-- deferred: P3 -->
- [ ] 1.8 Implement display server auto-detection (prefer Wayland if `WAYLAND_DISPLAY` set, fallback X11) <!-- deferred: P3 -->
- [x] 1.9 Implement `LinuxFile`: `open`/`read`/`write`/`stat`/`opendir`/`readdir` <!-- file_system.h/.cpp -->
- [x] 1.10 Implement `inotify` file watcher for hot-reload <!-- file_watcher.h/.cpp -->
- [x] 1.11 Implement `mmap`/`munmap` for memory-mapped files <!-- memory_mapped_file.h/.cpp -->
- [x] 1.12 Implement Linux crash handler: `sigaction` for SIGSEGV/SIGABRT, `backtrace()` + `backtrace_symbols()` <!-- crash_handler.h/.cpp -->
- [x] 1.13 Implement XDG path resolution (`XDG_DATA_HOME`, `XDG_CONFIG_HOME`, `XDG_CACHE_HOME`) <!-- file_system.cpp -->
- [x] 1.14 Implement `dlopen`/`dlsym`/`dlclose` for DLL management <!-- dll_loader.h/.cpp -->
- [x] 1.15 Implement `fork`/`execve` with pipe for process creation <!-- process.h/.cpp -->
- [x] 1.16 Add Linux build target to `build.zig` with conditional platform compilation <!-- already existed -->
- [ ] 1.17 Dynamic loading of optional libraries (Wayland, Fontconfig, D-Bus) via `dlopen` <!-- deferred: P3 -->
- [x] 1.18 Implement shared memory via `shm_open`/`mmap` <!-- shared_memory.h/.cpp -->
- [x] 1.19 Implement environment variable access via `getenv`/`setenv` <!-- environment.h/.cpp -->
- [x] 1.20 Implement system info (CPU/memory/OS/GPU/battery/locale) <!-- system_info.h/.cpp -->
- [x] 1.21 Implement stack walker via `backtrace()`/`dladdr()`/`__cxa_demangle` <!-- stack_walker.h/.cpp -->

## 2. Testing
- [ ] 2.1 Test X11 window creation and destruction (requires X server or Xvfb)
- [ ] 2.2 Test X11 keyboard/mouse input dispatch
- [ ] 2.3 Test Wayland window creation (requires Wayland compositor) <!-- deferred: P3 -->
- [ ] 2.4 Test `inotify` file watcher detects file modification
- [ ] 2.5 Test `mmap` round-trip matches normal file read
- [ ] 2.6 Test XDG path resolution returns valid directories
- [ ] 2.7 Test crash handler produces backtrace on simulated SIGSEGV
- [ ] 2.8 Test `dlopen`/`dlsym` cycle with `libm.so`
- [ ] 2.9 Test process creation with pipe capture (`/bin/echo hello`)
- [ ] 2.10 Verify 95%+ coverage on all new code
- [x] 2.11 Windows build succeeds (zig build — 0 errors)

## 3. Documentation
- [x] 3.1 Update `docs/engine/07-platform/13-linux-platform.md` <!-- 875 lines -->
- [ ] 3.2 Update `docs/engine/07-platform/00-platform-abstraction.md` with Linux backend details
