# 06. Execution plan and ownership

For the cross-project execution order, start with the
[live Rulebook priority queue](../../../.rulebook/PRIORITIES.md). It orders this
audit's scoped milestones alongside other tasks and separates stale scope from
current work. This document remains the audit's detailed acceptance/ownership plan.

Return to the [index](README.md). Tracking task:
[phase0_project-stabilization-audit](../../../.rulebook/tasks/phase0_project-stabilization-audit/proposal.md).
Its [checklist](../../../.rulebook/tasks/phase0_project-stabilization-audit/tasks.md)
and [spec delta](../../../.rulebook/tasks/phase0_project-stabilization-audit/specs/project-stabilization/spec.md)
are the operational counterparts of this plan.

## Scope of the current delivery

The initial analysis is preserved in files 01–06. The approved baseline
implementation is now tracked in five focused tasks linked from
[07 — Baseline implementation](07-baseline-implementation.md), with locked native
builds in [08](08-native-bootstrap.md) and final coverage/first-frame evidence in
[09](09-test-coverage-and-first-frames.md). Stage 0 is complete: current build inputs,
honest validation outcomes and startup diagnostics are recorded. Native suite
failures and renderer/editor corrections remain with their named owners; final
visual acceptance is stage 3.
Do not archive the stabilization task while corrective items remain open. Before handing an item off, create its follow-up through
Rulebook and record the link; do not leave ownership only in prose.

An existing task link is a scope reference, not a declaration that its historical
results passed for the current tree. In particular, a checked historical task
cannot close a newly found acceptance gap without fresh evidence.

## Dependency-ordered stages

| Stage | Work | Prerequisite and exit evidence |
|---|---|---|
| 0 — Trust the measurement | F-013, F-014, F-015, F-016, F-017; snapshot/build manifest; baseline diagnostics | Establish a reproducible native/TS environment, correct discovery and failure statuses; publish actual pass/fail/skip results. GPU fixes need a current diagnostic baseline before editing. |
| 1 — Protect state and lifetime | F-009, F-010, F-011, F-012, F-020, F-021 | Native build/tests from stage 0. Entity churn, scene failure, hierarchy and concurrent editor stress cases have explicit outcomes. Queue shutdown/timeouts depend on the ownership model. |
| 2 — Correct rendering contracts | F-003, then F-001/F-005; F-002 and F-004; F-006/F-007/F-008 capability boundaries | State tracking before temporal-state acceptance; valid velocity before moving-object temporal quality claims; real material inputs before graph image comparisons. Existing owners retain their implementation scope. |
| 3 — Verify images and sequences | All corrected rendering paths; matched Falcor and motion/resize/switch matrix | Stage 0 gates plus each relevant stage 2 correction. No applicable reference below 35 dB; no missing comparison represented as a pass; temporal checks use sequences. |
| 4 — Measure and simplify | F-018, F-019, F-022 | Correctness baseline from prior stages. Publish CPU/GPU/I/O results; make bounded refactors with unchanged accepted output and compatible data layouts. |

Independent diagnostics may run before unrelated corrections finish. The ordering
does not require waiting for all tooling work to read code or gather evidence.
It does require the relevant prerequisite before accepting a dependent change.
This is a dependency plan, not a request for parallel agents.

## Finding-to-task ownership

Here means `phase0_project-stabilization-audit`. New work remains here unless a
linked follow-up is created. Related historical tasks are context, not duplicate
implementation assignments.

