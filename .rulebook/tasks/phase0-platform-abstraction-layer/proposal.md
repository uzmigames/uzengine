# Proposal: phase0-platform-abstraction-layer

## Why

UzEngine currently uses Windows-only APIs directly throughout the codebase: `CreateFile`/`ReadFile` for file I/O, `CreateThread`/`WaitForSingleObject` for threading, `QueryPerformanceCounter` for timing, `OutputDebugString` for logging, and `WIN32_FIND_DATA` for directory enumeration. This prevents the engine from running on Linux (CI containers, Steam Deck) or macOS (ARM development machines).

More urgently, the CI pipeline cannot run headless tests without platform abstraction — Windows-specific calls make it impossible to compile and test on the Linux GitHub Actions runners used for automated builds. Every system added without PAL widens this gap.

## What Changes

- Define `IPlatform` interface (or thin C function layer) covering: filesystem, threads, mutexes, condition variables, high-resolution timer, dynamic library loading, memory mapping
- Implement `Win32Platform` wrapping existing Windows calls
- Stub `LinuxPlatform` and `MacOSPlatform` for future use
- Replace all direct `WIN32` calls in engine core with PAL calls
- Define platform-detection macros (`UZ_PLATFORM_WINDOWS`, `UZ_PLATFORM_LINUX`, `UZ_PLATFORM_MACOS`)

Phase 0 = design the interface. Phase 1 = `Win32Platform` implementation. Phase 3+ = Linux/macOS implementations.

## Impact

- Affected specs: `docs/specs/engine/platform-abstraction.md` (to be created)
- Affected code: `src/engine/core/` — file I/O, threading, timing utilities
- Breaking change: NO (wraps existing code, no behavior changes)
- User benefit: Linux CI builds; Steam Deck export target; macOS ARM development; headless test runners
