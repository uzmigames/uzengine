# Platform — Backend Registry

> **Status**: Planned (Phase 5+)
> **Source**: `platform/common/backend_registry.h` (future)
> **Depends on**: Platform abstraction interfaces (`IPlatformWindow`, `IPlatformInput`, etc.)

## Purpose

UzEngine currently uses compile-time platform selection via the Zig build system — only one platform's source files are compiled per target. This works well for simple cases (Windows-only, Linux-only) but becomes insufficient when:

1. **Multiple display servers coexist** — Linux needs X11 and Wayland support in the same binary
2. **Multiple RHI backends coexist** — Windows needs DX12 and Vulkan support in the same binary for fallback/testing
3. **Headless fallback is required** — CI/testing needs to run without a display server or GPU

The Backend Registry pattern provides a **static array factory registry** for runtime backend selection. Inspired by Godot's architecture but adapted for UzEngine's C++ style, it enables multiple backend implementations to coexist and be selected at startup without heap allocation or complex initialization ordering.

## Problem Statement

### Current Approach: Compile-Time Selection

Today, the Zig build system selects which platform files to compile:

```zig
// build.zig
if (target.os.tag == .linux) {
    engine_lib.addCSourceFile(.{ .file = .{ .src = "platform/linux/x11/display_x11.cpp" } });
} else if (target.os.tag == .windows) {
    engine_lib.addCSourceFile(.{ .file = .{ .src = "platform/windows/display_windows.cpp" } });
}
```

This results in **exactly one implementation per subsystem per target**. The choice is made at compile time and baked into the binary.

### Why This Breaks Down

1. **Linux + X11 + Wayland**: On Linux, the engine needs BOTH X11 (legacy, wide compatibility) AND Wayland (modern, required on some distributions). A single binary cannot support both — you must recompile to switch.

2. **Windows + DX12 + Vulkan**: When supporting Vulkan on Windows, you need both DX12 (primary) and Vulkan (fallback/testing). Compile-time selection means you ship two binaries, one for each API.

3. **Headless CI**: Running tests or automation on CI servers (no display, no GPU) requires a headless backend. Compile-time selection means separate test binaries from game binaries.

4. **Graceful Fallback**: If the primary backend fails (e.g., Wayland compositor crash, GPU driver error), the engine cannot recover by switching to a fallback — it crashes.

### The Registry Solution

Provide a **static array registry** where each backend registers a factory function at startup. The engine selects from this registry at initialization time, with a guaranteed fallback (headless/null backend) always available.

- **No runtime allocation**: Registry is a static array
- **Simple ordering**: Register in priority order (X11, Wayland, headless)
- **Automatic fallback**: Last entry is always headless/null backend
- **Configuration-driven**: Select via config file or command-line argument
- **Clean error recovery**: If primary fails, try next in list

## Design: Static Array Factory Registry

### Registry Interface

