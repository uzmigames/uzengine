---
name: audit
description: Audit code for structural compliance, demo patterns, test coverage, and documentation. Run after PR merges or feature implementations.
model: sonnet
context: fork
agent: implementation-auditor
---
Run the implementation auditor on the specified scope.

If $ARGUMENTS is provided, audit that specific directory, PR, or commit range.
Otherwise, audit all uncommitted/recently committed changes.

Examples:
- `/audit` — audit uncommitted changes
- `/audit runtime/engine/vfx/` — audit the VFX subsystem
- `/audit HEAD~5..HEAD` — audit last 5 commits
- `/audit PR#6` — audit a specific PR's changes

Steps:
1. Determine scope (from args or git diff)
2. Run all 7 audit categories (structure, demos, integration, tests, docs, quality, build)
3. Report PASS/WARN/FAIL for each check
4. Provide summary with recommendation (APPROVE / NEEDS WORK)