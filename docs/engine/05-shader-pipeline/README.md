# Shader Pipeline

Shader compilation, virtual filesystem, content-hash caching, and hot-reload system.

> **Status**: Done (Milestone 1.5) — DXC (SM6.6) and FXC (SM5.0) dual-compiler
> **Source**: `runtime/engine/shader/`

| Document | Topic |
|----------|-------|
| [01-overview.md](01-overview.md) | Pipeline overview, DXC vs FXC routing, shader targets |
| [02-vfs.md](02-vfs.md) | Shader virtual filesystem — path resolution and include handling |
| [03-compilation.md](03-compilation.md) | Compilation pipeline, DXC/FXC, SM6.6 bindless vs SM5.0 fallback |
| [04-cache.md](04-cache.md) | Content-hash shader cache, invalidation, disk layout |
| [05-hot-reload.md](05-hot-reload.md) | File watcher integration, live shader recompilation |
| [06-reflection.md](06-reflection.md) | Shader reflection — input layout, constant buffer introspection |
| [07-slang-metal-compilation.md](07-slang-metal-compilation.md) | Slang compiler integration for Metal/SPIR-V targets |
