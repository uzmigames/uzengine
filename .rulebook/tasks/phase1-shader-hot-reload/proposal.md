# Phase 1 — Shader Hot-Reload + Editor Integration

## Context

Currently shaders are embedded as `constexpr` strings in C++ source. This task moves shaders to external `.hlsl` files with a full compile pipeline (DXC), file watcher, hot-reload on save, and error reporting back to the VSCode extension as diagnostics.

## Goal

- Shaders loaded from `.hlsl` files in the project `shaders/` directory
- Compiled via DXC (DirectX Shader Compiler) at runtime
- Hot-reload triggered by file save or `shader.reload` IPC call
- Compile errors reported to extension as VS Code diagnostics (red squiggles)
- VSCode saves `.hlsl` → extension sends `shader.reload` → viewport updates within 1 second

## Spec Reference

[Shader System Spec](../../../docs/specs/shader-system.md)

## Approach

1. Add DXC as vcpkg dependency (or use the Windows SDK DXC)
2. Create `ShaderCompiler` that calls DXC and returns DXIL bytecode or error list
3. Create `ShaderManager` with hot-reload support (PSO swap on success)
4. Add file watcher in the engine (`ReadDirectoryChangesW`)
5. Register `shader.*` IPC handlers
6. Extension: on `.hlsl` save → call `shader.reload` IPC → show errors as diagnostics
