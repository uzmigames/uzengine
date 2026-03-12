---
name: research
description: Explore the codebase, find patterns, gather context. Delegates to researcher agent (haiku) for cheap exploration. Use when you need to understand how something works, find files, or gather context before implementing.
---

# Codebase Research

Delegate research tasks to the **researcher** agent (haiku model — cheap for read-only exploration).

## Usage

- `/research <query>` — Find and analyze code related to a topic
- `/research how <system>` — Understand how a system works
- `/research where <pattern>` — Find where something is defined/used
- `/research deps <module>` — Map dependencies of a module

## Workflow

1. Launch the **researcher** agent with the query
2. The agent will:
   - Search with Glob/Grep across `runtime/`
   - Read relevant files to understand patterns
   - Map dependencies and call chains
   - Report findings as actionable summary
3. Present findings to the user

## Agent Configuration

- **Agent**: researcher
- **Model**: haiku (cost-efficient — read-only, no code generation)
- **Scope**: Read-only — NEVER modifies files

## Output Format

The agent should report:
- Key files found and their purposes
- Relevant type definitions and interfaces
- Existing patterns to follow
- Dependencies and relationships
- Potential risks or edge cases
