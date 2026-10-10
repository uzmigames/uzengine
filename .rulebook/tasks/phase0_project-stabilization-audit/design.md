# Stabilization audit design

## Context

The user requested a detailed review before restarting feature development, then
asked for Rulebook organization. This delivery is documentation and planning.
Implementation remains pending in the checklist. No engine source is changed by
organizing the audit and no new runtime/visual acceptance is inferred.

## Documentation structure

The repository's [analysis skill](../../../.claude/skills/analysis/SKILL.md) defines
an index, numbered thematic files, globally numbered findings and a final execution
plan. The [analysis index](../../../docs/analysis/project-stabilization/README.md)
is the entry point; the six numbered files separate:

1. Scope, revision limits and observed checks.
2. Rendering correctness, F-001–F-008.
3. Runtime/editor lifetime, F-009–F-012.
4. Build and verification gates, F-013–F-017.
5. Performance and maintainability, F-018–F-022.
6. Dependency-ordered execution, ownership and closure evidence.

Each finding records priority, confidence, source location, impact and acceptance.
P1 has 11 findings, P2 has 10 and P3 has one. Severity is not a claim of runtime
reproduction. The source tree had substantial uncommitted changes, so the HEAD
hash alone cannot reproduce the audit.

## Ownership decision

Use one stabilization umbrella rather than creating 22 overlapping implementation
tasks. Existing resource-state, FSR2, post-process, path-tracer and native-test
tasks retain their scope. The
[ownership table](../../../docs/analysis/project-stabilization/06-execution-plan.md#finding-to-task-ownership)
states exactly which acceptance gaps remain here and which existing work is a
dependency or historical reference. This task must not archive over unresolved
findings; any future handoff requires a concrete Rulebook follow-up first.

This is a reversible planning choice within the user's request. It does not
assign parallel agents, authorize feature expansion or alter existing task status.
The project's agent opt-in override takes precedence over the skill's suggested
agent/model metadata.

## Implementation constraints for subsequent work

- Re-read each cited path before editing: this is an evolving working tree.
- Diagnose and record the root cause before changing shaders. Use the available
  diagnostic switches and published references; add required shader citations.
- Correct UzEngine to match Falcor; do not change the reference to improve a score.
- Preserve path-agnostic runtime behavior and explicit unsupported-capability results.
- Treat resource state, content validity, ownership and successful initialization
  as separate contracts; sharing one boolean across them caused several findings.
- Preserve handle, scene and IPC compatibility deliberately when interfaces change.
- Profile before claiming speedups; split large modules only around useful ownership
  boundaries after the behavior is stable.

## Validation of this documentation delivery

Use Rulebook 7.4.0 `task validate phase0_project-stabilization-audit` and verify local
links, unique/continuous finding IDs, complete checklist/spec references, English
documentation, and checklist-only `tasks.md`. The installed CLI does not expose
`--strict`, so use its supported validator rather than claiming an unsupported mode.

Result on 2026-10-10: Rulebook 7.4.0 accepted the task. Structural verification
passed for 11 Markdown files, 118 local links, the continuous F-001–F-022 sequence,
checklist-only task content and complete finding references in the index,
ownership plan, checklist and spec. The validator warned that the implementation
documentation/tests tail is unchecked; that is intentional because the corrective
work has not been performed. No tail waiver or archive is appropriate yet.

The original audit's executed checks are recorded separately in file 01. Engine,
shader and editor validation for future corrections remains unchecked. The task
stays pending; no canonical spec delta is applied and no archive is performed.
