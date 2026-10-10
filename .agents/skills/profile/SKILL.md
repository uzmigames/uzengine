---
name: profile
description: Analyze performance, add profiling instrumentation, or investigate frame time issues. Delegates to performance-engineer agent (sonnet). Use when optimizing, benchmarking, or investigating slowdowns.
---

# Performance Profiling

Delegate performance work to the **performance-engineer** agent.

## Usage

- `/profile <file>` — Add profiling instrumentation to a file
- `/profile analyze` — Analyze current profiling data / stat counters
- `/profile benchmark <demo>` — Run and report benchmark for a demo
- `/profile audit` — Check that all per-frame functions have UZ_PROFILE_SCOPE

## Profiling Stack

- **CPU**: Tracy via `UZ_PROFILE_SCOPE` macro
- **GPU**: DX12 timestamp queries via `dx12_gpu_profiler.h`
- **Memory**: Engine allocators with tracking
- **Stats**: `StatCounter` per system (draw calls, cache hits, etc.)

## Rules

- ALWAYS use `UZ_PROFILE_SCOPE` — NEVER raw Tracy macros
- Every per-frame function MUST have profiling
- Profile BEFORE optimizing — never guess
