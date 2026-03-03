# Tasks: tools-project-packager

## 1. Build Profile Schema

- [ ] 1.1 Define `game.build.json` schema: versioned JSON format with fields `schema_version`, `platform` (windows|linux|ps5|xsx), `packs[]`, `dlc[]`, `configuration` (debug|development|shipping), `output_dir`, `shader_compile_flags[]`
- [ ] 1.2 Implement `BuildProfile` C++ struct and JSON parser/serializer in `tools/packager/build_profile.cpp`
- [ ] 1.3 Validate schema on load: `platform` must be a known enum value; `output_dir` must be non-empty; report parse errors with field paths
- [ ] 1.4 Implement `BuildProfileManager` class with `Load(workspace_root) -> BuildProfile` and `Save(profile, workspace_root)` methods

## 2. Engine-Side Build Pipeline

- [ ] 2.1 Create `engine/src/ipc/handlers/build_handlers.cpp` with IPC handler `tools.build_project(profile)` that triggers the full pipeline
- [ ] 2.2 Implement pipeline step sequencing: validate → cook → compile_shaders → pack → stage → package
- [ ] 2.3 Emit `tools.build_progress` IPC events per step: `{step_name, status: starting|running|complete|failed, stdout_line?, percent_complete}`
- [ ] 2.4 Add IPC handler `tools.build_step(step_name, profile)` for running a single pipeline stage in isolation
- [ ] 2.5 Register both IPC handlers in the engine's IPC dispatch table

## 3. Build Runner

- [ ] 3.1 Create `tools/packager/build_runner.cpp` implementing the `BuildRunner` class that spawns and monitors each pipeline step as a subprocess
- [ ] 3.2 Capture stdout and stderr from each subprocess and emit them as `tools.build_progress` events line by line
- [ ] 3.3 Parse shader compilation errors: detect `.hlsl(line,col): error` patterns and link error messages to source file paths
- [ ] 3.4 Parse cook errors: detect asset import failures and link to source asset paths
- [ ] 3.5 Implement step re-run support: skip steps whose outputs are up-to-date (cook key unchanged) unless `--force` flag is set

## 4. Error Surfacing

- [ ] 4.1 Implement cook error parser: extract `asset_path`, `error_type`, and `message` from cook step output
- [ ] 4.2 Implement shader error parser: extract `.hlsl` file path, line number, and compiler message from DXC output
- [ ] 4.3 Implement packaging error handler: capture Windows error codes from packaging output and map to human-readable remediation hints
- [ ] 4.4 Aggregate all errors into a `BuildResult` struct: `{status: success|failed, failed_step, errors: [{step, asset_path?, file_path?, line?, message, remediation?}]}`

## 5. CLI Mode for CI

- [ ] 5.1 Add `--build-from-profile <path>` CLI argument to the engine entry point
- [ ] 5.2 Run the full pipeline from the specified `game.build.json` without opening VSCode
- [ ] 5.3 Write structured JSON build report to `{output_dir}/build-report.json` on completion
- [ ] 5.4 Exit code 0 on success; exit code 1 on failure with step name in stderr

## 6. VSCode Extension Panel

- [ ] 6.1 Create `vscode-extension/src/panels/ProjectPackagerPanel.ts` as a React `WebviewPanel`
- [ ] 6.2 Implement platform selector dropdown, content pack checkboxes, DLC group checkboxes, build configuration radio buttons, and output directory picker
- [ ] 6.3 Implement advanced mode: raw JSON editor for `game.build.json` with schema validation
- [ ] 6.4 Implement progress bar tracking overall pipeline completion based on completed vs. total steps
- [ ] 6.5 Implement per-step log accordion: each step has a collapsible section showing its stdout/stderr lines
- [ ] 6.6 Show "Build succeeded — output at {path}" VSCode notification on success (click to open in Explorer)
- [ ] 6.7 Show "Build failed at {step}" VSCode notification on failure (click to jump to that step's log)
- [ ] 6.8 Create `vscode-extension/src/commands/buildProject.ts` and register command `uzengine.buildProject`

## 7. Testing (≥95% line + branch coverage)

- [ ] 7.1 Write `tests/tools/test_build_profile_parse.cpp`: parse valid `game.build.json`; assert all fields round-trip correctly; parse invalid JSON with missing `platform`; assert validation error
- [ ] 7.2 Write `tests/tools/test_build_runner_steps.cpp`: mock each pipeline subprocess; assert steps execute in correct order; assert `build_progress` events emitted with correct step names
- [ ] 7.3 Write `tests/tools/test_build_error_parser.cpp`: synthetic shader error output with `.hlsl(12,5): error X3000`; assert parser extracts file path, line 12, and error message
- [ ] 7.4 Write `tests/tools/test_build_step_isolation.cpp`: run `tools.build_step("shader_compile", profile)` mock; assert only shader compile step is executed
- [ ] 7.5 Write `tests/tools/test_build_cli_mode.cpp`: invoke `--build-from-profile` with a valid profile; assert build-report.json is written with correct structure
- [ ] 7.6 Verify ≥95% line and branch coverage across all `tools/packager/` and `engine/src/ipc/handlers/build_handlers.cpp` using OpenCppCoverage
