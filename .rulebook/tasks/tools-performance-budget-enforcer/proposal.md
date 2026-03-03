# Proposal: tools-performance-budget-enforcer

## Why

Performance regressions in game engines are insidious. A new particle emitter added to a scene is barely noticeable at 200 particles, marginally noticeable at 2,000, and causes a hard frame drop at 20,000 — but no alarm fires between the first and third commit. A new material pass adds 0.3ms to the GPU frame time, which accumulates across 20 materials added over 6 weeks to produce a 6ms total regression that completely consumes the render budget. Without automated budget enforcement, these regressions are found by QA when the game stutters, not by engineers when the commit lands.

The `tools-performance-budget-enforcer` integrates with CI to run the engine against a benchmark scene and compare measured performance metrics against thresholds defined in `game.perf-budget.toml`. Metrics include: CPU frame time (median and 99th percentile), GPU frame time (median and 99th percentile), VRAM usage, draw call count, and triangle count. If any metric exceeds its threshold by more than a configurable tolerance, the CI step fails with a diagnostic report linking the regression to the specific metric, the measured value, and the configured threshold.

The enforcer complements (but does not replace) the VRAM visualization panel and draw call analyzer — those tools are for interactive diagnosis during development. The performance budget enforcer is the automated gate that prevents diagnosed regressions from being silently reintroduced. Together these three tools form a complete performance discipline pipeline: visualize in development, diagnose when metrics drift, and enforce automatically at merge time.

## What Changes

- `game.perf-budget.toml` configuration schema: per-scene budget entries with `{scene_path, cpu_frame_time_ms_p99, gpu_frame_time_ms_p99, vram_mb, draw_calls_max, triangles_max, warmup_frames, sample_frames}`
- Engine benchmark mode: `uzengine --benchmark scene=path/to/scene.uzscene --output metrics.json --frames 300 --warmup 60` renders the scene headlessly, records per-frame metrics, writes percentile summary to JSON
- `PerformanceBenchmark` report format: `{scene, frames_sampled, metrics: {cpu_frame_time: {p50, p95, p99}, gpu_frame_time: {p50, p95, p99}, vram_peak_mb, draw_calls_max, triangles_max}}`
- Budget checker CLI: `uzengine --check-budget game.perf-budget.toml --metrics metrics.json` compares results against thresholds and exits non-zero with human-readable violation report if any threshold is breached
- CI integration: GitHub Actions / generic CI step example in docs: run benchmark, run checker, fail build on violation
- VSCode integration: "Run Performance Benchmark" command (`uzengine.runPerfBenchmark`) runs benchmark in background, shows results in Output panel, highlights threshold breaches in yellow/red
- Regression report format for CI: `{scene, metric, measured, threshold, exceeded_by_percent, status: "PASS" | "WARN" | "FAIL"}`
- Historical trend tracking: benchmark results appended to `perf-history.jsonl` (newline-delimited JSON) for per-commit trend graphs in VSCode panel
- Unit tests: synthetic metrics JSON against synthetic budget TOML; assert checker correctly identifies violations and produces correct exit codes

## Impact

- Affected specs: `docs/specs/debug-profiling.md` (add performance budget enforcement section), `docs/guides/testing-guide.md` (add CI performance testing workflow)
- Affected code: `engine/src/tools/benchmark_runner.cpp` (new), `tools/perf_budget/budget_checker.cpp` (new), `vscode-extension/src/commands/runPerfBenchmark.ts` (new), `vscode-extension/src/panels/PerfBudgetPanel.ts` (new)
- Breaking change: NO (new tool; no existing systems modified; CI integration is opt-in via budget TOML)
- User benefit: Catches performance regressions at merge time, not at QA; creates accountability for performance by making budgets explicit and machine-verified; enables confident refactoring by providing immediate feedback on whether a change improved or degraded performance
