# CLAUDE.md

This file provides guidance to Claude Code when working with this repository.

## Project Overview

This project uses @hivehub/rulebook standards. All code generation should follow the rules defined in AGENTS.md.

**Languages**: C
**Coverage Threshold**: 95%

## 🚨 MANDATORY: Test-First Implementation Gate

**ABSOLUTE RULE — NO EXCEPTIONS**: You MUST NOT move to the next implementation task until the current one has passing tests with ≥95% line + branch coverage. This is a hard gate, not a guideline.

### The Implementation Gate

Every implementation task MUST pass through this gate before being committed or moving on:

```
✅ GATE CHECKLIST (must ALL be green before proceeding):
□ 1. Code compiles with zero warnings (/W4 MSVC, -Wall -Wextra Clang/GCC)
□ 2. All tests pass (no failures, no skipped tests)
□ 3. Line coverage   ≥ 95%  (measured, not estimated)
□ 4. Branch coverage ≥ 95%  (all if/else, switch arms, error paths)
□ 5. No regressions — existing tests still pass
```

### Why This Is Non-Negotiable

- **No regression safety net**: untested code silently breaks when dependencies change
- **Refactoring becomes dangerous**: impossible to safely refactor without coverage
- **Spec drift**: code diverges from spec without test enforcement
- **AAA-quality bar**: production game engines cannot ship with coverage gaps

### Mandatory Workflow for Every Feature

```
❌ WRONG — implement then skip tests:
1. Implement feature A ──► commit ──► start feature B   ← NEVER DO THIS

✅ CORRECT — implement then gate:
1. Implement feature A
2. Write tests for feature A (100% coverage target, ≥95% required)
3. Run tests → all pass
4. Verify coverage ≥ 95% (line + branch)
5. Commit BOTH implementation + tests together
6. ──► Only now may you start feature B
```

### Test Coverage Rules

1. **NEVER commit implementation without tests** — implementation + tests go in the same commit
2. **NEVER skip a test case** — if a scenario exists in the spec, it must have a test
3. **NEVER use placeholder assertions** — `EXPECT_TRUE(true)` is forbidden
4. **ALWAYS test error paths** — every `if (error)` branch needs a test
5. **ALWAYS test edge cases** — null inputs, overflow, empty collections, boundary values
6. **ALWAYS test the happy path AND the failure path** for every public function
7. **Coverage tool**: Google Test + OpenCppCoverage (C++), Jest --coverage (TypeScript)
8. **Coverage threshold**: 95% minimum. Strive for 100%. Never accept less than 95%.

### Test File Convention

| Source file | Test file |
|-------------|-----------|
| `src/engine/core/arena.cpp` | `tests/core/test_arena.cpp` |
| `src/engine/renderer/rhi/rhi_dx12.c` | `tests/renderer/test_rhi_dx12.cpp` |
| `vscode-uzengine/src/viewport-manager.ts` | `vscode-uzengine/src/test/viewport-manager.test.ts` |

Test files live in `tests/` (C++) or alongside source with `.test.ts` suffix (TypeScript).

### When a Coverage Gap Is Found

If coverage drops below 95% after a commit:
1. **STOP** — do not start new work
2. Identify the uncovered lines/branches
3. Write tests specifically for those paths
4. Re-run coverage — must be ≥95% before continuing
5. Commit the test additions

---

## ⚠️ CRITICAL: File Editing Rules

**MANDATORY**: When editing multiple files, you MUST edit files **SEQUENTIALLY**, one at a time.

### Why Sequential Editing is Required

The Edit tool uses exact string matching for replacements. When multiple files are edited in parallel:
- The tool may fail to find the exact string in some files
- Race conditions can cause partial or corrupted edits
- Error recovery becomes impossible

### Correct Pattern

```
✅ CORRECT (Sequential):
1. Edit file A → Wait for confirmation
2. Edit file B → Wait for confirmation
3. Edit file C → Wait for confirmation

❌ WRONG (Parallel):
1. Edit files A, B, C simultaneously → Failures likely
```

### Implementation Rules

