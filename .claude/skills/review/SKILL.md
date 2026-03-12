---
name: review
description: Code review C++ changes for quality, safety, and UzEngine conventions. Delegates to cpp-core-expert agent (opus) for thorough review. Use before committing significant C++ changes.
---

# C++ Code Review

Delegate code review to the **cpp-core-expert** agent (opus model — thorough, high-quality review).

## Usage

- `/review` — Review all unstaged/staged changes
- `/review <file>` — Review a specific file
- `/review recent` — Review changes since last commit

## What Gets Checked

1. **Memory safety** — leaks, dangling pointers, use-after-free, RAII compliance
2. **Engine API usage** — UZ_LOG not spdlog, UZ_CHECK not assert, engine allocators not raw new
3. **Documentation** — Rust-style `///` on all public items, detailed enough for API docs
4. **SOLID principles** — single responsibility, dependency inversion, interface segregation
5. **Naming** — meaningful names, no abbreviations, consistent conventions
6. **Thread safety** — data races, missing synchronization
7. **Performance** — unnecessary copies, missing move semantics, cache-unfriendly patterns
8. **Security** — no command injection, no buffer overflows, validated inputs at boundaries

## Agent Configuration

- **Agent**: cpp-core-expert
- **Model**: opus (highest quality for critical code review)

## Output Format

For each issue found:
```
[SEVERITY] file:line — Description
  Suggested fix: ...
```

Severities: CRITICAL (must fix), WARNING (should fix), STYLE (nice to fix)
