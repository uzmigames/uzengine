# Using git stash for Uncommitted Work

**Category**: code
**Tags**: git, safety, data-loss, workflow

## Description

Using git stash, git stash pop, or git stash apply to manage uncommitted work. On 2026-03-13, git stash pop failed with conflicts and all local changes were irrecoverably lost — hours of work destroyed. The command is too dangerous for uncommitted work in this project.

## Example

// WRONG:\ngit stash\ngit stash pop  // Can lose all changes on conflict!\n\n// CORRECT:\ngit commit -m \"wip: current work in progress\"\n// OR\ngit checkout -b temp-branch && git commit -am \"wip\"

## When to Use

Never. This is absolutely forbidden in UzEngine.
