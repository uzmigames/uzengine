# Context Budget — Scope work to avoid context overflow

## Tiers

| Task Type | Budget | Example |
|-----------|--------|---------|
| Read/review | ~3k tokens | "What does this shader do?" |
| Single-file fix | ~8k tokens | Bug fix in one .cpp or .hlsl |
| Feature (1-2 files) | ~15k tokens | New render pass, new component |
| Multi-subsystem (3+ files) | ~25k tokens | Data flow across ECS→CB→shader |

## Rules

1. **Before starting**, estimate which tier the task falls into
2. **Delegate to agents** for multi-subsystem work — don't try to hold 5+ files in main context
3. **Proactively compact** at ~60-70% context usage — don't wait for automatic compression
4. **Use data flow documents** (docs/analysis/dataflow/) to persist cross-file context instead of holding it all in memory
5. **One sub-task at a time** — complete, verify, then move to next. Don't read 5 files "for later"

## Anti-Patterns

- Reading 10 files upfront "to understand the codebase" → read 2-3, delegate rest to agents
- Holding full shader source in context while also editing C++ → use agent for one side
- Re-reading files already summarized in a data flow doc → trust the doc, verify only if suspicious

## Why

Context overflow causes: lost edits, repeated work, forgotten constraints, wrong variable names from compressed-away context. The cost of delegating to an agent is much lower than the cost of a context-induced bug.
