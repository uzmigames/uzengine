# Current-source Windows diagnostic baseline

## Why

The approved stabilization audit identified baseline provenance. This focused task implements umbrella item 1.5 in the first Windows stabilization milestone.

## What Changes

Capture initial Windows rendering diagnostics only after a current build and the relevant gates; retain executable/source hashes and exact capture settings.

## Impact

- Affected specs: project-stabilization.
- Affected code: runtime diagnostics, scripts/rendering, docs/analysis/project-stabilization.
- Breaking change: NO (invalid verification evidence now fails explicitly).
- Ownership: [audit checklist](../phase0_project-stabilization-audit/tasks.md), item 1.5. The umbrella retains the baseline gate and is closed only with this task's accepted evidence.
