---
name: analyze
description: Analyze UE5/Godot/Unity architecture patterns for a specific system. Delegates to architect agents (haiku) for cheap reference analysis. Use when designing a new engine system and need reference patterns.
---

# Engine Architecture Analysis

Analyze how reference engines solve a problem, then synthesize UzEngine-specific directives.

## Usage

- `/analyze <system> ue5` — Analyze how UE5 implements a system (reads W:\UE_5.6_Source)
- `/analyze <system> godot` — Analyze Godot 4's approach (reads E:\godot)
- `/analyze <system> unity` — Analyze Unity's approach (from knowledge)
- `/analyze <system> all` — Compare all three, synthesize for UzEngine

## Workflow

### Single engine:
Launch the appropriate architect agent (haiku — cheap for analysis):
- `unreal-architect` → reads actual UE5 source at W:\UE_5.6_Source
- `godot-architect` → reads actual Godot source at E:\godot
- `unity-architect` → from architectural knowledge

### All engines (`/analyze <system> all`):
1. Launch all 3 architect agents in parallel (haiku)
2. Launch `engine-architect` (sonnet) to synthesize findings into UzEngine directives
3. Output: concrete architecture recommendation for UzEngine

## Rules

- Clean-room only — study approach, reimplement from scratch
- NEVER copy code, NEVER mention engine names in code comments
- Only deviate from reference when more efficient alternative exists
- Output must be UzEngine-specific with MUST/SHALL/NEVER prescriptive language
