#!/usr/bin/env bash
# validate-commit.sh — Pre-commit validation hook for UzEngine
# Runs before git commit to enforce code quality rules.
# Adapted from Claude-Code-Game-Studios pattern.
#
# Checks:
#   1. Shader files being committed have @ref citations
#   2. No TODO/FIXME/HACK/TEMP markers in staged files
#   3. No .hlsl.old files being committed
#   4. Shader entry points use 'main' (not PSMain/VSMain/CSMain)
#   5. No hardcoded secrets or paths

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ERRORS=()
WARNINGS=()

# Get staged files
STAGED=$(cd "$ROOT" && git diff --cached --name-only --diff-filter=ACM 2>/dev/null || true)

if [ -z "$STAGED" ]; then
    echo "No staged files to validate."
    exit 0
fi

# ── 1. Shader @ref citation check ───────────────────────────────────────────
while IFS= read -r file; do
    [[ "$file" == runtime/shaders/runtime/include/* ]] && continue
    if [[ "$file" == *.hlsl ]]; then
        full="$ROOT/$file"
        if [ -f "$full" ] && ! grep -q '@ref' "$full" 2>/dev/null; then
            ERRORS+=("SHADER-REF: $file has no @ref citation (Rule 3: mandatory citation)")
        fi
    fi
done <<< "$STAGED"

# ── 2. Forbidden markers (TODO/FIXME/HACK/TEMP/placeholder) ────────────────
while IFS= read -r file; do
    if [[ "$file" == *.cpp || "$file" == *.h || "$file" == *.hlsl || "$file" == *.hlsli ]]; then
        full="$ROOT/$file"
        if [ -f "$full" ]; then
            hits=$(cd "$ROOT" && git diff --cached -- "$file" | grep '^\+' | grep -c 'TODO\|FIXME\|HACK\|TEMP\|placeholder' 2>/dev/null || true)
            if [ "$hits" -gt 0 ]; then
                ERRORS+=("FORBIDDEN-MARKER: $file adds $hits TODO/FIXME/HACK/TEMP/placeholder markers (Rule 4: no stubs)")
            fi
        fi
    fi
done <<< "$STAGED"

# ── 3. No .hlsl.old files ──────────────────────────────────────────────────
while IFS= read -r file; do
    if [[ "$file" == *.hlsl.old ]]; then
        ERRORS+=("STALE-FILE: $file is a .hlsl.old backup — should not be committed")
    fi
done <<< "$STAGED"

# ── 4. Shader entry point = main ────────────────────────────────────────────
while IFS= read -r file; do
    if [[ "$file" == *.hlsl ]] && [[ "$file" != *"/include/"* ]]; then
        full="$ROOT/$file"
        if [ -f "$full" ]; then
            # Check for PSMain, VSMain, CSMain as function definitions
            if grep -qP '^\s*(float4|void|half4)\s+(PSMain|VSMain|CSMain|GSMain)\s*\(' "$full" 2>/dev/null; then
                ERRORS+=("ENTRY-POINT: $file uses non-standard entry point — must be 'main' (ShaderManager rule)")
            fi
        fi
    fi
done <<< "$STAGED"

# ── 5. No hardcoded paths or secrets ────────────────────────────────────────
while IFS= read -r file; do
    if [[ "$file" == *.cpp || "$file" == *.h ]]; then
        full="$ROOT/$file"
        if [ -f "$full" ]; then
            # Check for hardcoded Windows paths in new lines
            path_hits=$(cd "$ROOT" && git diff --cached -- "$file" | grep '^\+' | grep -cP '[A-Z]:\\\\' 2>/dev/null || true)
            if [ "$path_hits" -gt 0 ]; then
                WARNINGS+=("HARDCODED-PATH: $file adds $path_hits hardcoded Windows paths — use uz::GetExeDir() or relative paths")
            fi
        fi
    fi
done <<< "$STAGED"

# ── 6. Renderer .cpp files should have @ref ─────────────────────────────────
while IFS= read -r file; do
    if [[ "$file" == runtime/engine/renderer/*.cpp || "$file" == runtime/engine/renderer/**/*.cpp ]]; then
        full="$ROOT/$file"
        if [ -f "$full" ] && ! grep -q '@ref' "$full" 2>/dev/null; then
            WARNINGS+=("CPP-REF: $file has no @ref citation — rendering code should cite published references (Rule 2)")
        fi
    fi
done <<< "$STAGED"

# ── Output ──────────────────────────────────────────────────────────────────
if [ ${#WARNINGS[@]} -gt 0 ]; then
    echo "=== Commit Warnings ==="
    for w in "${WARNINGS[@]}"; do
        echo "  [WARN] $w"
    done
fi

if [ ${#ERRORS[@]} -gt 0 ]; then
    echo "=== Commit Validation FAILED ==="
    for e in "${ERRORS[@]}"; do
        echo "  [ERROR] $e"
    done
    echo "=== ${#ERRORS[@]} error(s) — fix before committing ==="
    exit 1
fi

if [ ${#WARNINGS[@]} -gt 0 ]; then
    echo "=== ${#WARNINGS[@]} warning(s) — commit allowed but please address ==="
fi

echo "Commit validation passed."
exit 0
