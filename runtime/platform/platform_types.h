#pragma once

/// Platform-agnostic opaque handle types.
///
/// These wrap OS-specific types (HWND, Display*, etc.) so engine code
/// above the platform layer never includes OS headers directly.
///
/// # Usage
/// Use these types in cross-platform interfaces (e.g., `RHISwapChainDesc`).
/// Cast back to native types only inside platform-specific .cpp files.

#include <cstdint>

namespace uz {

/// Opaque native window handle.
///
/// On Windows: HWND. On Linux/X11: Window (unsigned long). On macOS: NSWindow*.
/// Cast to `void*` for storage; cast back in platform-specific code only.
using NativeWindowHandle = void*;

/// Opaque native display handle.
///
/// On Windows: unused (always nullptr). On Linux/X11: Display*. On macOS: unused.
using NativeDisplayHandle = void*;

/// Opaque native DLL/shared library handle.
///
/// On Windows: HMODULE. On Linux: void* from dlopen. On macOS: void* from dlopen.
using NativeDllHandle = void*;

} // namespace uz
