#pragma once

/// Compile-time platform detection macros.
///
/// These are set by build.zig via `-DUZ_PLATFORM_WINDOWS` etc.
/// If not set by the build system, auto-detect from compiler predefined macros.
///
/// # Defined Macros
/// Exactly one of the following will be defined to `1`:
/// - `UZ_PLATFORM_WINDOWS` — Win32/Win64
/// - `UZ_PLATFORM_LINUX` — Linux (X11/Wayland)
/// - `UZ_PLATFORM_MACOS` — macOS (Cocoa/Metal)

#if !defined(UZ_PLATFORM_WINDOWS) && !defined(UZ_PLATFORM_LINUX) && !defined(UZ_PLATFORM_MACOS)
    #if defined(_WIN32) || defined(_WIN64)
        #define UZ_PLATFORM_WINDOWS 1
    #elif defined(__linux__)
        #define UZ_PLATFORM_LINUX 1
    #elif defined(__APPLE__)
        #define UZ_PLATFORM_MACOS 1
    #else
        #error "Unsupported platform — define UZ_PLATFORM_WINDOWS, UZ_PLATFORM_LINUX, or UZ_PLATFORM_MACOS"
    #endif
#endif
