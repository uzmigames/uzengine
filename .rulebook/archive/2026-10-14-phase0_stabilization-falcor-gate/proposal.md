# Strict Falcor acceptance gate

## Why

The approved stabilization audit identified F-015. This focused task implements umbrella item 1.4 in the first Windows stabilization milestone.

## What Changes

Require matching image dimensions and capture metadata, preserve reference images, and accept only valid PSNR scores at or above 35 dB.

## Impact

- Affected specs: project-stabilization.
- Affected code: scripts/falcor/falcor_reference_test.py.
- Breaking change: NO (invalid verification evidence now fails explicitly).
- Ownership: [audit checklist](../phase0_project-stabilization-audit/tasks.md), item 1.4. The umbrella retains the baseline gate and is closed only with this task's accepted evidence.
