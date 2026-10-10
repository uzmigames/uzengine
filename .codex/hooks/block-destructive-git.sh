#!/usr/bin/env bash
# Hook: block ALL destructive git commands — ABSOLUTE PROHIBITION.
# This hook runs as PreToolUse on Bash commands and blocks any git command
# that could destroy, discard, or roll back work.
#
# Blocked commands:
#   git revert, git stash, git reset, git rebase,
#   git checkout -- <file>, git checkout .,
#   git restore <file>, git restore .,
#   git clean -f, git push --force, git push -f,
#   git branch -D, git branch -d

FULL_CMD=$(python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('tool_input',{}).get('command',''))" 2>/dev/null)

if [ -z "$FULL_CMD" ]; then
    exit 0
fi

# Extract only the first line — heredoc content in commit messages
# should not trigger false positives (e.g. "git commit -m 'fix revert bug'")
CMD=$(echo "$FULL_CMD" | head -1)

# Check for destructive git commands
BLOCKED=""

# git revert (any form)
echo "$CMD" | grep -qiE '\bgit\s+revert\b' && BLOCKED="git revert"

# git stash (any form)
[ -z "$BLOCKED" ] && echo "$CMD" | grep -qiE '\bgit\s+stash\b' && BLOCKED="git stash"

# git reset (any form — hard, soft, mixed)
[ -z "$BLOCKED" ] && echo "$CMD" | grep -qiE '\bgit\s+reset\b' && BLOCKED="git reset"

# git rebase (any form)
[ -z "$BLOCKED" ] && echo "$CMD" | grep -qiE '\bgit\s+rebase\b' && BLOCKED="git rebase"

# git clean -f (force clean)
[ -z "$BLOCKED" ] && echo "$CMD" | grep -qiE '\bgit\s+clean\b' && BLOCKED="git clean"

# git push --force or git push -f
[ -z "$BLOCKED" ] && echo "$CMD" | grep -qiE '\bgit\s+push\s.*(-f\b|--force)' && BLOCKED="git push --force"

# git checkout -- <file> or git checkout .
[ -z "$BLOCKED" ] && echo "$CMD" | grep -qiE '\bgit\s+checkout\s+--\s' && BLOCKED="git checkout --"
[ -z "$BLOCKED" ] && echo "$CMD" | grep -qiE '\bgit\s+checkout\s+\.\s*$' && BLOCKED="git checkout ."
[ -z "$BLOCKED" ] && echo "$CMD" | grep -qiE '\bgit\s+checkout\s+--\s*$' && BLOCKED="git checkout --"

# git restore <file> or git restore .
[ -z "$BLOCKED" ] && echo "$CMD" | grep -qiE '\bgit\s+restore\b' && BLOCKED="git restore"

# git branch -D or -d (delete)
[ -z "$BLOCKED" ] && echo "$CMD" | grep -qiE '\bgit\s+branch\s+-[dD]\b' && BLOCKED="git branch -D"

if [ -n "$BLOCKED" ]; then
    echo "{\"decision\":\"block\",\"reason\":\"BLOCKED: '${BLOCKED}' is a destructive git command and is absolutely prohibited. Use runtime CVars (--cvar r.Feature=0) to disable features for testing. Fix issues forward with new commits. NEVER roll back work.\"}"
    exit 0
fi

# Not a destructive command — allow
exit 0
