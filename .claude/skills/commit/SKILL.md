---
name: commit
description: Commit changes after verifying build and tests pass. Use when the user asks to commit, comitar, or save changes to git.
disable-model-invocation: true
---

Commit changes to git. Build and lint MUST pass before committing.

## Pre-commit gates (MANDATORY)

**Both gates must pass. If either fails, DO NOT commit. Fix the issue first.**

### Gate 1: Build + Lint

```bash
cd runtime && zig build 2>&1
```

If this fails, stop. Show the error and fix it before proceeding.

### Gate 2: Tests

```bash
cd runtime && zig build test 2>&1
```

If any test FAILS (not SKIPPED), stop. Show the failure and fix it before proceeding.

## Commit workflow

Only after BOTH gates pass:

1. Run `git status` and `git diff --stat` to see all changes
2. Run `git log --oneline -5` to match commit message style
3. Stage relevant files (prefer specific files over `git add -A`)
4. Draft a concise commit message:
   - Focus on the "why", not the "what"
   - 1-2 sentences max
5. Create the commit using HEREDOC format
6. Run `git status` to verify success

**Rules**:
- NEVER skip the build/test gates
- NEVER use `--no-verify`
- NEVER amend unless explicitly asked
- NEVER push unless explicitly asked
- Do NOT commit `.env`, credentials, or generated files