```cpp
namespace uz {

/// Maximum number of backends that can be registered for any subsystem.
/// 8 is sufficient for all foreseeable display drivers, RHI backends, audio backends, etc.
constexpr int kMaxBackends = 8;

/// Factory function signature for creating a display server backend.
/// The factory receives a description and returns a pointer to the created backend,
/// or nullptr if creation failed.
using DisplayCreateFunc = PlatformWindow*(*)(const WindowDesc& desc);

/// Metadata entry for a registered backend.
struct BackendEntry {
    /// Unique backend name (e.g., "x11", "wayland", "headless", "dx12", "vulkan").
    /// Used for configuration and user selection.
    const char* name;

    /// Factory function that creates an instance of this backend.
    /// Called at engine startup with the desired configuration.
    DisplayCreateFunc createFunc;
};

/// Static registry for display server backends.
///
/// Backends register themselves during platform initialization (PlatformInit phase).
/// The registry maintains a static array of backend entries; registration appends to this array.
/// The last registered entry should always be a headless/null backend as the guaranteed fallback.
///
/// Thread safety: Registration is synchronous before engine startup. All queries are
/// read-only and safe from multiple threads after initialization completes.
class DisplayRegistry {
public:
    /// Register a new backend with the given name and factory function.
    ///
    /// Called by each backend module during PlatformInit().
    /// Names must be unique. If a duplicate name is registered, an assertion fires.
    ///
    /// # Panics
    /// - If more than kMaxBackends backends are registered (assertion failure)
    /// - If name is already registered (assertion failure)
    ///
    /// # Example
    /// ```cpp
    /// void DisplayX11::RegisterBackend() {
    ///     DisplayRegistry::Register("x11", [](const WindowDesc& desc) -> PlatformWindow* {
    ///         auto* win = new X11Window();
    ///         if (!win->Create(desc)) { delete win; return nullptr; }
    ///         return win;
    ///     });
    /// }
    /// ```
    static void Register(const char* name, DisplayCreateFunc func);

    /// Get the total number of registered backends.
    static int GetCount();

    /// Get the name of the backend at the given index.
    ///
    /// # Panics
    /// - If index >= GetCount() (assertion failure)
    static const char* GetName(int index);

    /// Create an instance of the backend at the given index.
    ///
    /// Returns a newly allocated backend instance, or nullptr if creation failed.
    /// The caller owns the returned pointer and must delete it.
    ///
    /// # Panics
    /// - If index >= GetCount() (assertion failure)
    static PlatformWindow* Create(int index, const WindowDesc& desc);

    /// Create an instance of the backend with the given name.
    ///
    /// Searches the registry for a matching name and calls Create(index, desc).
    /// Returns nullptr if the name is not found or if the factory returns nullptr.
    /// The caller owns the returned pointer and must delete it.
    ///
    /// # Example
    /// ```cpp
    /// auto* win = DisplayRegistry::Create("wayland", desc);
    /// if (!win) {
    ///     UZ_LOG(Platform, Warning, "Wayland backend not available");
    ///     win = DisplayRegistry::Create(DisplayRegistry::GetCount() - 1, desc);  // headless
    /// }
    /// ```
    static PlatformWindow* Create(const char* name, const WindowDesc& desc);

private:
    /// Static array of registered backends (no heap allocation).
    static BackendEntry s_backends[kMaxBackends];

    /// Current number of registered backends.
    static int s_count;
};

} // namespace uz
```

### Implementation Outline

The registry is simple — a static array and a counter:

```cpp
// platform/common/backend_registry.cpp

namespace uz {

BackendEntry DisplayRegistry::s_backends[kMaxBackends] = {};
int DisplayRegistry::s_count = 0;

void DisplayRegistry::Register(const char* name, DisplayCreateFunc func) {
    UZ_CHECK(s_count < kMaxBackends, "Too many backends registered");
    UZ_CHECK(name != nullptr && func != nullptr, "Invalid backend registration");

    // Check for duplicate names
    for (int i = 0; i < s_count; ++i) {
        UZ_CHECK(strcmp(s_backends[i].name, name) != 0,
                 "Backend '{}' already registered", name);
    }

    s_backends[s_count].name = name;
    s_backends[s_count].createFunc = func;
    s_count++;

    UZ_LOG(Platform, Info, "Registered display backend: '{}'", name);
}

int DisplayRegistry::GetCount() {
    return s_count;
}

const char* DisplayRegistry::GetName(int index) {
    UZ_CHECK(index >= 0 && index < s_count, "Backend index out of range");
    return s_backends[index].name;
}

PlatformWindow* DisplayRegistry::Create(int index, const WindowDesc& desc) {
    UZ_CHECK(index >= 0 && index < s_count, "Backend index out of range");
    auto* backend = s_backends[index].createFunc(desc);

    if (!backend) {
        UZ_LOG(Platform, Warning, "Failed to create backend: '{}'",
               s_backends[index].name);
    }
    return backend;
}

PlatformWindow* DisplayRegistry::Create(const char* name, const WindowDesc& desc) {
    for (int i = 0; i < s_count; ++i) {
        if (strcmp(s_backends[i].name, name) == 0) {
            return Create(i, desc);
        }
    }

    UZ_LOG(Platform, Warning, "Backend not found: '{}'", name);
    return nullptr;
}

} // namespace uz
```

## Registration Pattern

Each backend module self-registers during platform initialization. The registration is guarded by `#ifdef` so that backends are only registered if compiled.

