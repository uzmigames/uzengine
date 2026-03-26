# Proposal: Linux Platform (Phase 4.10.0)

## Why
Linux is the second target platform for UzEngine (Steam Deck, SteamOS, Linux desktops). The platform abstraction interfaces (4.11.0) enable adding Linux without changing engine code. Linux requires dual display server support (X11 for legacy + Wayland for modern compositors), which Godot implements with 63 files across both servers. UE5 uses SDL2 as a thin wrapper. UzEngine will implement X11 directly (like Godot) for full control, with Wayland as a follow-up. This is P2 — the Vulkan backend already works cross-platform.

## What Changes

### New Systems
1. **LinuxWindow (X11)** — `XCreateWindow`, `XMapWindow`, event loop via `XNextEvent`/`XPending`
2. **LinuxInput (X11)** — keyboard via `XKeyEvent` + XKB, mouse via `XMotionEvent`, XInput2 for multi-touch
3. **LinuxWindow (Wayland)** — `wl_surface` + `xdg_toplevel` via libwayland + libdecor for CSD
4. **LinuxInput (Wayland)** — `wl_keyboard`, `wl_pointer`, `wl_touch` listeners
5. **LinuxFile** — `open`/`read`/`write`, `inotify` for file watching, `mmap` for memory-mapped files
6. **LinuxCrashHandler** — signal handler (SIGSEGV, SIGABRT) + `backtrace()` + core dump
7. **XDG paths** — `XDG_DATA_HOME`, `XDG_CONFIG_HOME`, `XDG_CACHE_HOME`
8. **Dynamic symbol loading** — `dlopen`/`dlsym` for optional deps (Wayland, Fontconfig, D-Bus)

### Refactoring
- build.zig extended with Linux target and conditional compilation
- Vulkan swapchain creation switched to use `VK_KHR_xlib_surface` / `VK_KHR_wayland_surface`

## Impact
- Affected specs: `docs/engine/07-platform/`
- Affected code: `runtime/platform/linux/`, `runtime/build.zig`
- Breaking change: NO — additive platform implementation behind interfaces
- User benefit: Engine runs on Linux (Steam Deck, desktops)
- Enables: Steam Deck support, Linux CI, cross-platform development
