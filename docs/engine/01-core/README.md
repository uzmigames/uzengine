# Core Systems

Foundation systems that every other engine module depends on. All are fully implemented and tested.

> **Status**: Done (Milestone 1.0–1.2)
> **Source**: `runtime/engine/core/`, `runtime/engine/paths/`

| Document | Topic |
|----------|-------|
| [01-paths.md](01-paths.md) | Path resolution API — `uz::GetExeDir()`, `uz::ResolveEngineResource()` |
| [02-logging.md](02-logging.md) | `UZ_LOG` macro system, log categories, spdlog integration |
| [03-assertions.md](03-assertions.md) | `UZ_CHECK` and `UZ_ENSURE` — invariant enforcement and error handling |
| [04-config.md](04-config.md) | Engine configuration loading and access |
| [05-memory.md](05-memory.md) | Engine allocators — arena, pool, frame; ownership model |