### Example: X11 Backend

```cpp
// platform/linux/x11/display_x11.cpp

#ifdef UZ_X11_ENABLED

namespace uz {

void RegisterX11Backend() {
    /// Lambda factory captures X11 initialization state.
    DisplayRegistry::Register("x11", [](const WindowDesc& desc) -> PlatformWindow* {
        auto* win = new X11Window();
        if (!win->Create(desc)) {
            delete win;
            return nullptr;
        }
        return win;
    });
}

} // namespace uz

#endif
```

### Example: Wayland Backend

```cpp
// platform/linux/wayland/display_wayland.cpp

#ifdef UZ_WAYLAND_ENABLED

namespace uz {

void RegisterWaylandBackend() {
    DisplayRegistry::Register("wayland", [](const WindowDesc& desc) -> PlatformWindow* {
        auto* win = new WaylandWindow();
        if (!win->Create(desc)) {
            delete win;
            return nullptr;
        }
        return win;
    });
}

} // namespace uz

#endif
```

### Example: Headless Fallback

```cpp
// platform/common/display_headless.cpp

namespace uz {

void RegisterHeadlessBackend() {
    /// Headless backend is always registered last.
    /// It provides a null implementation that allows the engine to run
    /// without a display server (for testing, CI, automation).
    DisplayRegistry::Register("headless", [](const WindowDesc& desc) -> PlatformWindow* {
        auto* win = new HeadlessWindow(desc);
        // Headless always succeeds — never fails to create
        return win;
    });
}

} // namespace uz
```

## Platform Initialization

The platform layer calls the registration functions in a fixed order during startup. Compile-time `#ifdef` guards ensure that only compiled backends register.

```cpp
// platform/linux/platform_init_linux.cpp

namespace uz {

void PlatformInit() {
    // Register all available backends for this platform.
    // The order determines priority: the first registered is tried first.

    #ifdef UZ_X11_ENABLED
    RegisterX11Backend();
    #endif

    #ifdef UZ_WAYLAND_ENABLED
    RegisterWaylandBackend();
    #endif

    // Headless is always registered last as the guaranteed fallback.
    RegisterHeadlessBackend();
}

void PlatformShutdown() {
    // Clean up any global state (file descriptors, Display* handles, etc.)
}

} // namespace uz
```

## Runtime Backend Selection

The engine selects which backend to use at startup based on configuration or fallback logic.

```cpp
// engine/app/application.cpp

namespace uz {

bool Application::InitPlatform() {
    // Get user preference from config (or environment, or command-line).
    // Example: config.ini or environment variable UZ_DISPLAY_DRIVER.
    const char* requested = GetConfig().GetString("display.driver", "default");

    PlatformWindow* window = nullptr;

    if (strcmp(requested, "default") == 0) {
        // Use first registered backend (highest priority).
        window = DisplayRegistry::Create(0, m_windowDesc);
    } else {
        // User requested a specific backend by name.
        window = DisplayRegistry::Create(requested, m_windowDesc);
    }

    // Fallback to headless if primary backend fails.
    if (!window) {
        UZ_LOG(App, Warning, "Failed to create requested display backend; "
               "falling back to headless mode");

        // Headless is always the last registered backend.
        int headlessIndex = DisplayRegistry::GetCount() - 1;
        window = DisplayRegistry::Create(headlessIndex, m_windowDesc);
    }

    if (!window) {
        UZ_LOG(App, Error, "Fatal: could not create any display backend");
        return false;
    }

    m_window = window;
    return true;
}

} // namespace uz
```

