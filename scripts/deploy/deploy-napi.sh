#!/usr/bin/env bash
# Deploy N-API addon + shared libs to extension/native/ — Linux/Mac
# Usage: scripts/deploy/deploy-napi.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="$ROOT/napi/build/Release"
DST="$ROOT/extension/native"

if [ "$(uname)" = "Darwin" ]; then
    ADDON="$SRC/uzengine.node"
    LIB_EXT="dylib"
else
    ADDON="$SRC/uzengine.node"
    LIB_EXT="so"
fi

if [ ! -f "$ADDON" ]; then
    echo "ERROR: $ADDON not found — build napi first"
    exit 1
fi

mkdir -p "$DST"

echo "Deploying N-API addon..."
cp "$ADDON" "$DST/"
echo "  uzengine.node ($(du -h "$DST/uzengine.node" | cut -f1))"

# Copy all shared libs alongside the addon
for lib in "$SRC"/*."$LIB_EXT"; do
    [ -f "$lib" ] || continue
    cp "$lib" "$DST/"
    echo "  $(basename "$lib")"
done

echo ""
echo "Done."
