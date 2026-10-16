# Required test and fixture integrity

## Why

The approved stabilization audit identified F-014. This focused task implements umbrella item 1.3 in the first Windows stabilization milestone.

## What Changes

Separate unit tests from editor/native integration harnesses, fail empty required suites and absent fixtures, repair lint, and install coverage providers without reducing thresholds.

## Impact

- Affected specs: project-stabilization.
- Affected code: extension, mcp, .github/workflows.
- Breaking change: NO (invalid verification evidence now fails explicitly).
- Ownership: [audit checklist](../phase0_project-stabilization-audit/tasks.md), item 1.3. The umbrella retains the baseline gate and is closed only with this task's accepted evidence.
