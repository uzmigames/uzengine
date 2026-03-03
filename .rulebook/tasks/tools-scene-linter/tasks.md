# Tasks: tools-scene-linter

## 1. SceneLinter Core Class

- [ ] 1.1 Create `tools/scene_linter/scene_linter.h` defining `SceneLinter` class with `Lint(scene_path, project_root) -> vector<LintDiagnostic>` public method; no engine state required
- [ ] 1.2 Create `tools/scene_linter/scene_linter.cpp` implementing the lint orchestration loop: parse scene file, instantiate all registered rules, collect diagnostics
- [ ] 1.3 Define `LintDiagnostic` struct: `{severity: error|warning|info, rule_id, node_id, message, file_path, line_number, suggestion}`
- [ ] 1.4 Define `LintRule` pure virtual interface with `Check(scene_file, project_root) -> vector<LintDiagnostic>` method
- [ ] 1.5 Implement rule registry: `SceneLinter::RegisterRule(unique_ptr<LintRule>)` and `SceneLinter::RegisterDefaultRules()`
- [ ] 1.6 Implement scene file parser that reads `.uzscene` INI-like format into a `SceneFile` struct with `nodes[]` and per-node `components[]`

## 2. Built-in Lint Rules

- [ ] 2.1 Implement `MissingAssetRule`: for every asset path referenced in any component, check that the file exists on disk relative to `project_root`; report `rule_id: "missing_asset"` with the non-existent path
- [ ] 2.2 Implement `DanglingNodeRefRule`: for every `parent_id` and `child_id` reference, check that the referenced node ID exists in the scene; report `rule_id: "dangling_node_ref"`
- [ ] 2.3 Implement `CycleDetectionRule`: run DFS over the parent-child graph; report the node IDs involved in any cycle with `rule_id: "cycle_detected"`
- [ ] 2.4 Implement `BrokenPrefabOverrideRule`: load the referenced prefab schema; verify all `override` property paths exist in the schema; report `rule_id: "broken_prefab_override"` with the invalid path
- [ ] 2.5 Implement `ZeroScaleRule`: check transform scale components; warn (not error) if any axis is 0.0; report `rule_id: "zero_scale"` with severity `warning`
- [ ] 2.6 Implement `DuplicateNodeIdRule`: detect duplicate node IDs within one scene file; report `rule_id: "duplicate_node_id"` for each duplicate occurrence
- [ ] 2.7 Implement `ScriptExistsRule`: for each `script_runner` component, verify the Lua script path exists and the file is valid UTF-8; report `rule_id: "script_not_found"` or `rule_id: "script_invalid_encoding"`

## 3. IPC and MCP Integration

- [ ] 3.1 Add `tools.lint_scene(scene_path)` IPC handler in `engine/src/ipc/handlers/tools_handlers.cpp` returning JSON array of `LintDiagnostic` objects
- [ ] 3.2 Register `tools.lint_scene` as an MCP tool in the MCP server tool registry
- [ ] 3.3 Add MCP tool documentation to `docs/specs/ai-native-features.md`

## 4. CI Mode

- [ ] 4.1 Add `--lint-scenes <dir>` CLI argument to the engine entry point
- [ ] 4.2 Walk all `.uzscene` files in the specified directory recursively and run `SceneLinter` on each
- [ ] 4.3 Add `--fail-on-error` flag: exit code 1 if any `error`-severity diagnostic is found
- [ ] 4.4 Print all diagnostics to stdout: format `[ERROR|WARN] {file_path}:{line_number} [{rule_id}] {node_id} — {message}`

## 5. VSCode Extension Integration

- [ ] 5.1 Create `vscode-extension/src/linter/sceneLinter.ts` that calls `tools.lint_scene` IPC on the active `.uzscene` file
- [ ] 5.2 Publish `LintDiagnostic[]` results to the VSCode `DiagnosticsCollection` API, mapping severity to VSCode `DiagnosticSeverity`
- [ ] 5.3 Register an on-save hook in `vscode-extension/src/extension.ts`: when a `.uzscene` file is saved, automatically run the linter
- [ ] 5.4 Add `uzengine.lintScene` VSCode configuration setting (boolean, default `true`) to enable/disable the on-save hook
- [ ] 5.5 Show red/yellow squiggle underlines in the `.uzscene` editor at the correct line numbers with hover messages from `LintDiagnostic.message` and `suggestion`

## 6. Performance Requirement

- [ ] 6.1 Implement benchmark test: lint a synthetic 1000-node scene file; assert total lint time < 500ms on a single CPU core
- [ ] 6.2 Optimize the asset path existence check to cache `stat()` results for the same path within one lint pass

## 7. Testing (≥95% line + branch coverage)

- [ ] 7.1 Write `tests/tools/test_scene_linter_missing_asset.cpp`: synthetic `.uzscene` referencing a non-existent mesh path; assert `MissingAssetRule` fires with correct `node_id` and missing path
- [ ] 7.2 Write `tests/tools/test_scene_linter_dangling_ref.cpp`: scene with `parent_id` pointing to non-existent node ID; assert `DanglingNodeRefRule` fires
- [ ] 7.3 Write `tests/tools/test_scene_linter_cycle.cpp`: scene with circular parent-child chain A→B→C→A; assert `CycleDetectionRule` reports all three node IDs
- [ ] 7.4 Write `tests/tools/test_scene_linter_zero_scale.cpp`: scene node with `scale_y: 0.0`; assert `ZeroScaleRule` fires with severity `warning`
- [ ] 7.5 Write `tests/tools/test_scene_linter_duplicate_id.cpp`: scene with two nodes sharing the same ID; assert `DuplicateNodeIdRule` fires twice (once per duplicate occurrence)
- [ ] 7.6 Write `tests/tools/test_scene_linter_clean_scene.cpp`: valid scene with all existing asset references and valid parent-child tree; assert zero diagnostics
- [ ] 7.7 Verify ≥95% line and branch coverage across all `tools/scene_linter/` source files using OpenCppCoverage
