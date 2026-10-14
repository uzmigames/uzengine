# UzEngine project stabilization audit

Date: 2026-10-10. Status: **stage-0 baseline complete; renderer/editor corrections remain open**.
Latest reconciliation: [current backlog, shipped mitigations and hierarchy rejection](10-backlog-reconciliation.md).
Rulebook task: [phase0_project-stabilization-audit](../../../.rulebook/tasks/phase0_project-stabilization-audit/proposal.md).

The review identifies 22 findings: **11 P1, 10 P2 and 1 P3**. The first work should
make builds and validation trustworthy, then correct state/lifetime contracts,
temporal inputs and material context. Performance and module cleanup follow a
verified correctness baseline.

The highest-risk evidence is not limited to visual polish: it includes ECS
generation storage indexed by packed IDs, unsynchronized editor/world mutation,
incorrect temporal history assumptions and gates that can pass without the
required tests or images. These are source findings and check results; they are
not all reproduced runtime failures or proven causes of the user's rendering bugs.

## Reading order

| File | Purpose |
|---|---|
| [01 — Scope and evidence](01-scope-and-evidence.md) | Revision limits, commands actually run and checks not performed |
| [02 — Rendering correctness](02-rendering-correctness.md) | F-001–F-008: temporal rendering, graph synchronization, materials and capability limits |
| [03 — Runtime and editor lifecycle](03-runtime-and-editor-lifecycle.md) | F-009–F-012: entities, threading, scene loading and hierarchy |
| [04 — Build and validation](04-build-and-validation.md) | F-013–F-017: CI, tests, Falcor gate, scripts and toolchain consistency |
| [05 — Performance and maintainability](05-performance-and-maintainability.md) | F-018–F-022: batching, VFS, IPC, paths and architecture |
| [06 — Execution plan](06-execution-plan.md) | Dependencies, existing task ownership, acceptance matrix and closure criteria |
| [07 — Baseline implementation](07-baseline-implementation.md) | Five implementation tasks, changes, executed checks and remaining gates |
| [08 — Native bootstrap](08-native-bootstrap.md) | Locked dependency provisioning and isolated native build evidence |
| [09 — Coverage and first frames](09-test-coverage-and-first-frames.md) | Current test results, startup captures and scoped baseline closure |
| [10 — Backlog reconciliation](10-backlog-reconciliation.md) | 2026-10-14 source assessment, graph residuals, capability limits and hierarchy rejection |

## Finding register

Every finding has source locations, impact, confidence and acceptance criteria
in its thematic file. IDs are global and stable within this analysis.

| ID | Priority | Finding | Evidence class |
|---|---|---|---|
| [F-001](02-rendering-correctness.md) | P1 | Temporal history state and validity are conflated | Source-confirmed |
| [F-002](02-rendering-correctness.md) | P1 | Mesh velocity contains camera motion only | Source-confirmed |
| [F-003](02-rendering-correctness.md) | P1 | Graph synchronization and resource declarations are incomplete | Source-confirmed; runtime exposure varies |
| [F-004](02-rendering-correctness.md) | P1 | Generated material graphs receive dummy context | Source-confirmed |
| [F-005](02-rendering-correctness.md) | P1 | Unavailable FSR2 remains selectable as an output producer | Source-confirmed |
| [F-006](02-rendering-correctness.md) | P2 | Internal tracer cannot independently prove rendering parity | Source-confirmed limitations; validation recommendation |
| [F-007](02-rendering-correctness.md) | P2 | Reviewed glass model assumes a sphere | Source-confirmed; intended contract needs definition |
| [F-008](02-rendering-correctness.md) | P2 | Exposed post-process controls include inactive effects | Source-confirmed |
| [F-009](03-runtime-and-editor-lifecycle.md) | P1 | ECS generation storage is indexed by packed EnTT identity | Source-confirmed |
| [F-010](03-runtime-and-editor-lifecycle.md) | P1 | Native editor commands can mutate the rendering world concurrently | Source-confirmed entry points; stress reproduction pending |
| [F-011](03-runtime-and-editor-lifecycle.md) | P1 | Scene-load success and failure side effects are inconsistent | Source-confirmed |
| [F-012](03-runtime-and-editor-lifecycle.md) | P2 | Scene parenting permits cycles | Source-confirmed |
| [F-013](04-build-and-validation.md) | P1 | CI lacks a reproducible clean-checkout contract | Source-confirmed lockfile gap; native bootstrap untested |
| [F-014](04-build-and-validation.md) | P1 | Tests can pass empty and coverage cannot run | Source-confirmed and executed checks |
| [F-015](04-build-and-validation.md) | P1 | Falcor gate approves some missing/sub-threshold evidence | Source-confirmed |
| [F-016](04-build-and-validation.md) | P2 | PowerShell gates resolve the wrong root/source scope | Source-confirmed |
| [F-017](04-build-and-validation.md) | P2 | Language and toolchain contracts disagree | Source/documentation drift |
| [F-018](05-performance-and-maintainability.md) | P2 | Batch preparation runs while instanced drawing is disabled | Source-confirmed; cost to measure |
| [F-019](05-performance-and-maintainability.md) | P2 | Loose-file VFS reads serialize on a shared lock | Source-confirmed; cost to measure |
| [F-020](05-performance-and-maintainability.md) | P2 | Editor queues and pending requests lack complete bounds | Source-confirmed; saturation behavior to measure |
| [F-021](05-performance-and-maintainability.md) | P2 | DLSS discovery assumes an absolute Windows path | Source-confirmed project-rule violation |
| [F-022](05-performance-and-maintainability.md) | P3 | Module boundaries, shared layouts and capability claims need maintenance | Architectural recommendation |

## Initial audit evidence

TypeScript checks passed in extension and MCP; MCP ran 68 tests successfully.
Extension lint failed, its Vitest command collected no tests while exiting zero,
and both coverage commands failed for a missing provider. Asset JSON and Python
syntax checks found no syntax errors in the inspected files.

There was **no current-source C++ build, GPU reproduction or Falcor PSNR result**.
The working tree included substantial uncommitted changes; its HEAD hash is not
a complete snapshot. See 01 for exact scope and command results. These are the
initial observations; reports 07–09 record the subsequent implementation and checks.

## How to resume

Follow the [execution plan](06-execution-plan.md) and
[Rulebook checklist](../../../.rulebook/tasks/phase0_project-stabilization-audit/tasks.md).
Recheck current source and existing task progress before each correction. Record
root cause and diagnostics before shader edits, retain Falcor as the reference,
and close an item only with its stated acceptance evidence. Existing task scopes
are linked rather than duplicated. Audit items 1.1–1.5 are now complete within
stage 0; the umbrella and its remaining corrective milestones stay open.
