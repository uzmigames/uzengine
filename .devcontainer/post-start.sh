#!/usr/bin/env bash
# post-start.sh — runs every time the container starts (after rebuild too).
# Lightweight: only syncs compile_commands.json symlink for clangd.
set -euo pipefail

WORKSPACE="/workspaces/UzEngine"
BUILD_DIR="${WORKSPACE}/build-container"

# Symlink compile_commands.json from Debug config to workspace root
# so clangd picks it up automatically without manual configuration.
COMPILE_COMMANDS="${BUILD_DIR}/Debug/compile_commands.json"
if [ -f "${COMPILE_COMMANDS}" ]; then
  ln -sf "${COMPILE_COMMANDS}" "${WORKSPACE}/compile_commands.json"
  echo "[post-start] compile_commands.json linked (clangd ready)"
elif [ -d "${BUILD_DIR}" ]; then
  # Ninja Multi-Config puts it at the build root, not config subdir
  if [ -f "${BUILD_DIR}/compile_commands.json" ]; then
    ln -sf "${BUILD_DIR}/compile_commands.json" "${WORKSPACE}/compile_commands.json"
    echo "[post-start] compile_commands.json linked from build root"
  fi
fi