## Applicable Subsystems

The registry pattern is useful for any subsystem with multiple coexisting backend implementations:

### 1. **Display Server** (Phase 5)
- Backends: `x11`, `wayland`, `headless`
- When to use: Linux with multiple display servers in one binary
- Current status: Compile-time selection (only one per build)

### 2. **RHI Backend** (Phase 6+ — optional)
- Backends: `dx12`, `vulkan`, `metal`, `dx11`
- When to use: Multi-API support with graceful fallback
- Current status: Compile-time selection (only one per build)
- **Note**: The current vtable-based RHI approach works well and does not require registry yet. Consider only if fallback logic or runtime selection becomes necessary.

### 3. **Audio Backend** (Future)
- Backends: `xaudio2` (Windows), `pulseaudio` (Linux), `alsa` (Linux), `null`
- When to use: When audio device changes or unavailable
- Current status: Planned

### 4. **Input Backend** (Future)
- Backends: `xinput2` (X11), `wayland-input`, `win32-raw`, `null`
- When to use: When input method unavailable or user configurable
- Current status: Planned

## What NOT to Use This For

The registry pattern is **not** appropriate for these cases:

- **Unique per-platform implementations** (file I/O, process management, system info) — compile-time selection is correct. Use one implementation per platform.
- **Single backend per platform** (Windows has only one window system; mobile has only one input method) — no need for runtime selection.
- **Hot-reload or dynamic loading** — the registry is static and initialized at startup, not for runtime plugin systems.
- **Stateful backends that conflict** (you cannot run X11 and Wayland simultaneously in the same process) — the registry selects one, then uses it exclusively.

## Rules

1. **Registry array is static** — no heap allocation, fixed size `kMaxBackends`
2. **`kMaxBackends = 8`** — sufficient for all display servers, RHI backends, audio backends
3. **Headless/null backend always registered last** — guaranteed fallback for every subsystem
4. **Registration happens before engine startup** — during `PlatformInit()` phase, before `Application::Init()`
5. **`#ifdef` guards at registration site** — not in backend code. Example: `#ifdef UZ_X11_ENABLED` wraps `RegisterX11Backend()`
6. **Backend code never checks platform** — it **is** the platform. No `#ifdef` inside `X11Window::Create()`
7. **One backend active at a time** — after selection, all operations go through the chosen backend
8. **Names are case-sensitive** — `"x11"` is not the same as `"X11"`
9. **Factory functions may return nullptr** — backends can fail to initialize (e.g., X11 server unavailable). The engine tries the next backend
10. **Caller owns allocated memory** — backend factory returns newly allocated pointers; the engine must `delete` them on shutdown

## Comparison Table

| Aspect | Compile-Time (Current) | Runtime Registry (Future) |
|--------|----------------------|---------------------------|
| Selection mechanism | Zig build system | Configuration file / command-line |
| Multiple backends per build | No | Yes |
| Fallback behavior | None — crashes | Automatic (try next in list) |
| Binary size | Smaller (one backend) | Larger (all backends) |
| Complexity | Low | Medium |
| When to use | 1 backend per platform | Multiple backends coexist |
| Example use case | Initial development | Multi-display-server distros, RHI fallback testing, CI headless mode |

## Timeline

- **Now (Phase 1-4)**: Compile-time selection is the correct approach. Registry not needed.
- **Phase 5 (Linux Wayland support)**: Implement `DisplayRegistry` for X11/Wayland/Headless coexistence
- **Phase 6+ (Multi-RHI)**: Evaluate whether to implement registry for DX12/Vulkan backend selection
- **Post-Phase 6**: Consider for audio and input backends as those systems mature

## Related Docs

- [Platform Abstraction](./00-platform-abstraction.md) — IPlatformWindow interface contract
- [Window System](./01-window-system.md) — PlatformWindow implementation
- [Linux Platform](./13-linux-platform.md) — Linux-specific backend implementations
