# MANDATORY: Update tasks.md After Every Completed Task

## Rule (applies to ALL agents)

After completing ANY task, fix, or feature, you MUST update the corresponding `.rulebook/tasks/*/tasks.md` file:

1. Find the matching checklist item(s) in the relevant `tasks.md`
2. Change `- [ ]` to `- [x]`
3. Add a brief description of what was done
4. If no existing item matches your work, add a new item under the appropriate section

## Task File Locations

```
.rulebook/tasks/phase0-0.8.0-uniform-buffer-system/tasks.md
.rulebook/tasks/phase0-0.4.0-static-shadow-cache/tasks.md
.rulebook/tasks/phase0-0.6.0-distance-field-shadows/tasks.md
```

## Example

Before:
```
- [ ] 8.5 DX11 IBL visual parity.
```

After:
```
- [x] 8.5 DX11 IBL visual parity. Fixed reversed-Z depth test (GreaterEqual), cubemap UAV dimension, compute sampler binding.
```

## Why

The `tasks.md` files are the **source of truth** for project progress. If you don't update them, the team lead and user cannot track what's done vs pending. This has caused confusion and wasted effort in past sessions.

## When

- BEFORE reporting completion to the team lead
- BEFORE going idle
- After EACH individual fix (don't batch — update as you go)

Failure to update tasks.md is considered an incomplete task.
