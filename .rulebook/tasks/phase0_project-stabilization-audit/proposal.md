# Project stabilization audit and corrective backlog

## Why
The 2026-10-10 review found correctness risks in temporal rendering, ECS lifetime,
material inputs and editor threading, together with validation gates that can
report success without exercising the intended behavior. The user requested a
detailed analysis before resuming implementation, then requested that the analysis
be organized according to Rulebook. Findings need stable identifiers, evidence,
acceptance criteria and explicit links to existing work.

This task records that analysis and owns the resulting stabilization backlog.
Creating it does not mean that any runtime correction has been implemented or
that a GPU defect has been reproduced.

## What Changes
- Publish an English, numbered analysis under `docs/analysis/project-stabilization/`,
  with an index, 22 findings, observed checks, limitations and an execution plan.
- Distinguish source-confirmed defects, executed check results, performance risks
  requiring measurement and architectural recommendations.
- Prioritize reproducible builds and truthful validation, then resource/lifetime
  correctness, temporal rendering and material inputs, then measured optimization.
- Link overlapping work to its existing Rulebook task. The execution plan defines
  the remaining scope owned here without rewriting historical task claims.
- Define acceptance scenarios for stabilization in `specs/project-stabilization/spec.md`.
- Leave implementation and its validation unchecked until completed with evidence.

## Impact
- Affected specs: project-stabilization (proposed delta; not applied to canonical specs).
- Affected documentation: `docs/analysis/project-stabilization/` and this task.
- Affected code in this change: none. Candidate implementation areas are listed per finding.
- Breaking change: NO for this documentation change. Future handle, loader and IPC
  contract changes must document compatibility before implementation.
- User benefit: one reviewable entry point and a dependency-ordered backlog, with
  evidence sufficient to distinguish a confirmed code issue from an untested symptom.

## Review entry points

- [Analysis index](../../../docs/analysis/project-stabilization/README.md)
- [Observed checks and limitations](../../../docs/analysis/project-stabilization/01-scope-and-evidence.md)
- [Execution plan and ownership](../../../docs/analysis/project-stabilization/06-execution-plan.md)
- [Task design](design.md)
