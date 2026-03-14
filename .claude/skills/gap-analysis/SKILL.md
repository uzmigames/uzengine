---
name: gap-analysis
description: Deep gap analysis comparing UzEngine's current state against a reference engine baseline. Analyzes code, specs, tasks, and produces a comprehensive report with feature matrix, priority ranking, and implementation roadmap.
---

# Deep Gap Analysis

Performs a comprehensive analysis of UzEngine's current implementation state vs a target visual baseline (e.g., "UE5 Basic Scene", "UE5 Starter Content quality").

## Usage

- `/gap-analysis <target>` — Full analysis against target baseline
- `/gap-analysis <target> --focus=<area>` — Focus on specific area (rendering, lighting, shadows, postprocess, materials, etc.)

Examples:
- `/gap-analysis ue5-basic-scene` — Compare against UE5 default scene quality
- `/gap-analysis ue5-basic-scene --focus=lighting` — Focus on lighting systems only

## Workflow

### Phase 1: Gather Current State (parallel, haiku agents)

Launch these agents in parallel:

1. **Explore agent** — Scan `src/engine/renderer/` for all implemented systems, shaders, passes
2. **Explore agent** — Scan `docs/specs/` and `docs/engine/` for all planned systems
3. **Explore agent** — Scan `.rulebook/tasks/` for all existing tasks and their status
4. **Explore agent** — Scan `shaders/` for all implemented shader features

### Phase 2: Reference Analysis (parallel, haiku architect agents)

Launch architect agents to analyze the target baseline:

1. **unreal-architect** — Analyze UE5's implementation of each system in the target baseline
2. **godot-architect** — Cross-reference with Godot's approach for simpler alternatives
3. **unity-architect** — Cross-reference with Unity's approach

### Phase 3: Synthesis (sonnet engine-architect)

1. **engine-architect** — Synthesize all findings into:
   - Feature matrix (UzEngine vs target baseline)
   - Gap list with severity (P0/P1/P2/P3)
   - Implementation order (dependency-aware)
   - Estimated complexity per feature
   - Tasks already created vs tasks needed

### Phase 4: Report Generation (docs-operator)

Generate report at `docs/analysis/<target>-gap-analysis-<date>.md`:

```markdown
# Gap Analysis: UzEngine vs <Target>
## Date: <date>
## Executive Summary
## Feature Matrix
## Detailed Gap Analysis
### Rendering Pipeline
### Lighting
### Shadows
### Post-Processing
### Materials
### Environment
### Scene Management
## Existing Task Coverage
## Missing Tasks
## Recommended Implementation Order
## Architecture Risks
```

## Rules

- Clean-room analysis only — study approach, never copy code
- Every gap must reference the specific UE5 source file/system analyzed
- Every gap must have a severity (P0=blocker, P1=critical, P2=important, P3=nice-to-have)
- Every gap must note if a Rulebook task already exists for it
- Report must be actionable — each gap has a concrete "what to implement" description
- Focus on visual parity, not feature parity — we want the same LOOK, not the same codebase
- All reports in English, saved to docs/analysis/
