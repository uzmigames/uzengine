# Reproducible build and source provenance

## Why

The approved stabilization audit identified F-013 and F-017. This focused task implements umbrella item 1.1 in the first Windows stabilization milestone.

## What Changes

Align the C++23/Zig contract, retain npm lockfiles, inventory native dependency inputs and record current-source build provenance.

## Impact

- Affected specs: project-stabilization.
- Affected code: runtime/build.zig, runtime/CMakeLists.txt, package lockfiles, scripts/tools, CI.
- Breaking change: NO (invalid verification evidence now fails explicitly).
- Ownership: [audit checklist](../phase0_project-stabilization-audit/tasks.md), item 1.1. The umbrella retains the baseline gate and is closed only with this task's accepted evidence.
