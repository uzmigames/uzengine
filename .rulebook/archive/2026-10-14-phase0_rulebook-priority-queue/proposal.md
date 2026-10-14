# Proposal: phase0_rulebook-priority-queue

## Why
The user cannot determine what to work on from the phase/alphabetical Rulebook
index. It mixes 41 completed-status tasks with executable work, stale proposals,
two empty task directories and broad roadmaps. Priority must reflect current
renderer/editor stability and dependencies, with completed work handed to Claude
for archive as explicitly requested by the user.

## What Changes
- Add a repository-owned priority manifest, explicit dependencies and scoped
  stabilization milestones without renaming task IDs or rewriting historical evidence.
- Generate a stable priority queue and the tasks index with separate execution,
  reconciliation, coordination and closure sections.
- Read checklist and metadata status dynamically, including archives; reject
  unclassified tasks, missing dependencies, cycles and invalid milestone selectors.
- Preserve native Rulebook status semantics; priority is a local planning overlay
  because Rulebook 7.4.0 exposes no priority field or priority-sort option.
- Record the Claude archive handoff and make next-session routing explicit.

## Impact
- Affected specs: task-priority-queue (new).
- Affected code: repository planning generator and its tests only; no engine changes.
- Affected docs/config: priority manifest, generated queue/index, shared context,
  archive handoff and persistent project overrides.
- Breaking change: NO; all task IDs, native statuses and existing checklists are preserved.
- User benefit: a justified next action and a maintainable queue that separates
  genuine blockers from feature expansion and obsolete scope.
