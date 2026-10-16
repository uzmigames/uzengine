# Reliable PowerShell quality gates

## Why

The approved stabilization audit identified F-016. This focused task implements umbrella item 1.2 in the first Windows stabilization milestone.

## What Changes

Resolve all gate paths from the script, inspect current engine sources using actual compilation configuration, and propagate required-tool and command failures.

## Impact

- Affected specs: project-stabilization.
- Affected code: scripts/tools/*.ps1.
- Breaking change: NO (invalid verification evidence now fails explicitly).
- Ownership: [audit checklist](../phase0_project-stabilization-audit/tasks.md), item 1.2. The umbrella retains the baseline gate and is closed only with this task's accepted evidence.
