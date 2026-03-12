#!/usr/bin/env bash
# build.sh — UzEngine full build pipeline (macOS/Linux)
# Mirrors build.ps1 for Unix systems.
#
# Usage:
#   ./scripts/build.sh [--compiler=zig|clang]
#
# Options:
#   --compiler=zig    Build runtime with Zig (default)
#   --compiler=clang  Build runtime with Clang via CMake preset

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"

# ─── Colors ──────────────────────────────────────────────────────────────────

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
GRAY='\033[0;37m'
CYAN='\033[0;36m'
NC='\033[0m'

# ─── Argument Parsing ─────────────────────────────────────────────────────────

COMPILER="zig"
for arg in "$@"; do
    if [[ "$arg" =~ ^--compiler=(.+)$ ]]; then
        COMPILER="${BASH_REMATCH[1]}"
    fi
done

echo -e "${CYAN}============================================${NC}"
echo -e "${CYAN} UzEngine Full Build Pipeline${NC}"
echo -e "${CYAN} Compiler: $COMPILER${NC}"
echo -e "${CYAN}============================================${NC}"
echo ""

# ─── Step 1: Runtime ─────────────────────────────────────────────────────────

echo -e "${YELLOW}[1/4] Building runtime ($COMPILER)...${NC}"
pushd "$ROOT/runtime" > /dev/null

case "$COMPILER" in
    zig)
        zig build -Doptimize=ReleaseFast
        ;;
    clang)
        cmake --preset clang-release 2>/dev/null
        cmake --build build/clang-release --config Release
        ;;
    *)
        echo -e "${RED}  Unknown compiler: $COMPILER. Use zig or clang.${NC}"
        popd > /dev/null
        exit 1
        ;;
esac

popd > /dev/null
echo -e "${GREEN}      OK${NC}"
echo ""

# ─── Step 2: N-API bridge (cmake-js + Electron) ───────────────────────────────

NAPI_DIR="$ROOT/napi"
if [[ -f "$NAPI_DIR/package.json" ]]; then
    echo -e "${YELLOW}[2/4] Building napi (cmake-js + Electron)...${NC}"
    pushd "$NAPI_DIR" > /dev/null
    npx cmake-js compile --runtime=electron --runtime-version=39.6.0 --CDCMAKE_BUILD_TYPE=Release
    popd > /dev/null
    echo -e "${GREEN}      OK${NC}"
    echo ""

    # Copy .node to extension/native/
    if [[ -d "$ROOT/extension" ]]; then
        echo -e "${GRAY}      Copying uzengine.node to extension/native/...${NC}"
        mkdir -p "$ROOT/extension/native"
        cp "$NAPI_DIR/build/Release/uzengine.node" "$ROOT/extension/native/uzengine.node"
        echo -e "${GREEN}      OK${NC}"
        echo ""
    fi
else
    echo -e "${GRAY}[2/4] napi/ not found — skipping.${NC}"
    echo ""
fi

# ─── Step 3: MCP server (TypeScript) ─────────────────────────────────────────

MCP_DIR="$ROOT/mcp"
if [[ -f "$MCP_DIR/package.json" ]]; then
    echo -e "${YELLOW}[3/4] Building mcp (tsc)...${NC}"
    pushd "$MCP_DIR" > /dev/null
    npx tsc -p .
    popd > /dev/null
    echo -e "${GREEN}      OK${NC}"
    echo ""
else
    echo -e "${GRAY}[3/4] mcp/ not found — skipping.${NC}"
    echo ""
fi

# ─── Step 4: Extension (TypeScript) ──────────────────────────────────────────

EXTENSION_DIR="$ROOT/extension"
if [[ -f "$EXTENSION_DIR/package.json" ]]; then
    echo -e "${YELLOW}[4/4] Building extension (tsc)...${NC}"
    pushd "$EXTENSION_DIR" > /dev/null
    npx tsc -p .
    popd > /dev/null
    echo -e "${GREEN}      OK${NC}"
    echo ""
else
    echo -e "${GRAY}[4/4] extension/ not found — skipping.${NC}"
    echo ""
fi

# ─── Done ────────────────────────────────────────────────────────────────────

echo -e "${CYAN}============================================${NC}"
echo -e "${CYAN} Build complete! (compiler: $COMPILER)${NC}"
echo -e "${CYAN}============================================${NC}"
