# Proposal: tools-scene-linter

## Why

A `.uzscene` file is a plain-text INI-like format authored by humans, AI agents, and editor tooling simultaneously. As a project grows to dozens of scenes with hundreds of nodes each, subtle structural errors become common: a mesh component references a `.uzmesh` file that was renamed or deleted; a script component points to a Lua file that no longer exists; a parent node references a child by ID that was never created; a prefab instance has a broken override pointing to a deleted material slot. These errors do not surface until the scene is loaded at runtime — which may happen during a play test, a demo, or a CI build, not during authoring.

The `tools-scene-linter` performs static analysis on `.uzscene` files without loading the engine or GPU. It reads every node in the scene file, validates all asset references against the project's file tree, checks parent-child ID consistency for cycles and dangling refs, validates property value types and ranges (e.g., a mesh scale of 0 is legal but almost always a bug), and identifies prefab override references to non-existent property paths. It runs in < 500ms on a 1000-node scene, making it suitable as a save-time hook and CI pre-merge check.

For AI-assisted development, the scene linter is essential infrastructure. An AI agent generating a `.uzscene` from a natural language prompt has no way to verify that every referenced asset path is correct — the linter provides a validation step the agent can run and react to, iterating until the scene is clean before presenting it to the user. This closes the feedback loop that currently requires a human to open the scene, notice the red "missing asset" icons, and manually fix references.

## What Changes

- `SceneLinter` class in `tools/scene_linter/scene_linter.h` — standalone, no engine state required, only reads project files
- Rule engine: pluggable `LintRule` interface with `Check(scene_file, project_root)` → `LintDiagnostic[]` return
- Built-in rules:
  - `MissingAssetRule`: every asset path referenced in a component must resolve to an existing file
  - `DanglingNodeRefRule`: every `parent_id` and `child_id` reference must point to a node that exists in the scene
  - `CycleDetectionRule`: detect circular parent-child relationships using DFS
  - `BrokenPrefabOverrideRule`: prefab override property paths must match the prefab's schema
  - `ZeroScaleRule`: warn on transform scale == 0 (warning, not error)
  - `DuplicateNodeIdRule`: error on duplicate node IDs within one scene file
  - `ScriptExistsRule`: Lua script paths referenced in script_runner components must exist and be valid UTF-8
- `LintDiagnostic` format: `{severity, rule_id, node_id, message, file_path, line_number, suggestion}`
- VSCode integration: diagnostics published to VSCode DiagnosticsCollection — shows red/yellow squiggles in .uzscene editor with hover messages
- On-save hook: lint runs automatically when a .uzscene file is saved in VSCode (configurable, on by default)
- IPC handler `tools.lint_scene(scene_path)` returns `LintDiagnostic[]` for AI agent use
- MCP tool `tools.lint_scene` for AI agent self-validation of generated scenes
- CI mode: `uzengine --lint-scenes project/scenes/ --fail-on-error` exits non-zero if any error-severity diagnostics found
- Unit tests: synthetic .uzscene files with each error type; assert diagnostics match expected rule_id, node_id, severity

## Impact

- Affected specs: `docs/specs/scene-system.md` (add scene linting section), `docs/specs/ai-native-features.md` (add tools.lint_scene MCP tool)
- Affected code: `tools/scene_linter/scene_linter.cpp` (new), `vscode-extension/src/linter/sceneLinter.ts` (new), `vscode-extension/src/extension.ts` (register on-save hook)
- Breaking change: NO (lint is a read-only pass, never modifies scene files)
- User benefit: Catches broken asset references before runtime; provides AI agents with a feedback loop for validating generated scenes; enables CI to reject commits with broken scene files before they reach main branch
