#!/usr/bin/env bash
# UzEngine Test Runner — Linux/Mac
# Usage: scripts/tools/test.sh [--compiler=zig|clang]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COMPILER="zig"

for arg in "$@"; do
    case "$arg" in
        --compiler=*) COMPILER="${arg#*=}" ;;
    esac
done

echo "============================================"
echo " UzEngine Testing"
echo " Compiler: $COMPILER"
echo "============================================"
echo ""

FAILED=()

# Runtime
echo "[runtime] Testing ($COMPILER)..."
cd "$ROOT/runtime"

case "$COMPILER" in
    zig)
        zig build test
        ;;
    clang)
        if [ ! -d "build/clang-debug" ]; then
            echo "  Building first..."
            cmake --preset clang-debug 2>/dev/null
            cmake --build build/clang-debug
        fi
        ./build/clang-debug/test_runtime
        ;;
    *)
        echo "ERROR: Unknown compiler '$COMPILER'. Use zig or clang."
        exit 1
        ;;
esac

if [ $? -ne 0 ]; then
    FAILED+=("runtime")
fi

# Extension (vitest)
cd "$ROOT"
if [ -f "extension/package.json" ]; then
    echo ""
    echo "[extension] vitest..."
    cd extension
    if command -v npx &>/dev/null; then
        npx vitest run --reporter=verbose 2>/dev/null || FAILED+=("extension")
    else
        echo "  SKIP - npx not found"
    fi
fi

# Report
echo ""
if [ ${#FAILED[@]} -eq 0 ]; then
    echo "All tests passed."
else
    echo "FAILED: ${FAILED[*]}"
    exit 1
fi
