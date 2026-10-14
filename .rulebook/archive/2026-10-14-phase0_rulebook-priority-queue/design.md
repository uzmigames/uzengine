# Priority queue decisions

## Source of truth

Native Rulebook remains responsible for task creation, status and archive. Its
7.4.0 task API has no priority field, and its generated index sorts by phase/name.
The repository therefore keeps a local overlay in `.rulebook/task-priorities.json`.
`scripts/tools/rulebook_queue.py` reads that plan and live checklists/metadata to
generate `.rulebook/PRIORITIES.md` and `.rulebook/tasks/README.md`. No installed
Rulebook package is patched. After native mutations, refresh both views.

## Priority policy

The current objective is stable Windows rendering and editor operation. P0 covers
reproducible builds, truthful gates, state/lifetime defects and invalid evidence.
P1 addresses current visible defects and correctness. P2 finishes existing paths
and measures performance. P3 holds platform, gameplay and appearance expansion.
Phase IDs, age, old self-declared P0/P1 labels and percentage checked do not decide
priority. Review-only entries are explicitly excluded from automatic execution.

Dependencies encode the current complete task scope. If a task is later split,
reassess its prerequisites. In particular, the broad stabilization audit is a
coordinator with scoped milestones: its early build work must not wait for its
own late rendering/performance acceptance. Gates target specific checklist items
rather than depending on completion of the entire umbrella.

## Preserve evidence and ownership

All task IDs, historical checklist ticks and native statuses are preserved.
Completed metadata with open work is visible as a reconciliation exception, not
hidden as done. Old active metadata is displayed but does not override priority.
Empty task directories are retained in reconciliation until their scope is known.
No runtime source changes or implementation claims are part of this work.

The user's request for Claude to archive completed tasks is recorded in
`docs/planning/claude-rulebook-archive-handoff.md` and the user-owned section of
`CLAUDE.md`. No live delivery acknowledgement is claimed. Initial inventory:
38 completed/no-open-item candidates; three completed-status exceptions. Counts
in generated views are live and can change when Claude archives work.

## Checks

The generator rejects duplicate classifications, unclassified non-completed tasks,
missing task/dependency/gate references, dependency cycles, reversed prerequisite
order and missing/ambiguous milestone item IDs. Archives remain resolvable, while
archived/superseded work with unchecked items does not satisfy dependencies.
Unit tests exercise those failure modes using isolated temporary task fixtures.
`--check` is read-only and rejects stale generated views. Repeated generation must
be byte-identical when inputs are unchanged.

Validation on 2026-10-10: Python compile checks passed; all nine behavioral tests
passed; the live queue passed `--check`; 248 local links across seven planning
documents resolved at verification time; Rulebook 7.4.0 accepted the task format.
Other workspace activity was archiving tasks during organization, so inventory
and closure-candidate counts are intentionally generated rather than frozen.
`STATE.md` remains managed by Rulebook; its legacy active-task pointer does not
override the explicit priority policy in `AGENTS.override.md` and `PLANS.md`.
