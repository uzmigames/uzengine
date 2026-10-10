#!/bin/bash
# Pre-build hook: delete stale shader .gen.h files so uzembed regenerates them.
# This ensures HLSL changes are never masked by cached bytecode headers.

RUNTIME_DIR="$(cd "$(dirname "$0")/../.." && pwd)/runtime"
GEN_DIR="$RUNTIME_DIR/build/generated/shaders"

if [ -d "$GEN_DIR" ]; then
    rm -rf "$GEN_DIR"
fi