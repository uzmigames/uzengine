## ADDED Requirements

### Requirement: Actionable priority order
The project SHALL expose a deterministic task queue ordered by present impact
and prerequisites, with phase IDs retained only as identifiers.

#### Scenario: Selecting the next task
Given the current priority manifest and live Rulebook checklists
When the queue is generated
Then the first ready execution item is identified, with a concrete scope and rationale, independently of alphabetical phase order.

#### Scenario: Broad stabilization umbrella
Given an audit task containing early validation and later feature acceptance
When it appears in the queue
Then scoped milestones refer to exact checklist items and do not block one another through a dependency on the entire umbrella.

### Requirement: Distinguish execution from closure and reconciliation
The queue MUST separate executable tasks, stale or undefined scope, coordination
and completed-status closure candidates without rewriting their historical evidence.

#### Scenario: Completed status with open work
Given a task marked completed with unchecked requirements or an open question
When the queue is generated
Then it remains visible for reconciliation and cannot silently satisfy a prerequisite.

#### Scenario: Empty or stale task
Given a task with no proposal/checklist or a proposal superseded by later implementation
When priorities are reviewed
Then it has an explicit reconciliation reason instead of becoming an implementation assignment or being deleted.

### Requirement: Validate queue integrity
The generator SHALL reject missing classifications, unknown dependencies,
dependency cycles, reversed prerequisite order and invalid milestone selectors.

#### Scenario: Invalid planning input
Given a missing task reference, cyclic dependency or absent checklist item
When the queue is checked
Then validation fails with a specific reason and does not approve the plan.

#### Scenario: Archived predecessor
Given a prerequisite moved through Rulebook into the archive
When the queue is regenerated
Then the reference resolves to its archived record and its actual completion evidence determines readiness.

### Requirement: Durable views and explicit archive handoff
The priority manifest SHALL remain independent of the stock Rulebook phase-sorted
index, and the archive request SHALL remain visible in Claude's shared project context.

#### Scenario: Native Rulebook changes the index
Given a task create, update or archive regenerates the stock index
When the documented queue refresh runs
Then the priority views are restored from the manifest and current task data without losing task IDs or altering implementation status.

#### Scenario: Claude receives the shared handoff
Given the user's explicit request to archive completed tasks
When Claude reads the project context
Then the handoff identifies candidates, unresolved exceptions and required Rulebook closure checks without claiming that delivery or archive has already happened.