1. **NEVER call multiple Edit tools in parallel** for different files
2. **ALWAYS wait for each edit to complete** before starting the next
3. **Verify each edit succeeded** before proceeding
4. **If an edit fails**, retry that specific edit before moving on

## ⚠️ CRITICAL: Test Implementation Rules

**MANDATORY**: You MUST write **complete, production-quality tests**. Never simplify or reduce test coverage.

### Forbidden Test Patterns

```typescript
// ❌ NEVER do this - placeholder tests
it('should work', () => {
  expect(true).toBe(true);
});

// ❌ NEVER do this - skipped tests
it.skip('should handle edge case', () => {});

// ❌ NEVER do this - incomplete assertions
it('should return data', () => {
  const result = getData();
  expect(result).toBeDefined(); // Too weak!
});

// ❌ NEVER do this - "simplify" by removing test cases
// Original had 10 test cases, don't reduce to 3
```

### Required Test Patterns

```typescript
// ✅ CORRECT - complete test with proper assertions
it('should return user data with correct structure', () => {
  const result = getUserById(1);
  expect(result).toEqual({
    id: 1,
    name: 'John Doe',
    email: 'john@example.com',
    createdAt: expect.any(Date),
  });
});

// ✅ CORRECT - test edge cases and error paths
it('should throw NotFoundError when user does not exist', () => {
  expect(() => getUserById(999)).toThrow(NotFoundError);
});
```

### Test Implementation Rules

1. **NEVER simplify tests** - Implement the full, complete test as originally designed
2. **NEVER skip test cases** - Every test case in the spec must be implemented
3. **NEVER use placeholder assertions** - Each assertion must verify actual behavior
4. **ALWAYS test error paths** - Exceptions, edge cases, and failure modes
5. **ALWAYS maintain coverage** - Tests must achieve the project's coverage threshold (95%+)

## Critical Rules

1. **ALWAYS read AGENTS.md first** - Contains all project standards and patterns
2. **Edit files sequentially** - One at a time, verify each edit
3. **Write complete tests** - No placeholders, no simplifications
4. **Tests required** - Minimum 95% coverage for all new code
5. **Quality checks before committing**:
   - Type check / Compiler check
   - Lint (zero warnings)
   - All tests passing
   - Coverage threshold met
6. **Documentation** - Update /docs/ when implementing features

## Persistent Memory

This project uses a **persistent memory system** via the Rulebook MCP server.
Memory persists across sessions — use it to maintain context between conversations.

**MANDATORY: You MUST actively use memory to preserve context across sessions.**

### Auto-Capture

Tool interactions (task create/update/archive, skill enable/disable) are auto-captured.
But you MUST also manually save important context:

- **Architectural decisions** — why you chose one approach over another
- **Bug fixes** — root cause and resolution
- **Discoveries** — codebase patterns, gotchas, constraints
- **Feature implementations** — what was built, key design choices
- **User preferences** — coding style, conventions, workflow preferences
- **Session summaries** — what was accomplished, what's pending

### Memory Commands (MCP)

```
rulebook_memory_save    — Save context (type, title, content, tags)
rulebook_memory_search  — Search past context (query, mode: hybrid/bm25/vector)
rulebook_memory_get     — Get full details by ID
rulebook_memory_timeline — Chronological context around a memory
rulebook_memory_stats   — Database stats
rulebook_memory_cleanup — Evict old memories
```

### Session Workflow

1. **Start of session**: `rulebook_memory_search` for relevant past context
2. **During work**: Save decisions, bugs, discoveries as they happen
3. **End of session**: Save a summary with `type: observation`

## Commands

```bash
# Quality checks
npm run type-check    # TypeScript type checking
npm run lint          # Run linter
npm test              # Run tests
npm run build         # Build project

# Task management (if using Rulebook)
rulebook task list    # List tasks
rulebook task show    # Show task details
rulebook validate     # Validate project structure
```

## File Structure

- `AGENTS.md` - Main project standards and AI directives
- `/rulebook/` - Modular rule definitions
- `/docs/` - Project documentation
- `/tests/` - Test files

When in doubt, check AGENTS.md for guidance.
