#!/usr/bin/env bash
# UzEngine Lint — Linux/Mac
# Usage: scripts/tools/lint.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

echo "============================================"
echo " UzEngine Lint"
echo "============================================"
echo ""

FAILED=()

# C++ (clang-tidy)
if command -v clang-tidy &>/dev/null; then
    RUNTIME="$ROOT/runtime"
    if [ -f "$RUNTIME/.clang-tidy" ]; then
        echo "[runtime] clang-tidy..."
        SOURCES=$(find "$RUNTIME/engine" -name "*.cpp" 2>/dev/null | head -200)
        if [ -n "$SOURCES" ]; then
            echo "$SOURCES" | xargs clang-tidy -p "$RUNTIME" -- -std=c++23 2>/dev/null || FAILED+=("runtime-cpp")
        fi
        echo "[runtime] OK"
    fi
else
    echo "[runtime] SKIP — clang-tidy not found"
fi

# TypeScript (eslint)
if [ -f "$ROOT/extension/package.json" ]; then
    echo "[extension] eslint..."
    cd "$ROOT/extension"
    if command -v npx &>/dev/null; then
        npx eslint src/ --ext .ts 2>/dev/null || FAILED+=("extension-ts")
    else
        echo "  SKIP — npx not found"
    fi
fi

# Report
echo ""
if [ ${#FAILED[@]} -eq 0 ]; then
    echo "Lint passed."
else
    echo "FAILED: ${FAILED[*]}"
    exit 1
fi
