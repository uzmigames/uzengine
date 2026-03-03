# Tasks: tools-performance-budget-enforcer

## 1. Budget Configuration Schema

- [ ] 1.1 Define `game.perf-budget.toml` schema: array of per-scene budget entries with fields `scene_path`, `cpu_frame_time_ms_p99`, `gpu_frame_time_ms_p99`, `vram_mb`, `draw_calls_max`, `triangles_max`, `warmup_frames`, `sample_frames`
- [ ] 1.2 Implement `PerfBudgetConfig` C++ struct and TOML parser in `tools/perf_budget/budget_config.cpp`
- [ ] 1.3 Validate TOML schema on load: all numeric thresholds must be > 0; `warmup_frames` >= 10; `sample_frames` >= 30; report parse errors with line numbers
- [ ] 1.4 Provide a documented example `game.perf-budget.toml` in `docs/guides/testing-guide.md`

## 2. Engine Benchmark Mode

- [ ] 2.1 Add `--benchmark` CLI flag to the engine entry point with sub-arguments: `scene=<path>`, `output=<path>`, `frames=<N>`, `warmup=<N>`
- [ ] 2.2 Implement `BenchmarkRunner` class in `engine/src/tools/benchmark_runner.cpp`: renders specified scene headlessly for `warmup + frames` frames; discards warmup frames from metrics
- [ ] 2.3 Collect per-frame metrics: CPU frame time (from high-resolution timer), GPU frame time (from DX12 timestamp queries), VRAM usage (from `GpuResourceTracker`), draw call count (from `DrawCallRecorder`), triangle count
- [ ] 2.4 Compute percentile summaries: p50, p95, p99 for CPU and GPU frame times using sorting + indexing
- [ ] 2.5 Write `PerformanceBenchmark` JSON report to `output` path with format: `{scene, frames_sampled, metrics: {cpu_frame_time: {p50, p95, p99}, gpu_frame_time: {p50, p95, p99}, vram_peak_mb, draw_calls_max, triangles_max}}`

## 3. Budget Checker CLI

- [ ] 3.1 Implement `BudgetChecker` class in `tools/perf_budget/budget_checker.cpp`
- [ ] 3.2 Add `--check-budget <budget_toml> --metrics <metrics_json>` CLI mode
- [ ] 3.3 For each metric in the report, compare against the corresponding threshold from the budget TOML
- [ ] 3.4 Produce human-readable violation report to stdout: one line per metric, format: `[PASS|WARN|FAIL] {metric}: measured={value}, threshold={threshold}, delta={+/-N%}`
- [ ] 3.5 Exit code 0 if all metrics PASS; exit code 1 if any FAIL; exit code 2 if any WARN and `--warn-as-error` flag is set
- [ ] 3.6 Define `RegressionReport` format per metric: `{scene, metric, measured, threshold, exceeded_by_percent, status: "PASS"|"WARN"|"FAIL"}`

## 4. Historical Trend Tracking

- [ ] 4.1 Implement benchmark result append to `perf-history.jsonl` (newline-delimited JSON, one record per run)
- [ ] 4.2 Each JSONL record includes: `timestamp`, `commit_hash` (from `git rev-parse HEAD` if available), `scene_path`, and all metric values
- [ ] 4.3 Add `--history-file <path>` flag to `--check-budget` CLI to specify the JSONL file location (default `perf-history.jsonl` in workspace root)

## 5. VSCode Extension Integration

- [ ] 5.1 Create `vscode-extension/src/commands/runPerfBenchmark.ts` implementing `uzengine.runPerfBenchmark` command
- [ ] 5.2 Run benchmark as a background child process, streaming stdout to VSCode Output panel
- [ ] 5.3 On completion, parse the regression report and highlight threshold breaches in the Output panel (yellow for WARN, red for FAIL)
- [ ] 5.4 Create `vscode-extension/src/panels/PerfBudgetPanel.ts` showing historical trend graphs from `perf-history.jsonl`
- [ ] 5.5 Implement per-metric sparkline graphs in the trend panel showing last 20 benchmark runs

## 6. CI Integration Documentation

- [ ] 6.1 Document GitHub Actions workflow example in `docs/guides/testing-guide.md`: run `--benchmark`, then `--check-budget`, fail build on non-zero exit code
- [ ] 6.2 Document generic CI step in `docs/specs/debug-profiling.md` for CI systems that are not GitHub Actions

## 7. Testing (≥95% line + branch coverage)

- [ ] 7.1 Write `tests/tools/test_budget_checker_pass.cpp`: synthetic metrics JSON where all values are below thresholds; assert all statuses are PASS and exit code is 0
- [ ] 7.2 Write `tests/tools/test_budget_checker_fail.cpp`: synthetic metrics JSON where `cpu_frame_time_ms_p99` exceeds threshold; assert status is FAIL, exit code is 1, and violation message contains correct delta percentage
- [ ] 7.3 Write `tests/tools/test_budget_checker_warn.cpp`: synthetic metrics at 95% of threshold; assert status is WARN
- [ ] 7.4 Write `tests/tools/test_budget_config_parser.cpp`: parse valid and invalid TOML budget files; assert schema validation errors include line numbers for invalid inputs
- [ ] 7.5 Write `tests/tools/test_perf_benchmark_percentiles.cpp`: feed 100 synthetic per-frame values to percentile calculator; assert p50, p95, p99 match expected values (computed manually)
- [ ] 7.6 Write `tests/tools/test_history_append.cpp`: run `BudgetChecker` three times; assert `perf-history.jsonl` has 3 records and each record contains all required fields
- [ ] 7.7 Verify ≥95% line and branch coverage across all `tools/perf_budget/` source files using OpenCppCoverage
