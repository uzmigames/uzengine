#!/usr/bin/env bash
# detect-gaps.sh — Automated gap detection hook for UzEngine
# Runs at session start (PreToolUse) to flag codebase inconsistencies.
# Adapted from Claude-Code-Game-Studios pattern.
#
# Checks:
#   1. Shaders without @ref citation
#   2. C++ rendering functions without @ref citation
#   3. tasks.md items marked [x] sanity (count summary)
#   4. .hlsl.old leftover files
#   5. Uncommitted shader changes
#   6. Missing specs for existing systems

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GAPS=()

# ── 1. Shaders without @ref citation ──────────────────────────────────────────
shader_dir="$ROOT/runtime/shaders/runtime"
if [ -d "$shader_dir" ]; then
    missing_ref=0
    missing_files=""
    while IFS= read -r -d '' hlsl; do
        # Skip include files (helpers don't always need @ref on every function)
        if [[ "$hlsl" == *"/include/"* ]]; then
            continue
        fi
        # Check if file has at least one @ref citation
        if ! grep -q '@ref' "$hlsl" 2>/dev/null; then
            missing_ref=$((missing_ref + 1))
            rel="${hlsl#$ROOT/}"
            missing_files="$missing_files\n    - $rel"
        fi
    done < <(find "$shader_dir" -name '*.hlsl' -print0 2>/dev/null)

    if [ "$missing_ref" -gt 0 ]; then
        GAPS+=("SHADER-REF: $missing_ref shader files without @ref citation:$missing_files")
    fi
fi

# ── 2. C++ renderer files without @ref citation ──────────────────────────────
renderer_dir="$ROOT/runtime/engine/renderer"
if [ -d "$renderer_dir" ]; then
    missing_cpp_ref=0
    missing_cpp=""
    while IFS= read -r -d '' cpp; do
        if ! grep -q '@ref' "$cpp" 2>/dev/null; then
            missing_cpp_ref=$((missing_cpp_ref + 1))
            rel="${cpp#$ROOT/}"
            missing_cpp="$missing_cpp\n    - $rel"
        fi
    done < <(find "$renderer_dir" -name '*.cpp' -print0 2>/dev/null)

    if [ "$missing_cpp_ref" -gt 0 ]; then
        GAPS+=("CPP-REF: $missing_cpp_ref renderer .cpp files without @ref citation:$missing_cpp")
    fi
fi

# ── 3. Leftover .hlsl.old files ──────────────────────────────────────────────
old_files=$(find "$ROOT/runtime/shaders" -name '*.hlsl.old' 2>/dev/null | wc -l)
if [ "$old_files" -gt 0 ]; then
    old_list=""
    while IFS= read -r f; do
        rel="${f#$ROOT/}"
        old_list="$old_list\n    - $rel"
    done < <(find "$ROOT/runtime/shaders" -name '*.hlsl.old' 2>/dev/null)
    GAPS+=("STALE-FILES: $old_files leftover .hlsl.old files (should be deleted):$old_list")
fi

# ── 4. TODO/FIXME/HACK in runtime source ────────────────────────────────────
if [ -d "$ROOT/runtime" ]; then
    todo_count=$(grep -rn 'TODO\|FIXME\|HACK\|TEMP\|placeholder' \
        "$ROOT/runtime/engine" "$ROOT/runtime/shaders" \
        --include='*.cpp' --include='*.h' --include='*.hlsl' --include='*.hlsli' \
        2>/dev/null | grep -v 'thirdparty' | wc -l)
    if [ "$todo_count" -gt 0 ]; then
        GAPS+=("FORBIDDEN-MARKERS: $todo_count TODO/FIXME/HACK/TEMP/placeholder markers in runtime source (rule: no stubs)")
    fi
fi

# ── 5. Task progress summary ────────────────────────────────────────────────
tasks_dir="$ROOT/.rulebook/tasks"
if [ -d "$tasks_dir" ]; then
    total_done=0
    total_pending=0
    while IFS= read -r tasks_file; do
        done_count=$(grep -c '^\- \[x\]' "$tasks_file" 2>/dev/null || true)
        pending_count=$(grep -c '^\- \[ \]' "$tasks_file" 2>/dev/null || true)
        total_done=$((total_done + done_count))
        total_pending=$((total_pending + pending_count))
    done < <(find "$tasks_dir" -name 'tasks.md' 2>/dev/null)
    # Just informational, not a gap
    echo "TASKS: $total_done done, $total_pending pending across all phases"
fi

# ── 6. Uncommitted shader changes ───────────────────────────────────────────
if command -v git &>/dev/null && [ -d "$ROOT/.git" ]; then
    shader_changes=$(cd "$ROOT" && git diff --name-only -- 'runtime/shaders/' 2>/dev/null | wc -l)
    shader_staged=$(cd "$ROOT" && git diff --cached --name-only -- 'runtime/shaders/' 2>/dev/null | wc -l)
    total_shader_changes=$((shader_changes + shader_staged))
    if [ "$total_shader_changes" -gt 5 ]; then
        GAPS+=("UNCOMMITTED-SHADERS: $total_shader_changes shader files with uncommitted changes — consider committing")
    fi
fi

# ── 7. Magic numbers in shader entry points (non-include .hlsl) ─────────────
if [ -d "$shader_dir" ]; then
    magic_count=0
    while IFS= read -r -d '' hlsl; do
        [[ "$hlsl" == *"/include/"* ]] && continue
        # Look for bare float literals that aren't 0.0, 1.0, 0.5, -1.0, 2.0, 3.14159
        hits=$(grep -cP '(?<!\w)\d+\.\d{3,}(?!f?\s*[;/])' "$hlsl" 2>/dev/null || true)
        magic_count=$((magic_count + hits))
    done < <(find "$shader_dir" -name '*.hlsl' -print0 2>/dev/null)

    if [ "$magic_count" -gt 10 ]; then
        GAPS+=("MAGIC-NUMBERS: ~$magic_count potential magic number literals in shaders — should use named constants with @ref")
    fi
fi

# ── Output ──────────────────────────────────────────────────────────────────
if [ ${#GAPS[@]} -eq 0 ]; then
    echo "No gaps detected."
else
    echo "=== UzEngine Gap Detection ==="
    for gap in "${GAPS[@]}"; do
        echo -e "  [GAP] $gap"
    done
    echo "=== ${#GAPS[@]} gap(s) found ==="
fi
