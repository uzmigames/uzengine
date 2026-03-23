#!/usr/bin/env bash
# statusline.sh — Claude Code status line for UzEngine
# Shows: branch | active phase task | task progress | gap count
# Adapted from Claude-Code-Game-Studios pattern.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# ── Git branch ──────────────────────────────────────────────────────────────
branch=""
if command -v git &>/dev/null && [ -d "$ROOT/.git" ]; then
    branch=$(cd "$ROOT" && git branch --show-current 2>/dev/null || echo "detached")
fi

# ── Active task phase (first unchecked item from most recent tasks.md) ──────
active_task=""
tasks_dir="$ROOT/.rulebook/tasks"
if [ -d "$tasks_dir" ]; then
    # Find most recently modified tasks.md
    latest=$(find "$tasks_dir" -name 'tasks.md' -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -1 | cut -d' ' -f2-)
    if [ -n "$latest" ] && [ -f "$latest" ]; then
        # Extract phase name from directory
        phase_dir=$(basename "$(dirname "$latest")")
        # Get first unchecked item
        first_pending=$(grep -m1 '^\- \[ \]' "$latest" 2>/dev/null | sed 's/^- \[ \] //' | head -c 60 || true)
        if [ -n "$first_pending" ]; then
            active_task="$phase_dir > $first_pending"
        else
            active_task="$phase_dir (all done)"
        fi
    fi
fi

# ── Task progress (done/total across all phases) ───────────────────────────
done_count=0
total_count=0
if [ -d "$tasks_dir" ]; then
    while IFS= read -r f; do
        d=$(grep -c '^\- \[x\]' "$f" 2>/dev/null || true)
        p=$(grep -c '^\- \[ \]' "$f" 2>/dev/null || true)
        done_count=$((done_count + d))
        total_count=$((total_count + d + p))
    done < <(find "$tasks_dir" -name 'tasks.md' 2>/dev/null)
fi

# ── Uncommitted changes count ──────────────────────────────────────────────
changes=""
if command -v git &>/dev/null && [ -d "$ROOT/.git" ]; then
    mod_count=$(cd "$ROOT" && git status --porcelain 2>/dev/null | wc -l)
    if [ "$mod_count" -gt 0 ]; then
        changes="${mod_count}ch"
    fi
fi

# ── Compose status line ────────────────────────────────────────────────────
parts=()
[ -n "$branch" ] && parts+=("$branch")
[ "$total_count" -gt 0 ] && parts+=("${done_count}/${total_count} tasks")
[ -n "$changes" ] && parts+=("$changes")
[ -n "$active_task" ] && parts+=("$active_task")

# Join with " | "
IFS=' | '
echo "${parts[*]}"
