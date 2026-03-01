# Phase 1 — Debug, Test & Profiling System

## Context

A comprehensive debug/test/profiling system is a hard prerequisite for all future phases. Without it:
- CPU/GPU bottlenecks are invisible during renderer development (Phase 2)
- Memory errors in arena allocators go undetected until crash
- Engine regressions (scene loading, IPC, ring buffer) can't be caught automatically
- Game developers using UzEngine have no tools to debug their own projects

This task is intentionally placed in Phase 1 (before PBR renderer and asset pipeline) because all subsequent systems depend on it.

## Goal

- **Assert macros**: `ASSERT`, `ASSERT_MSG`, `VERIFY` — debug-break on violation, stripped in Release
- **CPU profiler**: Tracy Profiler zones (`UZ_PROFILE_ZONE`, `UZ_PROFILE_FUNCTION`, `UZ_PROFILE_FRAME_MARK`)
- **GPU profiler**: Tracy GPU zones (DX12) + PIX event markers + IPC-triggered PIX/RenderDoc captures
- **Memory debugger**: MSVC ASAN (optional), arena high-water mark tracking in `UZ_DEBUG` builds
- **Debug overlay**: ImGui panel rendering to backbuffer (visible in JPEG stream to VSCode)
- **Unit tests**: doctest framework, CTest integration, initial test suite covering core modules
- **IPC debug commands**: toggle overlay, dump arena stats, trigger captures, run tests

## Dependencies

- **Phase 0** (DX12 renderer, JPEG stream, IPC) — completed

## Spec

[docs/specs/debug-profiling.md](../../../docs/specs/debug-profiling.md)
