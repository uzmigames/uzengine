# 1. Zig as Primary Build System with CMake Mirror

**Status**: proposed
**Date**: 2026-03-15

## Context

UzEngine needs a build system that supports multi-platform compilation (Windows DX12/DX11, macOS Metal, Linux Vulkan), automatic source discovery, shader embedding, and static library output for demos. The engine has 35 thirdparty dependencies with mixed C/C++/ObjC++ sources.

## Decision

Use Zig build system (build.zig) as the primary build tool with a CMake mirror for MSVC/Clang fallback. Zig handles: automatic source discovery via addCppDir(), cross-platform compilation flags, shader embedding via uzembed tool, static library output (runtime.lib + editor.lib), and DLL deployment. setup.ps1 manages all thirdparty dependency downloads.

## Alternatives Considered

- CMake-only (industry standard but verbose, poor cross-compilation)
- Premake (simpler but less powerful than Zig for cross-platform)
- Meson (good cross-platform but less C/C++ control than Zig)
- Bazel (powerful but extremely complex setup)

## Consequences

Pros: Single-file build definition (724 lines), excellent cross-compilation, fast incremental builds, built-in C/C++ compilation. Cons: Less industry adoption, team members need Zig knowledge, CMake mirror must be kept in sync manually. Critical constraint: demos link against prebuilt runtime.lib — MUST rebuild runtime before demo when engine code changes.
