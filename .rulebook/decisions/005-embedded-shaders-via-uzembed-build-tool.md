# 5. Embedded Shaders via uzembed Build Tool

**Status**: proposed
**Date**: 2026-03-15

## Context

Shaders need to be available at runtime for compilation to GPU bytecode. Options: load from filesystem at runtime, embed in binary at compile time, or use a virtual filesystem. Engine has ~50+ HLSL shader files across editor and runtime directories.

## Decision

Build-time shader embedding using the uzembed tool. All .hlsl files are scanned during build, converted to constexpr char[] C++ literals in .gen.h headers, and indexed via an EngineShader enum + EngineShaders lookup struct. Runtime compiles embedded source to DXIL/SPIR-V on first use via DXC (SM6.0+) or FXC (SM5.0 fallback). No runtime file I/O needed for shaders.

## Alternatives Considered

- Runtime file loading (requires shader files shipped alongside binary)
- SPIR-V precompilation (loses DXC optimization opportunities)
- Shader bytecode caching only (still needs source for first compile)
- Slang cross-compilation (adopted for Metal MSL, not primary path)

## Consequences

Pros: Single binary deployment (no loose shader files), compile-time validation of shader existence, enum-based type-safe shader lookup. Cons: Any shader change requires full rebuild of uzembed step, binary size increases with shader count, no runtime shader editing without hot-reload support. Future: shader hot-reload planned for development builds.
