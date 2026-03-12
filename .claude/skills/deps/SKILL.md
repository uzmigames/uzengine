---
name: deps
description: Manage thirdparty dependencies (vcpkg, manual libs). List, add, update, or audit dependencies. Delegates to build-system-engineer agent (sonnet).
---

# Dependency Management

Manage thirdparty libraries in `runtime/thirdparty/`.

## Usage

- `/deps list` — List all thirdparty dependencies with versions
- `/deps add <lib>` — Add a new dependency
- `/deps audit` — Check for outdated or unused dependencies
- `/deps check <lib>` — Verify a dependency is correctly integrated

## Current Dependencies

**Manual (runtime/thirdparty/)**:
- D3D12MA v3.1.0 — GPU memory allocator
- DXC — DirectX Shader Compiler (dxcompiler.dll + dxil.dll)
- Agility SDK — D3D12Core.dll (SDK 614)
- spdlog — Logging (wrapped by UZ_LOG)
- Tracy — CPU/GPU profiler (wrapped by UZ_PROFILE_SCOPE)
- DirectX Headers — d3d12.h, dxgiformat.h (SDK 614)
- spirv-reflect — Shader reflection
- xxhash — Content hashing for shader cache
- mikktspace — Tangent generation
- sse_mathfun — SIMD math (RJVB fork)

**vcpkg (runtime/thirdparty/vcpkg/)**:
- zlib, zstd — Compression

## Workflow

1. Launch **build-system-engineer** agent for complex integration
2. For simple queries, search `runtime/thirdparty/` directly
3. Always update `build.zig` and deploy DLLs via `addInstallBinFile`
