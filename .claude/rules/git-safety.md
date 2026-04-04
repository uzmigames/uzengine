# Git safety — ABSOLUTE PROHIBITION on destructive operations

# Git Safety Rules

## Allowed (always safe)
- `git status`, `git diff`, `git log`, `git blame`
- `git add <files>`, `git commit` (after quality checks)

## ABSOLUTELY FORBIDDEN — NO EXCEPTIONS, NO AUTHORIZATION OVERRIDES

These commands are BANNED. Do not run them. Do not suggest them. Do not ask permission to run them. There is NO scenario where they are acceptable:

- `git revert` — DESTROYS WORK. Use runtime flags to disable features instead.
- `git reset` (any mode) — DESTROYS WORK.
- `git checkout -- .` / `git checkout -- <file>` — DESTROYS UNCOMMITTED WORK permanently.
- `git restore .` / `git restore <file>` — DESTROYS UNCOMMITTED WORK permanently.
- `git stash` — hidden state gets forgotten, effectively destroys work.
- `git rebase` — rewrites history, causes conflicts.
- `git push --force` — NEVER.
- `git clean -f` — permanently deletes untracked files.
- `git branch -D` — permanent branch deletion.
- `git checkout <branch>` / `git switch` — breaks concurrent AI sessions.

## When something is broken

- **FIX IT FORWARD** — write a new commit that corrects the issue
- **Use runtime flags** — `--cvar r.Feature=0` to disable features for testing
- **NEVER roll back** — the working tree may contain unsaved work from other sessions

Multiple AI sessions may share the same working tree. ANY destructive operation can destroy hours of work from other sessions.
