#!/usr/bin/env bash
# post-create.sh — runs ONCE after the container is first created.
# Safe to call manually at any time (all steps are idempotent).
set -euo pipefail

WORKSPACE="/workspaces/UzEngine"
BUILD_DIR="${WORKSPACE}/build-container"

# ---------------------------------------------------------------------------
# 1. Claude Code — bypassPermissions inside container
#    Writes ~/.claude/settings.json so claude never asks for permission.
#    This is safe: the container is an isolated, ephemeral environment.
# ---------------------------------------------------------------------------
echo "=== [1/5] Configuring Claude Code (bypassPermissions) ==="
mkdir -p "${HOME}/.claude"
cat > "${HOME}/.claude/settings.json" <<'EOF'
{
  "permissions": {
    "allow": [
      "Bash(*)",
      "Read(*)",
      "Write(*)",
      "Edit(*)",
      "Glob(*)",
      "Grep(*)",
      "TodoWrite(*)",
      "Agent(*)",
      "WebFetch(*)",
      "WebSearch(*)"
    ],
    "deny": []
  }
}
EOF
echo "  Claude settings written to ${HOME}/.claude/settings.json"

# ---------------------------------------------------------------------------
# 2. Node dependencies
# ---------------------------------------------------------------------------
echo "=== [2/5] Installing Node dependencies ==="
cd "${WORKSPACE}/vscode-uzengine"
npm ci --prefer-offline 2>/dev/null || npm install

# ---------------------------------------------------------------------------
# 3. CMake configure
# ---------------------------------------------------------------------------
echo "=== [3/5] Configuring CMake (Ninja Multi-Config, x64-linux) ==="
cmake \
  -B "${BUILD_DIR}" \
  -G "Ninja Multi-Config" \
  -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  "${WORKSPACE}"

# ---------------------------------------------------------------------------
# 4. Build UzEngineTests (no GPU required — pure C++ unit tests)
# ---------------------------------------------------------------------------
echo "=== [4/5] Building UzEngineTests (Debug) ==="
cmake --build "${BUILD_DIR}" --config Debug --target UzEngineTests --parallel

# ---------------------------------------------------------------------------
# 5. Run all tests to verify the setup is working
# ---------------------------------------------------------------------------
echo "=== [5/5] Running C++ tests ==="
ctest \
  --test-dir "${BUILD_DIR}" \
  --build-config Debug \
  --output-on-failure \
  --parallel "$(nproc)"

echo ""
echo "========================================="
echo " Dev Container ready!"
echo "========================================="
echo ""
echo " C++ tests  : cd ${WORKSPACE} && ctest --test-dir build-container --build-config Debug -V"
echo " TS tests   : cd ${WORKSPACE}/vscode-uzengine && npm test"
echo " Build all  : cmake --build build-container --config Debug --parallel"
echo " Claude     : claude (bypassPermissions already configured)"
echo ""
