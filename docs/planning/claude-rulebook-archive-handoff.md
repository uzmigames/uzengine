# Handoff to Claude: archive completed Rulebook tasks

Date: 2026-10-10. Requested explicitly by the user during queue reorganization.
Status: request recorded in the shared repository; receipt by a running Claude session is not confirmed.

## Requested action

Claude: archive the genuinely completed Rulebook tasks using `rulebook_task`
(action `validate`, then `archive`) or `rulebook task validate <id>` followed by
`rulebook task archive <id>`. Keep the task IDs and history. Follow the project's
archive quality gates; do not bypass validation or declare missing tests passed.
For documentation-only tasks, use a justified tail waiver only where applicable.

The list below reflects metadata/checklist inspection, not fresh verification of
every implementation. Re-read each task and its closure evidence before archive.
Codex is organizing priorities; do not replace the priority manifest or overwrite
its generated queue by hand. After archives, run the queue refresh command in
AGENTS.override.md once available.

## First archive candidates

These 38 tasks have completed metadata and no unchecked checklist items.
This is eligibility for closure review, not automatic approval of their evidence.

- `phase0_capture-preload-determinism`
- `phase0_clouds-noise-assets`
- `phase0_lighting-block-b-01-pass-timings`
- `phase0_lighting-block-b-03-async-sdf-bake`
- `phase0_lighting-block-b-04-froxel-fog-correctness`
- `phase0_lighting-block-b-04b-contact-shadows-depth`
- `phase0_lighting-block-b-04c-depth-prepass`
- `phase0_lighting-block-b-05b-env-mis`
- `phase0_lighting-block-b-05c-sky-capture-intensity`
- `phase0_lighting-block-b-06-ddgi-dedup`
- `phase0_lighting-block-b-07-clustered-lights`
- `phase0_lighting-cluster-spot-cone`
- `phase0_lighting-mesh-winding-inversions`
- `phase0_lighting-split-sum-rough-metals`
- `phase0_lighting-sponza-mesh-decals`
- `phase0_lighting-sponza-recalibration`
- `phase0_lighting-ssr-cvars-once`
- `phase0_postprocess-chain-restore`
- `phase0_sponza-followup-01-dxr-ddgi-energy-parity`
- `phase0_sponza-followup-02-camera-teleport-cascade-lag`
- `phase0_sponza-followup-03-diag-3d-target-readback`
- `phase0_sponza-followup-04-decal-autolod-normal-consistency`
- `phase0_sponza-followup-05-stone-metallic-channel-audit`
- `phase0_sponza-followup-06-firefly-noise-indirect`
- `phase0_sponza-followup-07-taa-bloom-jitter-wobble`
- `phase0_sponza-followup-08-review-fixes`
- `phase0_sponza-followup-09-validate-flicker-capture`
- `phase0_sponza-followup-10-isolate-taa-window`
- `phase0_sponza-followup-11-isolate-flicker-display`
- `phase0_sponza-followup-12-isolate-shadow-lighting`
- `phase0_sponza-followup-13-isolate-gdf-motion`
- `phase0_sponza-followup-14-validate-flicker-correction`
- `phase0_sponza-parity-01-core-fixes`
- `phase0_sponza-parity-02-material-graph-energy`
- `phase0_sponza-parity-03-sdf-gi-radiance-cache`
- `phase0_sponza-parity-04-final-gather-reflections`
- `phase0_sponza-parity-05-loading-exposure-cli`
- `phase0_sponza-parity-06-qa-harness-validation`

## Exceptions: reconcile before archiving

- `phase0_brixelizer-gi-integration`: 6 unchecked items remain.
- `phase0_lighting-block-b-02-gdf-incremental`: 2 unchecked items remain.
- `phase0_lighting-block-b-05-layered-reflections`: 2 unchecked items remain.

Brixelizer was closed as superseded, not fully implemented: preserve that reason.
GDF incremental still contains a failed moving-object transient gate and a cost
item; layered reflections retains graph-dependency/coverage items. Verify existing
successors and assign each unresolved item to an explicit existing or new follow-up
before closing the parent. Do not check boxes merely to make archive validation pass.

## Completion report

Record archived task IDs, unresolved exceptions, successor links and gate results.
Leave pending/in-progress work untouched. Do not reset, revert, delete task folders
manually, or modify runtime code as part of this archive cleanup.

