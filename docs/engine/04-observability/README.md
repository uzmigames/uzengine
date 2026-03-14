# Observability

Logging, profiling, stat counters, and crash reporting for the engine runtime.

> **Status**: Partial (core logging and profiling done; advanced GPU profiling planned)
> **Source**: `runtime/engine/core/`, `runtime/engine/profiling/`

| Document | Topic |
|----------|-------|
| [01-log-system.md](01-log-system.md) | `UZ_LOG` macro, log categories, severity levels, spdlog backend |
| [02-cpu-profiling.md](02-cpu-profiling.md) | Tracy integration, `UZ_PROFILE_SCOPE`, named zones |
| [03-gpu-profiling.md](03-gpu-profiling.md) | GPU timestamp queries, DRED breadcrumbs, PIX integration |
| [04-stat-counters.md](04-stat-counters.md) | `StatCounter` system — draw calls, allocations, frame time |
| [05-crash-reporting.md](05-crash-reporting.md) | SEH crash handler, minidump generation, crash log JSON |

> **See also**: [../observability/](../../observability/) for cross-cutting observability specs.