| Finding | Accountable scope / existing relationship |
|---|---|
| F-001 | Existing [resource-state registry](../../../.rulebook/tasks/phase0_render-graph-state-registry/proposal.md) owns manual-state migration; here owns separate temporal-content validity and reset acceptance. The [jitter/wobble fix](../../../.rulebook/archive/2026-10-10-phase0_sponza-followup-07-taa-bloom-jitter-wobble/proposal.md) is historical context, not a newly open jitter defect. |
| F-002 | Here: motion-vector contract and coverage; coordinate with [skeletal GPU skinning](../../../.rulebook/tasks/phase4-4.3.0-skeletal-mesh-gpu-skinning/proposal.md) for previous-pose data. |
| F-003 | Existing [resource-state registry](../../../.rulebook/tasks/phase0_render-graph-state-registry/proposal.md) owns import/manual-state migration and missing reads; here owns any remaining UAV-to-UAV/transient-state acceptance not in that task. Reconcile before coding. |
| F-004 | Here: real generated-material input context. [Material graph pipeline](../../../.rulebook/tasks/phase2_material-graph-pipeline/proposal.md) and [material energy](../../../.rulebook/archive/2026-10-10-phase0_sponza-parity-02-material-graph-energy/proposal.md) provide integration context. |
| F-005 | Here: unsupported-selection/output-validity guard; [FSR2 integration](../../../.rulebook/tasks/phase2_fsr2-full-integration/proposal.md) owns the SDK-enabled implementation and its quality evidence. |
| F-006 | Here: validation claims and capability limits; [runtime path tracer](../../../.rulebook/tasks/phase0_path-tracer-runtime-mode/proposal.md) and [Falcor path tracer](../../../.rulebook/tasks/phase1_falcor-path-tracer/proposal.md) retain full implementation scope. |
| F-007 | Here: explicit geometry/material contract and its checks; coordinate future general transmission with [layered materials](../../../.rulebook/tasks/phase1_falcor-layered-materials/proposal.md). |
| F-008 | Existing [post-process chain task](../../../.rulebook/archive/2026-10-10-phase0_postprocess-chain-restore/proposal.md) owns current capability documentation. Here verifies accurate exposure; implementing every absent effect is outside stabilization. |
| F-009 | Here: bounded ECS generation bookkeeping and handle compatibility. |
| F-010 | Here: world ownership and queued native commands. |
| F-011 | Here: transactional scene-load outcome and state consistency. [Scene loader](../../../.rulebook/tasks/phase3b-3.4b-scene-loader/proposal.md) is prior implementation context. |
| F-012 | Here: cycle-safe parenting across all entry points. |
| F-013 | Here: lockfiles, dependency bootstrap and complete clean-checkout CI. |
| F-014 | Here: TS discovery, lint, coverage and fixture semantics. Existing [native test failures](../../../.rulebook/tasks/phase0_preexisting-test-failures/proposal.md) owns its native failure list; new failures need explicit ownership. |
| F-015 | Here: Falcor gate exit status and input/metadata checks. [QA harness validation](../../../.rulebook/archive/2026-10-10-phase0_sponza-parity-06-qa-harness-validation/proposal.md) supplies historical harness context, not a current PSNR pass. |
| F-016 | Here: quality-script roots, source scope and exit propagation. |
| F-017 | Here: C++/Zig contract and documentation alignment. |
| F-018 | Here: unused batching cost and correctness prerequisite for instancing. |
| F-019 | Here: loose-file concurrency measurement and lock scope. |
| F-020 | Here: bounded native/IPC queues and request lifecycle, after F-010 ownership. |
| F-021 | Here: path-agnostic DLSS discovery; [DLSS integration](../../../.rulebook/tasks/phase1_falcor-dlss-integration/proposal.md) remains the broader capability task. |
| F-022 | Here: capability matrix, layout checks and targeted responsibility boundaries. |

## Rendering acceptance matrix

Every run records the provenance described in 01. Use supported backend/hardware
combinations and explicitly report unavailable cases.

| Axis | Required cases | Main evidence |
|---|---|---|
| Camera | Static, movement, cut/teleport | Consecutive frames, history reset and velocity diagnostics |
| Geometry | Static, rigid movement, skinned movement, supported deformation, nonuniform scale | Velocity/material buffers and image sequences |
| Resources | First frame, second frame, resize, recreation, transient reuse | State-truth tests and GPU validation |
| Scene/editor | Load success/failure, scene switch while rendering, resize and shutdown | Command results, state consistency, bounded queues |
| Temporal options | TAA off/on, supported upscaler switches, unavailable backend, initialization failure | Valid producer/output every frame; no stale history |
| Materials | Time, view/object position, screen coordinates, vertex colors, supported glass geometry | Dedicated graph fixtures at multiple resolutions |
| Reference | Matched scene/camera/exposure/dimensions, missing file, wrong dimensions, below-threshold score | Required comparisons succeed only at PSNR >= 35 dB; negative controls fail |

## Definition of closure

Each closed finding links its implementation revision, diagnostic reproduction,
targeted regression and applicable image/sequence evidence. A limitation is an
explicit supported-capability contract, not a passing result for missing behavior.
Performance items require recorded measurements. The Rulebook documentation/tests
tail is completed only after actual implementation and verification; documentation
format validation alone does not close the corrective backlog.
