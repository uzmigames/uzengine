#!/usr/bin/env bash
# UzEngine Format — Linux/Mac
# Usage: scripts/tools/format.sh [--check]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHECK_ONLY=false

for arg in "$@"; do
    case "$arg" in
        --check) CHECK_ONLY=true ;;
    esac
done

ACTION=$( [ "$CHECK_ONLY" = true ] && echo "Checking format" || echo "Formatting" )

echo "============================================"
echo " UzEngine $ACTION"
echo "============================================"
echo ""

FAILED=()

# C++ (clang-format)
if command -v clang-format &>/dev/null; then
    RUNTIME="$ROOT/runtime"
    if [ -f "$RUNTIME/.clang-format" ]; then
        echo "[runtime] clang-format..."
        SOURCES=$(find "$RUNTIME/engine" "$RUNTIME/tests" -name "*.cpp" -o -name "*.h" 2>/dev/null | head -500)
        if [ -n "$SOURCES" ]; then
            if [ "$CHECK_ONLY" = true ]; then
                echo "$SOURCES" | xargs clang-format --dry-run --Werror 2>/dev/null || FAILED+=("runtime-cpp")
            else
                echo "$SOURCES" | xargs clang-format -i 2>/dev/null
            fi
        fi
        echo "[runtime] OK"
    fi
else
    echo "[runtime] SKIP — clang-format not found"
fi

# TypeScript (prettier)
if [ -f "$ROOT/extension/package.json" ]; then
    echo "[extension] prettier..."
    cd "$ROOT/extension"
    if command -v npx &>/dev/null; then
        if [ "$CHECK_ONLY" = true ]; then
            npx prettier --check "src/**/*.ts" 2>/dev/null || FAILED+=("extension-ts")
        else
            npx prettier --write "src/**/*.ts" 2>/dev/null
        fi
    else
        echo "  SKIP — npx not found"
    fi
fi

# Report
echo ""
if [ ${#FAILED[@]} -eq 0 ]; then
    echo "Format OK."
else
    echo "FAILED: ${FAILED[*]}"
    exit 1
fi
