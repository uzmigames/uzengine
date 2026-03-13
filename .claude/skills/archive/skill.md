---
name: archive
description: Archive a completed Rulebook task. Only archives tasks that are 100% complete (all checklist items checked). Uses the Rulebook MCP server.
---

# Rulebook Task Archiver

Archive completed tasks via the Rulebook MCP server. **Only tasks with 100% completion are eligible.**

## Usage

- `/archive <task-id>` — Archive a specific task (after validation)
- `/archive all` — Scan all active tasks and archive any that are 100% complete
- `/archive check` — List tasks and their completion status without archiving

## Rules

1. **NEVER archive a task that has any unchecked `- [ ]` items** — 100% means ALL items `[x]`, zero `[ ]`
2. **Always verify before archiving** — Read the `tasks.md` file and count checked vs unchecked items
3. **Use the MCP tool** — Archive via `mcp__rulebook__rulebook_task_archive`, NOT by manually moving directories
4. **Report what was archived** — Show task ID and final checklist summary

## Workflow

### For `/archive <task-id>`

1. Read `.rulebook/tasks/<task-id>/tasks.md`
2. Count all `- [x]` and `- [ ]` items
3. If ANY `- [ ]` exists → **REFUSE** and report which items are pending
4. If 100% complete → call `mcp__rulebook__rulebook_task_archive` with the task ID
5. Report success

### For `/archive all`

1. List all directories in `.rulebook/tasks/` (excluding `archive/`)
2. For each, read `tasks.md` and count completion
3. Archive only those with 0 pending items
4. Report: archived tasks + tasks still pending with their missing items

### For `/archive check`

1. List all directories in `.rulebook/tasks/` (excluding `archive/`)
2. For each, read `tasks.md` and count completion
3. Report table: task ID, done count, pending count, percentage
4. Do NOT archive anything

## Example Output

```
## Archive Check

| Task | Done | Pending | Status |
|------|------|---------|--------|
| phase3a-3.2.0-forward-pbr | 15/15 | 0 | ✅ Ready to archive |
| phase3b-3.1b-component-model | 16/17 | 1 | ❌ 2.8 coverage pending |
| phase4_4.8.0_system-information | 16/17 | 1 | ❌ 2.7 coverage pending |
```
