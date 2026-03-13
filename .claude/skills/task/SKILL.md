---
name: task
description: Create Rulebook tasks with full proposal, checklist, and OpenSpec specs. Use when planning new milestones, features, or systems. Generates proposal.md, tasks.md, and specs/ with Given/When/Then scenarios.
---

# Rulebook Task Creator

Create production-quality Rulebook tasks with proposal, implementation checklist, and OpenSpec-compatible specifications.

## Usage

- `/task create <task-id>` — Create a new task with full structure
- `/task create <task-id> "<title>"` — Create with explicit title
- `/task list` — List all tasks
- `/task show <task-id>` — Show task details
- `/task validate <task-id>` — Validate task format

## Workflow for `/task create`

### Step 1 — Gather Context

Before writing anything, research the task scope:

1. **Read relevant engine docs** — Check `docs/engine/` for specs related to the system
2. **Read the roadmap** — Check `docs/ROADMAP.md` for the milestone definition, dependencies, and acceptance criteria
3. **Check existing tasks** — Run `rulebook task list` to avoid duplicates
4. **Check existing code** — Search `runtime/` for any existing implementation to understand current state

### Step 2 — Create Task via Rulebook MCP

```
mcp__rulebook__rulebook_task_create(taskId: "<task-id>")
```

This creates the directory `.rulebook/tasks/<task-id>/` with template files.

### Step 3 — Write proposal.md

Read the template first, then overwrite with:

```markdown
# Proposal: <Title> (<Phase>)

## Why
<Minimum 20 characters. Explain the problem this solves and why it's needed NOW.
Reference specific pain points, downstream dependencies, or blocking issues.>

## What Changes

### New Systems
<Numbered list of new systems/components being added.
Each item: name + one-sentence description of what it does.>

### Refactoring
<What existing code changes. Skip if purely additive.>

## Impact
- Affected specs: <docs/engine/ paths>
- Affected code: <runtime/ paths>
- Breaking change: YES/NO — <explain if YES>
- User benefit: <concrete benefit>
- Enables: <what downstream systems this unblocks>
```

### Step 4 — Write tasks.md

Simple checkbox checklist organized by phase:

```markdown
## 1. Implementation
- [ ] 1.1 <specific, actionable task>
- [ ] 1.2 <specific, actionable task>
...

## 2. Testing
- [ ] 2.1 <specific test: what is tested and what is verified>
- [ ] 2.2 <specific test>
...
- [ ] 2.N Verify 95%+ coverage on all new code

## 3. Documentation
- [ ] 3.1 Update <specific doc path> with <what>
- [ ] 3.2 <specific doc task>
```

**Rules for tasks.md:**
- NO technical details, NO code examples — those go in specs/
- Each task is one concrete action (implement X, test Y, update Z)
- Testing section MUST include error/edge case tests
- Documentation section MUST reference specific doc paths
- Always end testing with coverage verification task

### Step 5 — Write specs/\<module\>/spec.md

Create spec directory and write OpenSpec-compatible delta:

```markdown
# <System Name> — Spec Delta

## ADDED Requirements

### Requirement: <Name>
The <system> SHALL/MUST <do something specific and testable>.
<Additional detail about behavior, constraints, guarantees.>

#### Scenario: <Descriptive Name>
Given <precondition>
When <action occurs>
Then <expected outcome>
And <additional verification>

### Requirement: <Next Requirement>
...
```

**CRITICAL format rules:**
- Delta headers: `## ADDED` / `## MODIFIED` / `## REMOVED` / `## RENAMED`
- Requirements: `### Requirement: <Name>` — MUST contain SHALL or MUST keywords
- Scenarios: `#### Scenario: <Name>` — MUST use 4 hashtags (NOT 3)
- Scenarios: MUST use Given/When/Then structure
- Each requirement SHOULD have 1-3 scenarios covering happy path + error cases
- Requirements must be testable — no vague language like "should be good"

### Step 6 — Validate

```
mcp__rulebook__rulebook_task_validate(taskId: "<task-id>")
```

Fix any validation errors before reporting success.

## Task ID Naming Convention

Use this pattern for milestone tasks:

```
phase<N>_<N>.<N>.<N>_<kebab-name>
```

Examples:
- `phase2_2.0.0_application-lifecycle`
- `phase2_2.1.0_job-system`
- `phase3_3.0.0_triangle-to-screen`
- `phase4_4.2.0_physics-integration`

For non-milestone tasks, use verb-led kebab-case:
- `fix-shader-cache-invalidation`
- `add-vulkan-backend`
- `refactor-rhi-barriers`

## Quality Checklist

Before marking a task as complete, verify:

- [ ] proposal.md Why section ≥ 20 characters
- [ ] proposal.md references specific docs/code paths
- [ ] tasks.md has Implementation + Testing + Documentation sections
- [ ] tasks.md testing includes error/edge cases
- [ ] specs/ has at least one spec.md
- [ ] All requirements contain SHALL or MUST
- [ ] All scenarios use `#### Scenario:` (4 hashtags)
- [ ] All scenarios have Given/When/Then
- [ ] `rulebook task validate` passes with zero errors

## Example — Complete Task Structure

```
.rulebook/tasks/phase2_2.1.0_job-system/
├── proposal.md                     # Why: single-threaded wastes cores
│                                   # What: worker pool, MPMC queue, Counter, ParallelFor
│                                   # Impact: enables parallel shader compile, culling, physics
├── tasks.md                        # 12 impl tasks, 9 test tasks, 2 doc tasks
└── specs/
    └── job-system/
        └── spec.md                 # 9 ADDED requirements, 14 scenarios
```

## Delegation

- This skill runs in the **main conversation** (opus) for planning quality
- After task creation, optionally delegate to `docs-operator` (haiku) if doc updates are needed
- Use `researcher` (haiku) agent for codebase exploration before writing specs
