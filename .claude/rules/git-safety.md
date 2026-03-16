# Git Safety

## ABSOLUTELY FORBIDDEN
- `git stash` (any subcommand) — destroyed hours of work on 2026-03-13
- `git checkout <branch>` / `git switch` / `git checkout -b` — multiple tabs share working tree
- `git push --force` to main/master
- `git reset --hard` without user confirmation

## What To Do Instead
- Commit first: `git commit -m "wip: ..."` before dangerous operations
- Use `git diff` to compare without switching branches
- Ask the user to switch branches manually
