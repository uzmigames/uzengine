---
name: docs
description: Create or update engine documentation. Delegates to docs-operator agent (haiku) for cost-efficient doc generation. Use when writing new docs, updating existing docs after code changes, or auditing doc coverage.
---

# Documentation Generation

Delegate documentation work to the **docs-operator** agent (haiku model — fast and cheap).

## Usage

- `/docs <topic>` — Create or update docs for a specific topic
- `/docs audit` — Audit all docs for gaps and inconsistencies
- `/docs sync` — Sync docs with recent code changes

## Workflow

1. Launch the **docs-operator** agent with the user's request
2. The agent will:
   - Read AGENTS.md and existing docs structure
   - Analyze relevant source code in `runtime/`
   - Create/update docs in `docs/engine/` following the numbered directory structure
   - Use Rust-style documentation, English language, comparison tables
3. Report results back to the user

## Agent Configuration

- **Agent**: docs-operator
- **Model**: haiku (cost-efficient for documentation tasks)
- **Scope**: `/docs/` directory only — never modifies source code

## Quality Rules

- Every doc MUST have: purpose, architecture diagram (ASCII), API examples, comparison table (UE5/Godot/Unity/UzEngine)
- Follow the existing `docs/engine/XX-topic/01-overview.md` naming convention
- Cross-reference related docs with relative links
- Include implementation phase table (Phase 1/2/3/4)
